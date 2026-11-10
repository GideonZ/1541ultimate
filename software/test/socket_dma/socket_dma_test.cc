/*
 * Host test of the socket DMA service (network/socket_dma.cc), port 64.
 *
 * The production dmaThread runs in a thread against host sockets on loopback.
 * C64 commands are recorded instead of executed, so each case checks exactly
 * what the C64 would have been given. Payload bytes never contain 0xFF, so a
 * payload tail that is misread as commands is skipped rather than executed.
 *
 * SOCKET_DMA_STALL_MS is built as 3000 here so the stall case runs in seconds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "socket_dma_stubs.h"
#include "network_config.h"
#include "socket_dma.h"

// ---- recorded C64 / file manager activity -------------------------------
struct Rec {
    int function_id;
    int mode;
    std::vector<uint8_t> data;
};
static std::mutex rec_mutex;
static std::vector<Rec> recs;

static std::vector<Rec> take_recs(void)
{
    std::lock_guard<std::mutex> g(rec_mutex);
    std::vector<Rec> r;
    r.swap(recs);
    return r;
}

#define REC_SAVE_TEMP (-1)

SubsysCommand::SubsysCommand(void *ui, int id, int func, int m, const void *buf, int len)
    : function_id(func), mode(m), buffer((void *)buf), bufferSize(len), direct_call(0) { }
SubsysCommand::SubsysCommand(void *ui, int id, int func, int m, const char *p, const char *f)
    : function_id(func), mode(m), buffer(0), bufferSize(0), direct_call(0) { }

int SubsysCommand::execute(void)
{
    Rec r;
    r.function_id = function_id;
    r.mode = mode;
    if (buffer && bufferSize > 0) {
        r.data.assign((uint8_t *)buffer, (uint8_t *)buffer + bufferSize);
    }
    {
        std::lock_guard<std::mutex> g(rec_mutex);
        recs.push_back(r);
    }
    delete this;
    return 0;
}

static C64 the_c64;
C64 *C64::getMachine(void) { return &the_c64; }
static FileManager the_fm;
FileManager *FileManager::getFileManager(void) { return &the_fm; }
FRESULT FileManager::save_temp_file(const char *dir, const char *name, uint8_t *buf, uint32_t len, mstring *path)
{
    Rec r;
    r.function_id = REC_SAVE_TEMP;
    r.mode = 0;
    r.data.assign(buf, buf + len);
    std::lock_guard<std::mutex> g(rec_mutex);
    recs.push_back(r);
    return FR_DENIED;
}
cart_def sid_cart = { 0, 0 };
cart_def boot_cart = { 0, 0 };
C1541 *c1541_A = 0;
void getProductTitleString(char *buf, int size, bool ascii) { strncpy(buf, "Host Test", size); }
void getProductVersionString(char *buf, int size, bool ascii) { strncpy(buf, "Host Test", size); }
int getFpgaVersion(void) { return 0; }
const char *getProductUniqueId(void) { return "0"; }

// ---- configuration ------------------------------------------------------
static std::atomic<int> cfg_dma_enabled(1);

int ConfigStore::get_value(uint8_t id) { return (id == CFG_NETWORK_ULTIMATE_DMA_SERVICE) ? cfg_dma_enabled.load() : 0; }
static char cfg_password[32];   // the network password; empty = none
const char *ConfigStore::get_string(uint8_t id) { return (id == CFG_NETWORK_PASSWORD) ? cfg_password : ""; }
static ConfigStore the_cfg;
NetworkConfig::NetworkConfig() { cfg = &the_cfg; }
NetworkConfig::~NetworkConfig() { }
void NetworkConfig::effectuate_settings(void) { }
NetworkConfig networkConfig;

// ---- RTOS -----------------------------------------------------------------
static std::atomic<int> listen_port(0);

int xTaskCreate(TaskFunction_t fn, const char *name, int stack, void *arg, int prio, void *handle)
{
    if (strcmp(name, "DMA Load Task") == 0) {
        std::thread(fn, arg).detach();   // the UDP ident task is not exercised
    }
    return 1;
}

void vTaskDelay(TickType_t ticks) { usleep((useconds_t)ticks * portTICK_PERIOD_MS * 1000); }

TickType_t xTaskGetTickCount(void)
{
    using namespace std::chrono;
    return (TickType_t)(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / portTICK_PERIOD_MS);
}

// Port 64 is privileged: bind the service to a free loopback port instead.
#undef bind
int sdma_test_bind(int s, const struct sockaddr *a, socklen_t l)
{
    struct sockaddr_in sa;
    memcpy(&sa, a, sizeof(sa));
    sa.sin_port = 0;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int r = bind(s, (struct sockaddr *)&sa, sizeof(sa));
    if (r == 0) {
        socklen_t sl = sizeof(sa);
        getsockname(s, (struct sockaddr *)&sa, &sl);
        listen_port = ntohs(sa.sin_port);
    }
    return r;
}

// ---- client side ------------------------------------------------------------
static void sleep_ms(int ms) { usleep(ms * 1000); }

static int client_connect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(listen_port.load());
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        perror("connect");
        exit(2);
    }
    return fd;
}

static void send_all(int fd, const std::vector<uint8_t> &v)
{
    size_t done = 0;
    while (done < v.size()) {
        ssize_t n = send(fd, v.data() + done, v.size() - done, 0);
        if (n <= 0) {
            return;   // the server may already have dropped us; the case checks that
        }
        done += n;
    }
}

// Waits up to 'ms' for the server to close. Returns the bytes it sent, and sets *closed.
static std::vector<uint8_t> drain(int fd, int ms, bool *closed)
{
    std::vector<uint8_t> got;
    *closed = false;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) {
            continue;
        }
        uint8_t b[4096];
        ssize_t n = recv(fd, b, sizeof(b), 0);
        if (n <= 0) {
            *closed = true;
            break;
        }
        got.insert(got.end(), b, b + n);
    }
    return got;
}

// Half-closes and waits for the server to finish the session.
static std::vector<uint8_t> finish(int fd)
{
    bool closed;
    shutdown(fd, SHUT_WR);
    std::vector<uint8_t> got = drain(fd, 10000, &closed);
    close(fd);
    if (!closed) {
        printf("  (server did not close within 10 s)\n");
    }
    return got;
}

static std::vector<uint8_t> payload(int len, int seed)
{
    std::vector<uint8_t> v(len);
    for (int i = 0; i < len; i++) {
        v[i] = (uint8_t)((i * 7 + seed * 31 + (i >> 8)) % 255);   // never 0xFF
    }
    return v;
}

static std::vector<uint8_t> header(uint16_t cmd, uint32_t len, bool three)
{
    std::vector<uint8_t> h = { (uint8_t)cmd, (uint8_t)(cmd >> 8), (uint8_t)len, (uint8_t)(len >> 8) };
    if (three) {
        h.push_back((uint8_t)(len >> 16));
    }
    return h;
}

static std::vector<uint8_t> cat(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static std::vector<uint8_t> slice(const std::vector<uint8_t> &v, size_t from, size_t to)
{
    return std::vector<uint8_t>(v.begin() + from, v.begin() + to);
}

// ---- checks -----------------------------------------------------------------
static int failures = 0, passes = 0;

static void report(const char *name, bool ok, const std::string &why)
{
    if (ok) {
        passes++;
        printf("PASS %s\n", name);
    } else {
        failures++;
        printf("FAIL %s: %s\n", name, why.c_str());
    }
    fflush(stdout);
}

static std::string describe(const std::vector<Rec> &r)
{
    std::string s = std::to_string(r.size()) + " command(s) executed";
    for (size_t i = 0; i < r.size(); i++) {
        s += "; #" + std::to_string(i) + " func " + std::to_string(r[i].function_id) +
             " mode " + std::to_string(r[i].mode) + " len " + std::to_string(r[i].data.size());
    }
    return s;
}

static bool is_dmaload(const Rec &r, const std::vector<uint8_t> &p)
{
    return r.function_id == C64_DMA_BUFFER && r.mode == RUNCODE_DMALOAD && r.data == p;
}

static const uint16_t CMD_DMA = 0xFF01, CMD_RUN_CRT = 0xFF0D, CMD_AUTH = 0xFF1F;
static const int STALL = SOCKET_DMA_STALL_MS;

static void t_plain(void)
{
    std::vector<uint8_t> p = payload(1000, 1);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 1000, false), p));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("plain_dma", r.size() == 1 && is_dmaload(r[0], p), describe(r));
}

static void t_gap_in_payload(void)
{
    std::vector<uint8_t> p = payload(1000, 2);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 1000, false), slice(p, 0, 500)));
    sleep_ms(1500);
    send_all(fd, slice(p, 500, 1000));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("payload_with_1500ms_gap_is_loaded_whole", r.size() == 1 && is_dmaload(r[0], p), describe(r));
}

static void t_idle_between_commands(void)
{
    std::vector<uint8_t> p1 = payload(300, 3), p2 = payload(300, 4);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 300, false), p1));
    sleep_ms(1500);
    send_all(fd, cat(header(CMD_DMA, 300, false), p2));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("idle_1500ms_between_commands_keeps_session",
           r.size() == 2 && is_dmaload(r[0], p1) && is_dmaload(r[1], p2), describe(r));
}

static void t_truncated_then_close(void)
{
    std::vector<uint8_t> p = payload(1000, 5);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 1000, false), slice(p, 0, 500)));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("truncated_payload_is_not_executed", r.empty(), describe(r));
}

static void t_split_header(void)
{
    std::vector<uint8_t> p = payload(1000, 6);
    std::vector<uint8_t> all = cat(header(CMD_DMA, 1000, false), p);
    int fd = client_connect();
    send_all(fd, slice(all, 0, 1));
    sleep_ms(300);
    send_all(fd, slice(all, 1, all.size()));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("header_split_across_segments", r.size() == 1 && is_dmaload(r[0], p), describe(r));
}

static void t_oversize(void)
{
    std::vector<uint8_t> big = payload(SOCKET_BUFFER_SIZE + 10000, 7);
    std::vector<uint8_t> p = payload(400, 8);
    int fd = client_connect();
    send_all(fd, cat(cat(header(CMD_RUN_CRT, big.size(), true), big), cat(header(CMD_DMA, 400, false), p)));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("oversize_payload_is_skipped_and_stream_stays_in_sync",
           r.size() == 1 && is_dmaload(r[0], p), describe(r));
}

static void t_stall_limit(void)
{
    std::vector<uint8_t> p = payload(1000, 9);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 1000, false), slice(p, 0, 500)));
    bool closed;
    auto t0 = std::chrono::steady_clock::now();
    drain(fd, 6000, &closed);
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    send_all(fd, slice(p, 500, 1000));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("stall_beyond_limit_drops_without_executing",
           r.empty() && closed && ms >= 2500, describe(r) + ", closed " + (closed ? "yes" : "no") + " after " + std::to_string(ms) + " ms");
}

// An authenticated client may wait between commands longer than the stall limit.
static void t_long_idle(void)
{
    std::vector<uint8_t> p1 = payload(200, 11), p2 = payload(200, 12);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 200, false), p1));
    sleep_ms(2 * STALL + 500);
    send_all(fd, cat(header(CMD_DMA, 200, false), p2));
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("authenticated_client_may_idle_past_the_stall_limit",
           r.size() == 2 && is_dmaload(r[0], p1) && is_dmaload(r[1], p2), describe(r));
}

// The stall limit applies from the first byte of a header: one byte and silence ends it.
static void t_one_header_byte_then_silence(void)
{
    int fd = client_connect();
    std::vector<uint8_t> h = header(CMD_DMA, 100, false);
    send_all(fd, slice(h, 0, 1));
    bool closed;
    auto t0 = std::chrono::steady_clock::now();
    drain(fd, STALL + 3000, &closed);
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    close(fd);
    std::vector<Rec> r = take_recs();
    report("one_header_byte_then_silence_is_dropped_at_the_stall_limit", closed && r.empty() && ms >= STALL - 500,
           describe(r) + ", closed " + (closed ? "yes" : "no") + " after " + std::to_string(ms) + " ms");
}

// The limit is on silence, not on the whole message: a slow upload that keeps coming
// is loaded whole even when it takes longer than the limit.
static void t_trickle(void)
{
    std::vector<uint8_t> p = payload(1000, 13);
    std::vector<uint8_t> all = cat(header(CMD_DMA, 1000, false), p);
    int fd = client_connect();
    size_t step = all.size() / 5 + 1;
    for (size_t off = 0; off < all.size(); off += step) {
        send_all(fd, slice(all, off, std::min(all.size(), off + step)));
        sleep_ms(STALL * 2 / 3);
    }
    finish(fd);
    std::vector<Rec> r = take_recs();
    report("slow_upload_longer_than_the_limit_is_loaded_whole", r.size() == 1 && is_dmaload(r[0], p), describe(r));
}

// With a password set, a client that has not authenticated must not hold the only slot.
static void t_unauthenticated_idle(void)
{
    strcpy(cfg_password, "pw");
    int fd = client_connect();
    bool closed;
    auto t0 = std::chrono::steady_clock::now();
    drain(fd, STALL + 3000, &closed);
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    close(fd);

    // an authenticated one may idle on
    std::vector<uint8_t> p = payload(100, 14);
    fd = client_connect();
    send_all(fd, cat(header(CMD_AUTH, 2, false), { 'p', 'w' }));
    sleep_ms(STALL + 1000);
    send_all(fd, cat(header(CMD_DMA, 100, false), p));
    std::vector<uint8_t> reply = finish(fd);
    cfg_password[0] = 0;
    std::vector<Rec> r = take_recs();
    report("unauthenticated_client_is_dropped_at_the_stall_limit", closed && ms >= STALL - 500 && ms < STALL + 2500,
           std::string("closed ") + (closed ? "yes" : "no") + " after " + std::to_string(ms) + " ms");
    report("authenticated_client_with_password_may_idle", reply.size() == 1 && reply[0] == 1 && r.size() == 1 && is_dmaload(r[0], p),
           describe(r));
}

// With a password set, a client that has not authenticated is dropped at once for any command but
// AUTHENTICATE: its payload is not read, so a header that announces a large payload does not hold
// the only slot while the server waits for the bytes.
static void t_unauthenticated_command(void)
{
    strcpy(cfg_password, "pw");
    int fd = client_connect();
    send_all(fd, header(CMD_RUN_CRT, 0x100000, true));
    bool closed;
    auto t0 = std::chrono::steady_clock::now();
    drain(fd, STALL + 3000, &closed);
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    close(fd);
    cfg_password[0] = 0;
    std::vector<Rec> r = take_recs();
    report("unauthenticated_command_is_dropped_without_reading_its_payload", closed && ms < 1000 && r.empty(),
           std::string("closed ") + (closed ? "yes" : "no") + " after " + std::to_string(ms) + " ms, " + std::to_string(r.size()) + " actions");
}

static void t_disable_while_idle(SocketDMA *dma)
{
    std::vector<uint8_t> p = payload(100, 10);
    int fd = client_connect();
    send_all(fd, cat(header(CMD_DMA, 100, false), p));
    sleep_ms(200);
    cfg_dma_enabled = 0;
    dma->effectuate_settings();
    bool closed;
    drain(fd, 3000, &closed);
    close(fd);
    std::vector<Rec> r = take_recs();
    report("disabling_the_service_ends_an_idle_session", closed && r.size() == 1 && is_dmaload(r[0], p),
           describe(r) + ", closed " + (closed ? "yes" : "no"));
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    SocketDMA *dma = new SocketDMA();
    for (int i = 0; i < 200 && !listen_port.load(); i++) {
        sleep_ms(10);
    }
    if (!listen_port.load()) {
        printf("FAIL service did not start listening\n");
        return 1;
    }
    t_plain();
    t_gap_in_payload();
    t_idle_between_commands();
    t_truncated_then_close();
    t_split_header();
    t_oversize();
    t_stall_limit();
    t_long_idle();
    t_one_header_byte_then_silence();
    t_trickle();
    t_unauthenticated_idle();
    t_unauthenticated_command();
    t_disable_while_idle(dma);
    printf("socket_dma: %d passed, %d failed\n", passes, failures);
    fflush(stdout);
    _exit(failures ? 1 : 0);
}
