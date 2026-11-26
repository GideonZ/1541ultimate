/*
 * Host test of the remote syslog forwarder (network/syslog.cc).
 *
 * The production syslog task runs in a host thread and sends to UDP collectors on
 * loopback. The test changes "Log to Syslog Server" the way a REST call or the
 * menu does (set the string, then close the config, which effectuates the stores)
 * and checks that logging starts, retargets and stops without a restart.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "syslog_stubs.h"
#include "network_config.h"
#include "syslog.h"

// ---- stubs -----------------------------------------------------------------
static std::recursive_mutex safe_mutex;
void syslog_test_enter(void) { safe_mutex.lock(); }
void syslog_test_leave(void) { safe_mutex.unlock(); }

static std::atomic<int> tasks_created(0);
int xTaskCreate(TaskFunction_t fn, const char *name, int stack, void *arg, int prio, TaskHandle_t *handle)
{
    ++tasks_created;
    if (handle) *handle = (TaskHandle_t)1;
    std::thread(fn, arg).detach();
    return pdPASS;
}
// Delays run four times faster than the firmware's ticks, so the test is quick
void vTaskDelay(TickType_t ticks) { usleep(ticks * portTICK_PERIOD_MS * 1000 / 4); }
void vTaskDelete(TaskHandle_t) { pthread_exit(NULL); }
void vTaskSuspend(TaskHandle_t) { for (;;) sleep(1); }
NetworkInterface *NetworkInterface::DoWeHaveLink(void) { return (NetworkInterface *)1; }

int syslog_test_sockets_opened = 0;
int syslog_test_sockets_open = 0;
#undef socket
#undef closesocket
int syslog_test_socket(int d, int t, int p)
{
    int s = socket(d, t, p);
    if (s >= 0) { ++syslog_test_sockets_opened; ++syslog_test_sockets_open; }
    return s;
}
int syslog_test_close(int s)
{
    --syslog_test_sockets_open;
    return close(s);
}

static std::vector<ConfigurableObject *> objects;
static std::string server_setting;
void ConfigStore::addObject(ConfigurableObject *o) { objects.push_back(o); }
void ConfigStore::effectuate(void) { for (ConfigurableObject *o : objects) o->effectuate_settings(); }
const char *ConfigStore::get_string(uint8_t id)
{
    return (id == CFG_NETWORK_REMOTE_SYSLOG_SERVER) ? server_setting.c_str() : "";
}
void ConfigStore::set_string(uint8_t id, const char *s)
{
    if (id == CFG_NETWORK_REMOTE_SYSLOG_SERVER) server_setting = s;
}
static ConfigStore the_cfg;
NetworkConfig::NetworkConfig() { cfg = &the_cfg; }
NetworkConfig::~NetworkConfig() { }
void NetworkConfig::effectuate_settings(void) { }
NetworkConfig networkConfig;

Syslog syslog;

// ---- helpers ---------------------------------------------------------------
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("  FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Changing the setting as REST does: set the item, then the store is closed.
static void set_server(const char *s)
{
    networkConfig.cfg->set_string(CFG_NETWORK_REMOTE_SYSLOG_SERVER, s);
    networkConfig.cfg->effectuate();
}

// Logging as the firmware does it: every character goes through custom_outbyte,
// which ends in Syslog::charout.
static void log_line(const char *text)
{
    for (const char *p = text; *p; p++) syslog.charout(*p);
    syslog.charout('\n');
}

struct Collector {
    int fd;
    int port;
    Collector() {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        bind(fd, (struct sockaddr *)&a, sizeof(a));
        socklen_t l = sizeof(a);
        getsockname(fd, (struct sockaddr *)&a, &l);
        port = ntohs(a.sin_port);
    }
    ~Collector() { close(fd); }
    // Waits up to ms for one datagram; "" when none arrived
    std::string recv_one(int ms) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, ms) <= 0) return "";
        char b[2048];
        int n = recv(fd, b, sizeof(b), 0);
        return n > 0 ? std::string(b, n) : "";
    }
    // Everything that arrives within ms
    std::vector<std::string> drain(int ms) {
        std::vector<std::string> v;
        for (;;) { std::string s = recv_one(ms); if (s.empty()) break; v.push_back(s); }
        return v;
    }
};

static std::string target(Collector &c) { return "127.0.0.1:" + std::to_string(c.port); }

// Logs a marker and says whether it reached the collector
static bool delivered(Collector &c, const char *marker)
{
    log_line(marker);
    for (int i = 0; i < 20; i++) {
        std::string s = c.recv_one(250);
        if (s == marker) return true;
    }
    return false;
}
static bool silent(Collector &c, const char *marker)
{
    log_line(marker);
    return c.recv_one(1000).empty();
}

// ---- cases -------------------------------------------------------------------
static int run_runtime(void)
{
    Collector a, b;
    printf("collectors on %d and %d\n", a.port, b.port);

    printf("setting empty at start: nothing is sent\n");
    server_setting = "";
    CHECK(!syslog.init(16 * 1024), "init with an empty setting must report syslog off");
    CHECK(silent(a, "boot-while-off"), "datagram arrived while syslog is off");
    CHECK(tasks_created == 0, "a task was made without a server");

    printf("set at runtime: logging starts without a restart\n");
    set_server(target(a).c_str());
    CHECK(delivered(a, "after-enable"), "no datagram after setting the server at runtime");

    printf("retarget to another port: logging follows\n");
    set_server(target(b).c_str());
    usleep(400000);  // The task switches sockets on its next pass; lines logged before that still go to the old one
    a.drain(100);
    CHECK(delivered(b, "after-retarget"), "no datagram on the new target");
    a.drain(300);
    CHECK(silent(a, "old-target-must-not-hear"), "old target still receives");
    CHECK(syslog_test_sockets_open == 1, "sockets open after retarget: %d", syslog_test_sockets_open);

    printf("an unrelated settings change keeps the socket\n");
    int opened = syslog_test_sockets_opened;
    networkConfig.cfg->effectuate();
    CHECK(delivered(b, "after-noop-effectuate"), "logging stopped by an unrelated change");
    CHECK(syslog_test_sockets_opened == opened, "socket reopened without a change");

    printf("cleared: logging stops\n");
    set_server("");
    b.drain(500);
    CHECK(silent(b, "after-clear"), "datagram arrived after clearing the setting");
    CHECK(syslog_test_sockets_open == 0, "socket still open after clear: %d", syslog_test_sockets_open);

    printf("invalid values stop logging\n");
    set_server(target(a).c_str());
    CHECK(delivered(a, "valid-again"), "no datagram after re-enabling");
    set_server("not-an-address");
    a.drain(500);
    CHECK(silent(a, "after-invalid"), "datagram arrived after an invalid value");
    set_server(("127.0.0.1:99999"));
    CHECK(silent(a, "after-bad-port"), "datagram arrived after an invalid port");

    printf("re-enable after stopping: no stale text, no leak\n");
    log_line("logged-while-off");
    set_server(target(a).c_str());
    CHECK(delivered(a, "after-reenable"), "no datagram after re-enabling");
    std::vector<std::string> more = a.drain(500);
    for (auto &s : more) CHECK(s != "logged-while-off", "text logged while off was sent");

    printf("toggling repeatedly while another task logs\n");
    std::atomic<bool> run(true);
    std::thread logger([&]() { int n = 0; while (run) { log_line(("spam " + std::to_string(n++)).c_str()); usleep(200); } });
    for (int i = 0; i < 30; i++) {
        set_server(i % 3 == 0 ? "" : target(i % 2 ? a : b).c_str());
        usleep(20000);
    }
    run = false;
    logger.join();
    set_server("");
    usleep(500000);
    CHECK(syslog_test_sockets_open == 0, "sockets leaked: %d", syslog_test_sockets_open);
    CHECK(tasks_created == 1, "tasks created: %d (expected one for the lifetime)", (int)tasks_created);

    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("all syslog tests passed\n");
    return 0;
}

// A value that is already saved when the firmware starts: init() must start logging.
static int run_boot(void)
{
    Collector a;
    server_setting = target(a);
    CHECK(syslog.init(16 * 1024), "init must report syslog on for a saved server");
    CHECK(delivered(a, "boot-with-saved-server"), "no datagram for a server saved in flash");
    CHECK(tasks_created == 1, "tasks created: %d", (int)tasks_created);
    printf(failures ? "%d FAILED\n" : "boot test passed\n", failures);
    return failures ? 1 : 0;
}

// A saved value that is not an address is reported at start, and nothing is sent.
static int run_boot_invalid(void)
{
    char name[] = "/tmp/syslogtestXXXXXX";
    int fd = mkstemp(name);
    fflush(stdout);
    int saved = dup(1);
    dup2(fd, 1);
    server_setting = "bogus";
    bool on = syslog.init(16 * 1024);
    fflush(stdout);
    dup2(saved, 1);
    char text[1024] = "";
    lseek(fd, 0, SEEK_SET);
    read(fd, text, sizeof(text) - 1);
    close(fd);
    unlink(name);
    CHECK(!on, "init must report syslog off for an invalid server");
    CHECK(strstr(text, "Invalid syslog server specified"), "no message for an invalid saved server: '%s'", text);
    CHECK(tasks_created == 0, "a task was made for an invalid server");
    printf(failures ? "%d FAILED\n" : "boot invalid test passed\n", failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "boot")) return run_boot();
    if (argc > 1 && !strcmp(argv[1], "boot-invalid")) return run_boot_invalid();
    return run_runtime();
}
