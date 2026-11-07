// The UCI network target (target 3) on its worker task, against real loopback sockets.
// The test plays the UCI Server task: it runs each command as the server does, including
// the reply to a deferred one, and an abort or a C64 reset as the server calls them.
//
// RED before the worker: a WRITE to a peer that stops reading blocks the caller, which
// on the device is the UCI Server task, so a C64 reset waits with it.
#include "network_target.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

CommandTarget *command_targets[CMD_IF_MAX_TARGET + 1];
Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" };
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };
extern "C" void outbyte(int c) { putchar(c); }

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

// --- the UCI Server's side of command_defer ----------------------------------------------
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool deferred = false;
static uint32_t last_ticket = 0;
static bool reported[100000];

uint32_t command_defer(void) { deferred = true; return ++last_ticket; }

void command_deferred_done(uint32_t ticket)
{
    pthread_mutex_lock(&lock);
    reported[ticket % 100000] = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static bool wait_done(uint32_t ticket, int ms)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&lock);
    while (!reported[ticket % 100000] && pthread_cond_timedwait(&changed, &lock, &deadline) == 0) { }
    bool done = reported[ticket % 100000];
    pthread_mutex_unlock(&lock);
    return done;
}

// Through the base class, as the UCI Server calls it (and as an older target still compiles).
static CommandTarget *target(void) { return command_targets[3]; }

// A command as the server runs it: parse_command, then, for a deferred one, the reply
// once the worker reports it. Returns false when the reply did not come in time.
static bool run(const uint8_t *bytes, int length, Message **reply, Message **status, int ms = 5000)
{
    static uint8_t buffer[CMD_MAX_COMMAND_LEN + 1];
    memcpy(buffer, bytes, length);
    Message command = { length, true, buffer };
    deferred = false;
    target()->parse_command(&command, reply, status);
    if (!deferred) {
        return true;
    }
    if (!wait_done(last_ticket, ms)) {
        return false;
    }
    target()->deferred_reply(reply, status);
    return true;
}

static bool status_is(Message *status, const char *text)
{
    return status->length == (int)strlen(text) && !memcmp(status->message, text, status->length);
}

// --- loopback peers -------------------------------------------------------------------
struct Peer {
    int listener, client;
    uint16_t port;
    bool reads;              // false: take the connection and never read
    volatile long eof_ms;    // when a reading peer saw the end of the stream
    pthread_t thread;
};

static void *peer_run(void *a)
{
    Peer *p = (Peer *)a;
    p->client = accept(p->listener, NULL, NULL);
    if (p->reads) {
        char b[4096];
        while (p->client >= 0 && recv(p->client, b, sizeof(b), 0) > 0) { }
        p->eof_ms = now_ms();
    }
    return NULL;
}

static void peer_start(Peer *p, int type, bool reads)
{
    memset(p, 0, sizeof(*p));
    p->reads = reads;
    p->client = -1;
    p->listener = ::socket(AF_INET, type, 0);
    int small = 4096;
    setsockopt(p->listener, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    bind(p->listener, (struct sockaddr *)&a, len);
    getsockname(p->listener, (struct sockaddr *)&a, &len);
    p->port = ntohs(a.sin_port);
    if (type == SOCK_STREAM) {
        listen(p->listener, 1);
        pthread_create(&p->thread, NULL, peer_run, p);
    }
}

static void peer_stop(Peer *p)
{
    if (p->client >= 0) {
        shutdown(p->client, SHUT_RDWR);
    }
    if (p->thread) {
        pthread_join(p->thread, NULL);
    }
    if (p->client >= 0) {
        close(p->client);
    }
    close(p->listener);
}

static int open_command(uint8_t *cmd, uint8_t which, uint16_t port)
{
    cmd[0] = 3; cmd[1] = which; cmd[2] = port & 0xFF; cmd[3] = port >> 8;
    strcpy((char *)cmd + 4, "127.0.0.1");
    return 4 + 9;
}

static int open_socket_to(uint8_t which, uint16_t port)
{
    uint8_t cmd[32];
    Message *reply, *status;
    int length = open_command(cmd, which, port);
    if (!run(cmd, length, &reply, &status) || !status_is(status, "00,OK") || reply->length != 1) {
        return -1;
    }
    return reply->message[0];
}

// --- the cases --------------------------------------------------------------------------

// The replies of the commands, byte for byte, as before the worker.
static void test_replies(void)
{
    Message *reply, *status;
    const uint8_t identify[] = { 3, 0x01 };
    CHECK(run(identify, 2, &reply, &status), "identify: no reply");
    CHECK(reply->length == 34 && !memcmp(reply->message, "ULTIMATE-II NETWORK INTERFACE V1.0", 34), "identify reply");
    const uint8_t ip[] = { 3, 0x05, 0 };
    CHECK(run(ip, 3, &reply, &status) && reply->length == 12 && reply->message[0] == 10, "get ip address");

    // A UDP peer, which does not answer; the target's socket sends to it and reads what
    // the peer sends back on its own socket.
    Peer peer;
    peer_start(&peer, SOCK_DGRAM, false);
    int s = open_socket_to(0x08, peer.port);
    CHECK(s >= 0, "open udp");
    uint8_t write[3 + 800] = { 3, 0x11, (uint8_t)s };
    memset(write + 3, 'w', 800);
    CHECK(run(write, sizeof(write), &reply, &status) && status_is(status, "00,OK") &&
          reply->length == 2 && reply->message[0] == (800 & 0xFF) && reply->message[1] == (800 >> 8), "write udp");
    char got[1500];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(peer.listener, got, sizeof(got), 0, (struct sockaddr *)&from, &fl);
    CHECK(n == 800, "the peer got %d bytes", n);
    // a datagram of 1472 bytes, which takes two reply blocks
    char datagram[1472];
    for (int i = 0; i < 1472; i++) datagram[i] = (char)i;
    sendto(peer.listener, datagram, sizeof(datagram), 0, (struct sockaddr *)&from, fl);
    usleep(20000);
    const uint8_t read[] = { 3, 0x10, (uint8_t)s, 1472 & 0xFF, 1472 >> 8 };
    CHECK(run(read, 5, &reply, &status), "read: no reply");
    CHECK(reply->length == 895 && !reply->last_part && reply->message[0] == (1472 & 0xFF) &&
          reply->message[1] == (1472 >> 8) && !memcmp(reply->message + 2, datagram, 893), "first read block");
    CHECK(status->length == 0, "no status before the last block");
    target()->get_more_data(&reply, &status);
    CHECK(reply->length == 1472 - 893 && reply->last_part && !memcmp(reply->message, datagram + 893, 1472 - 893),
          "second read block");
    CHECK(status_is(status, "00,OK"), "read status");
    const uint8_t close_cmd[] = { 3, 0x09, (uint8_t)s };
    CHECK(run(close_cmd, 3, &reply, &status) && status_is(status, "00,OK"), "close");
    CHECK(run(close_cmd, 3, &reply, &status) && status_is(status, "12,ERROR ON CLOSE: 9"), "close twice");
    peer_stop(&peer);
}

struct Blocked {
    uint8_t cmd[3 + 880];
    Message *reply, *status;
    volatile bool returned;
};

static void *parse_in_thread(void *a)
{
    Blocked *b = (Blocked *)a;
    Message command = { (int)sizeof(b->cmd), true, b->cmd };
    deferred = false;
    target()->parse_command(&command, &b->reply, &b->status);
    b->returned = true;
    return NULL;
}

// WRITEs to a peer that stops reading until one stalls. Returns false when the stalled
// one blocked the caller (the UCI Server task) instead of waiting on the worker.
static bool stall_a_write(Peer *peer, int s, Blocked *b, pthread_t *thread)
{
    b->cmd[0] = 3; b->cmd[1] = 0x11; b->cmd[2] = (uint8_t)s;
    memset(b->cmd + 3, 'x', 880);
    for (int i = 0; i < 20000; i++) {
        b->returned = false;
        pthread_create(thread, NULL, parse_in_thread, b);
        long start = now_ms();
        while (!b->returned && now_ms() - start < 1000) {
            usleep(1000);
        }
        if (!b->returned) {
            return false;   // the caller is stuck in send()
        }
        pthread_join(*thread, NULL);
        if (deferred && !wait_done(last_ticket, 1000)) {
            return true;    // the worker waits on the peer; the caller does not
        }
        if (deferred) {
            target()->deferred_reply(&b->reply, &b->status);
        }
    }
    (void)peer;
    return false;
}

static void stalled_write(const char *label, bool reset)
{
    Peer peer;
    peer_start(&peer, SOCK_STREAM, false);
    int s = open_socket_to(0x07, peer.port);
    CHECK(s >= 0, "%s: open tcp", label);
    Blocked b;
    pthread_t thread;
    bool on_worker = stall_a_write(&peer, s, &b, &thread);
    CHECK(on_worker, "%s: a stalled WRITE blocks the UCI Server task", label);
    if (!on_worker) {
        peer_stop(&peer);      // the send fails, the caller comes back
        pthread_join(thread, NULL);
        target()->c64_reset();
        return;
    }
    uint32_t stalled = last_ticket;
    long start = now_ms();
    if (reset) {
        target()->c64_reset();
    }
    target()->abort(0);
    long took = now_ms() - start;
    CHECK(took < 20, "%s: abort/reset took %ld ms", label, took);

    Message *reply, *status;
    const uint8_t identify[] = { 3, 0x01 };
    bool answered = run(identify, 2, &reply, &status, 2000);
    took = now_ms() - start;
    CHECK(answered && took < 600, "%s: the next command answered after %ld ms", label, took);
    printf("info %s: next command after %ld ms\n", label, took);
    CHECK(!reported[stalled % 100000], "%s: the aborted WRITE reported a result", label);
    if (reset) {
        // The socket of the program before the reset is gone from the table.
        uint8_t write[] = { 3, 0x11, (uint8_t)s, 'x' };
        CHECK(run(write, sizeof(write), &reply, &status) && status_is(status, "12,SEND ERROR: 9"),
              "%s: the old socket survived the reset", label);
    }
    peer_stop(&peer);
    target()->c64_reset();
    run(identify, 2, &reply, &status);
}

// A C64 reset closes the target's sockets, on the worker: the peer sees the end.
static void test_reset_closes(void)
{
    Peer peer;
    peer_start(&peer, SOCK_STREAM, true);
    int s = open_socket_to(0x07, peer.port);
    CHECK(s >= 0, "reset closes: open tcp");
    usleep(50000);
    long start = now_ms();
    target()->c64_reset();
    while (!peer.eof_ms && now_ms() - start < 2000) {
        usleep(1000);
    }
    long took = peer.eof_ms ? peer.eof_ms - start : -1;
    CHECK(took >= 0 && took < 500, "reset closes: the peer saw the end after %ld ms (-1: not in 2 s)", took);
    peer_stop(&peer);
}

// "a READ of 769-1023 bytes can hang the Ultimate" (a game's notes, from fw 3.15). The
// gateware's response queue holds 896 bytes, and a block of exactly 896 never ends: the
// pointer stops on the last byte while DATA_AV stays set, so a client that reads until
// DATA_AV clears never stops. Every requested length must give blocks of at most 895 and
// hand over exactly the datagram, with the count in front.
static void test_read_lengths(void)
{
    static const int lengths[] = { 512, 768, 769, 893, 894, 895, 896, 1023, 1024, 1472 };
    Peer peer;
    peer_start(&peer, SOCK_DGRAM, false);
    int s = open_socket_to(0x08, peer.port);
    CHECK(s >= 0, "read lengths: open udp");
    Message *reply, *status;
    uint8_t hello[] = { 3, 0x11, (uint8_t)s, 'h' };
    run(hello, sizeof(hello), &reply, &status);
    char got[64];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    recvfrom(peer.listener, got, sizeof(got), 0, (struct sockaddr *)&from, &fl);
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        int length = lengths[i];
        static uint8_t datagram[1472];
        for (int j = 0; j < length; j++) datagram[j] = (uint8_t)(j * 7 + length);
        sendto(peer.listener, datagram, length, 0, (struct sockaddr *)&from, fl);
        usleep(20000);
        uint8_t read[] = { 3, 0x10, (uint8_t)s, (uint8_t)length, (uint8_t)(length >> 8) };
        CHECK(run(read, sizeof(read), &reply, &status), "read %d: no reply", length);
        static uint8_t all[2 + 1472 + 900];
        int total = 0, blocks = 0, biggest = 0;
        while (true) {
            blocks++;
            if (reply->length > biggest) biggest = reply->length;
            if (total + reply->length <= (int)sizeof(all)) {
                memcpy(all + total, reply->message, reply->length);
            }
            total += reply->length;
            if (reply->last_part || blocks > 4) {
                break;
            }
            target()->get_more_data(&reply, &status);
        }
        CHECK(biggest <= 895, "read %d: a block of %d bytes, which never ends on the C64", length, biggest);
        CHECK(blocks <= 2, "read %d: %d blocks", length, blocks);
        CHECK(status_is(status, "00,OK"), "read %d: status %.*s", length, status->length, status->message);
        CHECK(total == length + 2 && all[0] == (length & 0xFF) && all[1] == (length >> 8) &&
              !memcmp(all + 2, datagram, length), "read %d: got %d bytes", length, total);
    }
    const uint8_t close_cmd[] = { 3, 0x09, (uint8_t)s };
    run(close_cmd, 3, &reply, &status);
    peer_stop(&peer);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    const char *only = argc > 1 ? argv[1] : NULL;
    if (!only || !strcmp(only, "replies")) test_replies();
    if (!only || !strcmp(only, "abort")) stalled_write("abort during a stalled WRITE", false);
    if (!only || !strcmp(only, "reset")) stalled_write("reset during a stalled WRITE", true);
    if (!only || !strcmp(only, "reset-closes")) test_reset_closes();
    if (!only || !strcmp(only, "read-lengths")) test_read_lengths();
    printf("Network target: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
