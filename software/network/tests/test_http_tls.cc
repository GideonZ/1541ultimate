#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include "tls_http_connection.h"
#include "http_request.h"
#include "http_target.h"
#include "mbedtls/x509_crt.h"

static int checks, failures;
static void expect(bool value, const char *label)
{
    ++checks;
    if (!value) { ++failures; printf("FAIL %s\n", label); }
}

// Count only allocations routed through the linked objects, including the TLS
// library and core. This is scoped ownership accounting, not process RSS.
static void *allocations[16384];
static int live_allocations;
static bool reject_allocation;
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_calloc(size_t n, size_t size)
{
    if (reject_allocation) { reject_allocation = false; return NULL; }
    void *p = __real_calloc(n, size);
    if (p) {
        for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i) {
            if (!allocations[i]) { allocations[i] = p; ++live_allocations; return p; }
        }
        abort();
    }
    return p;
}
extern "C" void __wrap_free(void *p)
{
    if (p) {
        for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i) {
            if (allocations[i] == p) { allocations[i] = NULL; --live_allocations; break; }
        }
    }
    __real_free(p);
}

static int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int descriptors()
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) return -1;
    int count = 0;
    while (dirent *entry = readdir(directory)) if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count;
}

struct Platform {
    int fd, opens, closes, open_delay;
    bool ready, random_ready, block_write;
    std::atomic<bool> cancelled;
    OperationCancellation command_cancellation;
    mbedtls_x509_crt roots;
    Platform(const char *ca) : fd(-1), opens(0), closes(0), open_delay(0),
        ready(true), random_ready(true), block_write(false), cancelled(false)
    {
        mbedtls_x509_crt_init(&roots);
        expect(mbedtls_x509_crt_parse_file(&roots, ca) == 0, "load test trust roots");
    }
    ~Platform() { expect(fd == -1, "transport already closed"); mbedtls_x509_crt_free(&roots); }
};

static int64_t clock_read(void *) { return now_ms(); }
static bool clock_ready(void *p) { return static_cast<Platform *>(p)->ready; }
static bool cancelled(void *p) {
    Platform *platform = static_cast<Platform *>(p);
    return platform->cancelled.load() || platform->command_cancellation.cancelled();
}
static int entropy(void *p, unsigned char *bytes, size_t length)
{
    if (!static_cast<Platform *>(p)->random_ready) return -1;
    while (length) {
        ssize_t n = getrandom(bytes, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        bytes += n; length -= n;
    }
    return 0;
}
static int trust(void *p, mbedtls_ssl_config *config)
{
    mbedtls_ssl_conf_ca_chain(config, &static_cast<Platform *>(p)->roots, NULL);
    return 0;
}
static int wait_for(Platform *p, short events, int64_t end)
{
    while (!cancelled(p)) {
        int64_t left = end - now_ms();
        if (left <= 0) return -1;
        pollfd fd = { p->fd, events, 0 };
        int result = poll(&fd, 1, left > 20 ? 20 : (int)left);
        if (result > 0) return 0;
        if (result < 0 && errno != EINTR) return -1;
    }
    return -1;
}
static int open_socket(void *context, const char *, uint16_t port, uint32_t timeout)
{
    Platform *p = static_cast<Platform *>(context);
    ++p->opens;
    int64_t end = now_ms() + timeout;
    p->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (p->fd < 0) return -1;
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int result = connect(p->fd, (sockaddr *)&address, sizeof(address));
    if (result < 0) {
        if (errno != EINPROGRESS || wait_for(p, POLLOUT, end)) return -1;
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) return -1;
    }
    for (int i = 0; i < p->open_delay && now_ms() < end && !cancelled(p); i += 10)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
}
static int read_socket(void *context, uint8_t *bytes, size_t size, uint32_t timeout)
{
    Platform *p = static_cast<Platform *>(context);
    if (wait_for(p, POLLIN, now_ms() + timeout)) return -1;
    return recv(p->fd, bytes, size > 37 ? 37 : size, 0);
}
static int write_socket(void *context, const uint8_t *bytes, size_t size, uint32_t timeout)
{
    Platform *p = static_cast<Platform *>(context);
    if (p->block_write) {
        int64_t end = now_ms() + timeout;
        while (now_ms() < end && !cancelled(p))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return -1;
    }
    if (wait_for(p, POLLOUT, now_ms() + timeout)) return -1;
    return send(p->fd, bytes, size > 29 ? 29 : size, MSG_NOSIGNAL);
}
static void close_socket(void *context)
{
    Platform *p = static_cast<Platform *>(context);
    ++p->closes;
    if (p->fd >= 0) { close(p->fd); p->fd = -1; }
}

static void exchange(const char *mode, const char *ca, int port)
{
    int initial_allocations = live_allocations;
    int initial_descriptors = descriptors();
    tls_stream_diagnostics observed = {};
    int64_t elapsed = 0;
    int opens = 0, closes = 0;
    {
        Platform platform(ca);
        tls_stream_port callbacks = { &platform, clock_read, clock_ready, cancelled,
            entropy, trust, open_socket, read_socket, write_socket, close_socket };
        const bool timeout_case = strstr(mode, "timeout") != NULL;
        const bool cancel_case = strstr(mode, "cancel") != NULL;
        const bool success = strcmp(mode, "valid") == 0 || strcmp(mode, "recovery") == 0 || strcmp(mode, "clean-eof") == 0 || strcmp(mode, "close-clean") == 0;
        const bool auth_error = strcmp(mode, "hostname") == 0 || strcmp(mode, "untrusted") == 0 ||
                                strcmp(mode, "expired") == 0 || strcmp(mode, "future") == 0;
        const bool write_failure = strcmp(mode, "write-timeout") == 0 || strcmp(mode, "write-cancel") == 0;
        platform.ready = strcmp(mode, "no-time") != 0;
        platform.random_ready = strcmp(mode, "no-entropy") != 0;
        if (strcmp(mode, "total-timeout") == 0) platform.open_delay = 250;
        if (strcmp(mode, "pre-cancel") == 0) platform.cancelled.store(true);
        std::thread canceller;
        if (strcmp(mode, "handshake-cancel") == 0)
            canceller = std::thread([&platform] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); platform.cancelled.store(true); });
        int64_t begin = now_ms();
        {
            TlsHttpConnection connection(callbacks, timeout_case ? 650 : 3000);
            if (strcmp(mode, "closed-before-open") == 0) {
                connection.close_session();
                connection.close_session();
            }
            {
                HttpRequest request(&connection);
                if (strcmp(mode, "allocation") == 0) reject_allocation = true;
                int opened = request.connect_to_server(strcmp(mode, "hostname") == 0 ? "wrong.lab.test" : "tls.lab.test", port);
                bool should_open = success || write_failure || strcmp(mode, "read-timeout") == 0 || strcmp(mode, "total-timeout") == 0 ||
                                   strcmp(mode, "read-cancel") == 0 || strcmp(mode, "truncated") == 0 || strcmp(mode, "close-truncated") == 0;
                expect((opened >= 0) == should_open, "authenticated open result");
                if (strcmp(mode, "closed-before-open") == 0)
                    expect(platform.opens == 0 && platform.closes == 0, "closed adapter cannot start a transport");
                bool complete = false;
                if (opened >= 0) {
                    platform.block_write = write_failure;
                    if (strcmp(mode, "write-cancel") == 0)
                        canceller = std::thread([&platform] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); platform.cancelled.store(true); });
                    StreamRamFile message(64);
                    message.format("GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n");
                    expect((request.send_request(&message) == 0) == !write_failure, "send HTTP request over TLS");
                    if (strcmp(mode, "read-cancel") == 0)
                        canceller = std::thread([&platform] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); platform.cancelled.store(true); });
                    complete = request.recv_response();
                    if (complete) {
                        uint8_t bytes[16] = {};
                        expect(request.read_response_data(sizeof(bytes), bytes) == 8 &&
                               memcmp(bytes, "tls-ok!\n", 8) == 0, "exact HTTP response body");
                        if (strcmp(mode, "clean-eof") == 0) {
                            uint8_t untouched = 0xA5;
                            expect(connection.read(&untouched, 1) == 0 && untouched == 0xA5,
                                   "authenticated EOF preserves caller buffer");
                            expect(connection.read(&untouched, 1) == 0 && untouched == 0xA5,
                                   "authenticated EOF stays terminal");
                        }
                    } else {
                        uint8_t byte = 0xA5;
                        expect(request.read_response_data(1, &byte) == 0 && byte == 0xA5,
                               "failed HTTP exchange exposes no partial response");
                    }
                }
                expect(complete == success, "HTTP completion matches case");
            }
            elapsed = now_ms() - begin;
            observed = connection.diagnostic();
            if (success) expect(observed.status == TLS_OK, "success diagnostics");
            if (auth_error) {
                uint32_t flag = strcmp(mode, "hostname") == 0 ? MBEDTLS_X509_BADCERT_CN_MISMATCH :
                    strcmp(mode, "untrusted") == 0 ? MBEDTLS_X509_BADCERT_NOT_TRUSTED :
                    strcmp(mode, "expired") == 0 ? MBEDTLS_X509_BADCERT_EXPIRED : MBEDTLS_X509_BADCERT_FUTURE;
                expect(observed.status == TLS_ERROR && (observed.verify_flags & flag) != 0,
                       "specific authentication failure retained");
            }
            if (timeout_case) expect(observed.status == TLS_TIMEOUT && elapsed >= 630 && elapsed <= 900, "one total deadline within tolerance");
            if (cancel_case) expect(observed.status == TLS_CANCELLED && elapsed <= 750, "bounded cancellation");
            if (strcmp(mode, "no-time") == 0) expect(observed.status == TLS_TIME_UNAVAILABLE, "time fails closed");
            if (strcmp(mode, "no-entropy") == 0) expect(observed.status == TLS_ERROR && observed.stage == TLS_STAGE_RANDOM, "entropy fails closed");
            if (strcmp(mode, "allocation") == 0) expect(observed.status == TLS_ERROR && observed.code == MBEDTLS_ERR_SSL_ALLOC_FAILED, "allocation diagnostic");
            if (strcmp(mode, "truncated") == 0 || strcmp(mode, "close-truncated") == 0)
                expect(observed.status == TLS_TRUNCATED, "unclean TLS EOF is not HTTP success");
            int before = platform.closes;
            int64_t close_begin = now_ms();
            connection.close_session();
            connection.close_session();
            expect(now_ms() - close_begin < 250 && platform.closes == before, "bounded idempotent cleanup");
            expect(connection.open("tls.lab.test", port) < 0, "no session restart");
            uint8_t byte = 0xA5;
            expect(connection.read(&byte, 1) < 0 && byte == 0xA5, "closed read cannot publish data");
            expect(connection.write(&byte, 1) < 0, "closed write cannot replay data");
            tls_stream_diagnostics retained = connection.diagnostic();
            expect(retained.status == observed.status && retained.stage == observed.stage &&
                   retained.code == observed.code && retained.verify_flags == observed.verify_flags,
                   "cleanup and rejected operations preserve causal diagnostics");
            opens = platform.opens; closes = platform.closes;
            expect(closes == opens && opens <= 1, "one transport close per attempted open");
        }
        if (canceller.joinable()) canceller.join();
    }
    expect(descriptors() == initial_descriptors, "descriptor balance");
    expect(live_allocations == initial_allocations, "scoped allocation balance");
    printf("CASE %s status=%d stage=%d flags=%u elapsed_ms=%d opens=%d closes=%d allocations=%d\n",
           mode, observed.status, observed.stage, observed.verify_flags, (int)elapsed, opens, closes, live_allocations);
}

struct TargetProvider : HttpSecureProvider {
    tls_stream_port callbacks;
    uint32_t timeout;
    bool unavailable;
    int created, destroyed;
    tls_stream_diagnostics last;
    TargetProvider(const tls_stream_port& port, uint32_t budget, bool missing) :
        callbacks(port), timeout(budget), unavailable(missing), created(0), destroyed(0), last{} {}
    class Connection : public TlsHttpConnection {
        TargetProvider& owner;
    public:
        Connection(TargetProvider& p) : TlsHttpConnection(p.callbacks, p.timeout), owner(p) {}
        ~Connection() { owner.last = diagnostic(); ++owner.destroyed; }
    };
    HttpConnection *create_connection(const OperationCancellation& token) {
        static_cast<Platform *>(callbacks.context)->command_cancellation = token;
        ++created;
        return unavailable ? NULL : new Connection(*this);
    }
};

static void routed_exchange(const char *mode, const char *ca, int port)
{
    int initial_allocations = live_allocations, initial_descriptors = descriptors();
    {
        Platform platform(ca);
        CancellationSource command_events;
        OperationCancellation command_token = command_events.token();
        bool command_cancel = strncmp(mode, "command-", 8) == 0;
        bool command_pre_cancel = strcmp(mode, "command-pre-cancel") == 0;
        platform.ready = strcmp(mode, "no-time") != 0;
        platform.random_ready = strcmp(mode, "no-entropy") != 0;
        if (strcmp(mode, "pre-cancel") == 0) platform.cancelled.store(true);
        if (strcmp(mode, "total-timeout") == 0) platform.open_delay = 250;
        bool timeout = strstr(mode, "timeout") != NULL;
        bool cancel = strstr(mode, "cancel") != NULL;
        bool success = strcmp(mode, "valid") == 0 || strcmp(mode, "recovery") == 0 || strcmp(mode, "close-clean") == 0;
        bool no_provider = strcmp(mode, "no-provider") == 0;
        bool no_create = strcmp(mode, "no-create") == 0;
        tls_stream_port callbacks = { &platform, clock_read, clock_ready, cancelled,
            entropy, trust, open_socket, read_socket, write_socket, close_socket };
        TargetProvider provider(callbacks, timeout ? 650 : 3000, no_create);
        std::thread canceller;
        if (cancel && strcmp(mode, "pre-cancel") && !command_pre_cancel)
            canceller = std::thread([&platform, &command_events, command_cancel] {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if (command_cancel) command_events.signal(); else platform.cancelled.store(true);
            });
        int64_t begin = now_ms();
        {
            HttpTarget target(7, no_provider ? NULL : &provider);
            target.begin_command(command_token);
            if (command_pre_cancel) command_events.signal();
            char url[128];
            snprintf(url, sizeof(url), "https://%s:%d/probe", strcmp(mode, "hostname") == 0 ? "wrong.lab.test" : "tls.lab.test", port);
            std::vector<uint8_t> create = {6, HTTP_CMD_HEADER_CREATE, 1};
            create.insert(create.end(), url, url + strlen(url));
            Message command = {int(create.size()), true, create.data()}, *data, *status;
            target.parse_command(&command, &data, &status);
            expect(data->length == 1, "routed header handle");
            uint8_t handle = data->length ? data->message[0] : 0;
            const char *line = "Connection: close";
            std::vector<uint8_t> add = {6, HTTP_CMD_HEADER_ADD, handle};
            add.insert(add.end(), line, line + strlen(line));
            command = {int(add.size()), true, add.data()};
            target.parse_command(&command, &data, &status);
            uint8_t exchange[] = {6, HTTP_CMD_DO_EXCHANGE_RAW, handle, 255};
            command = {sizeof(exchange), true, exchange};
            target.parse_command(&command, &data, &status);
            if (success) {
                expect(data->last_part && data->length == 8 && !memcmp(data->message, "tls-ok!\n", 8), "routed exact response");
                expect(status->length >= 15 && !memcmp(status->message, "HTTP/1.1 200 OK", 15), "routed remote status");
            } else {
                expect(data->last_part && data->length == 0, "routed failure has no body");
                expect(status->length == 23 && !memcmp(status->message, "503 SERVICE UNAVAILABLE", 23), "routed local failure status");
            }
            target.get_more_data(&data, &status);
            expect(data->length == 0 && data->last_part, "routed failure leaves no stale bytes");
        }
        int64_t elapsed = now_ms() - begin;
        if (canceller.joinable()) canceller.join();
        expect(provider.created == (no_provider || command_pre_cancel ? 0 : 1) && provider.destroyed == (no_provider || no_create || command_pre_cancel ? 0 : 1), "routed provider ownership");
        expect(platform.opens == platform.closes && platform.opens <= 1, "routed transport cleanup");
        if (no_provider || no_create || command_pre_cancel) expect(platform.opens == 0, "unavailable or cancelled command opens no TLS transport");
        if (timeout) expect(provider.last.status == TLS_TIMEOUT && elapsed >= 630 && elapsed <= 900, "routed original deadline");
        if (cancel) expect((command_pre_cancel || provider.last.status == TLS_CANCELLED) && elapsed <= 750, "routed cancellation");
        if (success) expect(provider.last.status == TLS_OK, "routed success diagnostics");
        if (strcmp(mode, "close-truncated") == 0)
            expect(provider.last.status == TLS_TRUNCATED, "routed unclean EOF remains a failure");
        printf("ROUTE %s status=%d flags=%u elapsed_ms=%d opens=%d closes=%d\n", mode,
               provider.last.status, provider.last.verify_flags, int(elapsed), platform.opens, platform.closes);
    }
    expect(descriptors() == initial_descriptors, "routed descriptor balance");
    expect(live_allocations == initial_allocations, "routed scoped allocation balance");
}

int main(int argc, char **argv)
{
    if (argc == 7 && strcmp(argv[6], "--routing") == 0) {
        routed_exchange(argv[1], argv[2], atoi(argv[3]));
        routed_exchange("recovery", argv[4], atoi(argv[5]));
        printf("TLS routing: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (argc != 6) return 2;
    exchange(argv[1], argv[2], atoi(argv[3]));
    exchange("recovery", argv[4], atoi(argv[5]));
    printf("TLS adapter: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

extern "C" void outbyte(int byte) { fputc(byte, stdout); }
