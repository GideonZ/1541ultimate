// The HTTP server's response body (HTTPRespMessage.BodyCB / BodyContext) when the
// client goes away before the reply is fully sent, over real loopback sockets:
//   - the body context must be released (BodyCB(ctx, NULL, -1)), not leaked
//   - the next client on the same connection slot must not be sent the previous
//     client's unsent body
//   - a static file served as a body must be closed
// The body callback follows the firmware's stream_body: it owns its context,
// frees it itself at end of data and, called with len < 0, frees it and stops.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server.h"
#include "middleware.h"

extern uint8_t _ReadStaticFiles(HTTPReqMessage *req, HTTPRespMessage *res);
extern void _NotFound(HTTPReqMessage *req, HTTPRespMessage *res);

static HTTPServer srv;
static int failures = 0;
static int live_bodies = 0;

typedef struct { int remaining; } Body;

static int body_cb(void *context, uint8_t *buf, int len)
{
    Body *b = (Body *)context;
    if (len < 0) { // the connection went away
        free(b);
        live_bodies--;
        return 0;
    }
    int n = b->remaining < len ? b->remaining : len;
    memset(buf, 'B', n);
    b->remaining -= n;
    if (!n) {
        free(b);
        live_bodies--;
    }
    return n;
}

static void dispatch(HTTPReqMessage *req, HTTPRespMessage *res)
{
    if (!strncmp(req->Header.URI, "/body", 5)) {
        const char *hdr = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
        Body *b = malloc(sizeof(Body));
        b->remaining = 4 * 1024 * 1024; // far more than the socket buffers take
        live_bodies++;
        memcpy(res->_buf, hdr, strlen(hdr));
        res->_index = strlen(hdr);
        res->BodyCB = body_cb;
        res->BodyContext = b;
    } else if (!strncmp(req->Header.URI, "/static", 7)) {
        // /static/big.bin -> STATIC_FILE_FOLDER/big.bin
        req->Header.URI += 7;
        if (!_ReadStaticFiles(req, res)) _NotFound(req, res);
    } else {
        _NotFound(req, res);
    }
}

static const char NOT_FOUND[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";

static void check(int ok, const char *name, const char *detail)
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, *detail ? " - " : "", detail);
    if (!ok) failures++;
}

static int client_get(const char *path)
{
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(MHS_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(c, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); exit(2); }
    char req[256];
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    send(c, req, strlen(req), 0);
    return c;
}

static void pump(int n)
{
    while (n--) HTTPServerRun(&srv, dispatch);
}

// run the server until every connection slot is free again (bounded)
static int drain_slots(void)
{
    for (int i = 0; i < 200 && srv.available_connections < MAX_HTTP_CLIENT; i++) pump(1);
    return srv.available_connections == MAX_HTTP_CLIENT;
}

static int open_fds(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

// client asks for a big body, reads nothing, closes
static void abandon(const char *path)
{
    int c = client_get(path);
    pump(3);   // accept, read request, first write
    close(c);  // unread data in the client's buffer: the server's next send fails
}

static void t_body_released_when_client_goes_away(void)
{
    for (int i = 0; i < 3; i++) {
        abandon("/body");
        drain_slots();
    }
    char d[64];
    snprintf(d, sizeof d, "%d response bodies still allocated", live_bodies);
    check(live_bodies == 0 && srv.available_connections == MAX_HTTP_CLIENT, "response_body_released_on_client_abort", d);
}

static void t_no_stale_body_for_next_client(void)
{
    abandon("/body");
    drain_slots();
    // the next client lands on the slot the first one used
    int c = client_get("/nothing");
    pump(2);
    char got[8192];
    int total = 0, eof = 0;
    for (int i = 0; i < 30 && !eof && total < (int)sizeof got; i++) {
        if (srv.available_connections < MAX_HTTP_CLIENT) pump(1);
        int n = recv(c, got + total, sizeof got - total, MSG_DONTWAIT);
        if (n > 0) total += n;
        else if (n == 0) eof = 1;
        else if (srv.available_connections == MAX_HTTP_CLIENT) break;
    }
    close(c);
    drain_slots();
    char d[96];
    snprintf(d, sizeof d, "second client got %d bytes, a bare 404 is %d", total, (int)(sizeof NOT_FOUND - 1));
    check(total == (int)(sizeof NOT_FOUND - 1) && !memcmp(got, NOT_FOUND, total), "next_client_gets_only_its_own_reply", d);
}

static void t_static_file_closed_when_client_goes_away(void)
{
    int before = open_fds();
    abandon("/static/big.bin");
    drain_slots();
    char d[64];
    snprintf(d, sizeof d, "%d file descriptors held after", open_fds() - before);
    check(open_fds() == before, "static_file_closed_on_client_abort", d);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN); // send() to a dead peer: the server wants the error, not the signal
    alarm(60);                // a blocked select() must fail the run, not hang it
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s && head -c 2097152 /dev/zero > %s/big.bin", STATIC_FILE_FOLDER, STATIC_FILE_FOLDER);
    if (system(cmd)) return 2;
    HTTPServerInit(&srv, MHS_PORT);
    if (srv.sock < 0) { printf("no server socket\n"); return 2; }
    const char *only = argc > 1 ? argv[1] : "";
    int all = !*only;
    if (all || !strcmp(only, "body")) t_body_released_when_client_goes_away();
    if (all || !strcmp(only, "stale")) t_no_stale_body_for_next_client();
    if (all || !strcmp(only, "static")) t_static_file_closed_when_client_goes_away();
    HTTPServerClose(&srv);
    printf("httpd_body: %d failed\n", failures);
    return failures ? 1 : 0;
}
