/*
 * Host test of the FTP server's active-mode data connection (network/ftpd.cc):
 * FTPDataConnection connects to a loopback listener that plays the FTP
 * client's data port, or to an address that does not answer.
 *
 * Host Linux honours SO_SNDTIMEO on connect(), unlike lwIP, so the connect
 * cases here show that the bounded connect works on POSIX sockets; that the
 * old code waits for TCP's own timeout on the device is shown on the bench.
 */
#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <chrono>
#include <string>
#include <thread>

#include "ftpd_stubs.h"
// The test sets the session fields a PORT command would; they are private.
#include "indexed_list.h"
#include "vfs.h"
#define class struct
#define private public
#include "ftpd.h"
#undef private
#undef class
#include "network_config.h"

// ---- file system: not used by the connect cases, but ftpd.cc links against it ----
extern "C" {
vfs_file_t *vfs_open(vfs_t *fs, const char *name, const char *flags) { return NULL; }
void vfs_close(vfs_file_t *file) { }
int vfs_write(const void *buffer, int chunks, int chunk_len, vfs_file_t *file)
{
    return chunks * chunk_len;
}
int vfs_read(void *buffer, int chunks, int chunk_len, vfs_file_t *file) { return 0; }
vfs_t *vfs_openfs() { return NULL; }
void vfs_closefs(vfs_t *) { }
vfs_dir_t *vfs_opendir(vfs_t *fs, const char *name, into_mode_t into_mode) { return NULL; }
void vfs_closedir(vfs_dir_t *dir) { }
vfs_dirent_t *vfs_readdir(vfs_dir_t *dir) { return NULL; }
int vfs_stat_dirent(vfs_dirent_t *ent, vfs_stat_t *st) { return -1; }
int vfs_stat(vfs_t *fs, const char *name, vfs_stat_t *st) { return -1; }
int vfs_chdir(vfs_t *fs, const char *name) { return -1; }
char *vfs_getcwd(vfs_t *fs, void *args, int dummy) { return (char *)"/"; }
int vfs_rename(vfs_t *fs, const char *old_name, const char *new_name) { return -1; }
int vfs_mkdir(vfs_t *fs, const char *name, int flags) { return -1; }
int vfs_rmdir(vfs_t *fs, const char *name) { return -1; }
int vfs_remove(vfs_t *fs, const char *name) { return -1; }
}

// ---- the rest of the firmware ---------------------------------------------------
int _my_vnprintf(void (*putc)(char c, void **param), void **param, size_t maxlen, const char *fmt, va_list ap)
{
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    for (int i = 0; i < n && i < (int)maxlen && tmp[i]; i++) putc(tmp[i], param);
    return n;
}
void getProductTitleString(char *buf, int size, bool ascii) { strncpy(buf, "Host Test", size); }
const char *getProductString(void) { return "Host Test"; }
Rtc rtc;
static ConfigStore the_cfg;
NetworkConfig::NetworkConfig() { cfg = &the_cfg; }
NetworkConfig::~NetworkConfig() { }
void NetworkConfig::effectuate_settings(void) { }
NetworkConfig networkConfig;
int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, TaskHandle_t *handle) { return pdPASS; }
void vTaskDelay(TickType_t ticks) { usleep((useconds_t)ticks * portTICK_PERIOD_MS * 1000); }
void vTaskDelete(TaskHandle_t t) { }
TickType_t xTaskGetTickCount(void)
{
    using namespace std::chrono;
    return (TickType_t)(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / portTICK_PERIOD_MS);
}

// ---- helpers -------------------------------------------------------------------------
static int failures, passes;
static void report(const char *name, bool ok, const std::string &why)
{
    if (ok) { passes++; printf("PASS %s - %s\n", name, why.c_str()); }
    else { failures++; printf("FAIL %s - %s\n", name, why.c_str()); }
    fflush(stdout);
}

static long ms_since(std::chrono::steady_clock::time_point t0)
{
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// A session that has seen "PORT a,b,c,d,p1,p2".
static FTPDaemonThread *port_session(uint32_t ip_host_order, uint16_t port)
{
    FTPDaemonThread *t = (FTPDaemonThread *)calloc(1, sizeof(FTPDaemonThread));
    t->passive = 0;
    t->dataip.addr = htonl(ip_host_order);
    t->dataport = port;
    return t;
}

static int listener(uint16_t *port)
{
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) || listen(l, 1)) { perror("listen"); exit(2); }
    socklen_t sl = sizeof(a);
    getsockname(l, (struct sockaddr *)&a, &sl);
    *port = ntohs(a.sin_port);
    return l;
}

// ---- cases -----------------------------------------------------------------------------
static void t_connect_refused(void)
{
    uint16_t port;
    int l = listener(&port);
    close(l);   // nothing listens there now
    FTPDaemonThread *t = port_session(INADDR_LOOPBACK, port);
    FTPDataConnection *conn = new FTPDataConnection(t);
    auto t0 = std::chrono::steady_clock::now();
    FTPTransferResult r = conn->receivefile(NULL, "x.bin");
    long ms = ms_since(t0);
    delete conn;
    free(t);
    report("active_connect_refused_fails_at_once", r == FTP_TRANSFER_SETUP_FAILED && ms < 1000,
           "result " + std::to_string(r) + " after " + std::to_string(ms) + " ms");
}

static void t_connect_unanswered(void)
{
    // 10.255.255.1: routed out of the container, never answered.
    FTPDaemonThread *t = port_session(0x0AFFFF01, 9);
    FTPDataConnection *conn = new FTPDataConnection(t);
    auto t0 = std::chrono::steady_clock::now();
    FTPTransferResult r = conn->receivefile(NULL, "x.bin");
    long ms = ms_since(t0);
    delete conn;
    free(t);
    report("active_connect_to_a_silent_address_gives_up_within_5s", r == FTP_TRANSFER_SETUP_FAILED && ms < 6500,
           "result " + std::to_string(r) + " after " + std::to_string(ms) + " ms");
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    t_connect_refused();
    t_connect_unanswered();
    printf("ftpd: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
