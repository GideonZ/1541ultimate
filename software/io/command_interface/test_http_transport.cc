#include "http_request.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void expect(bool condition, const char *label)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL transport: %s\n", label);
    }
}

// A deterministic byte stream, not a TLS emulator. Tiny fragments exercise the
// HTTP parser and writer independently of sockets or a particular TLS library.
class FragmentConnection : public HttpConnection
{
public:
    const char *response;
    int offset, read_count, write_count, open_count, close_count;
    int fragment, write_limit, fail_after, open_result;
    bool read_error, oversized_write;
    char sent[1024];
    int sent_length;

    explicit FragmentConnection(const char *text) : response(text), offset(0),
        read_count(0), write_count(0), open_count(0), close_count(0), fragment(3),
        write_limit(7), fail_after(-1), open_result(0), read_error(false),
        oversized_write(false), sent_length(0) {}

    int open(const char *host, uint16_t port)
    {
        open_count++;
        expect(strcmp(host, "fixture.invalid") == 0 && port == 443, "backend receives host and port");
        return open_result;
    }
    int read(void *buffer, int length)
    {
        read_count++;
        if (read_error) {
            errno = EINTR; // Backend errors must not be retried using stale socket errno.
            return -1;
        }
        int n = strlen(response) - offset;
        if (n > fragment) n = fragment;
        if (n > length) n = length;
        memcpy(buffer, response + offset, n);
        offset += n;
        return n;
    }
    int write(const void *buffer, int length)
    {
        write_count++;
        if (fail_after >= 0 && sent_length >= fail_after) return -1;
        if (oversized_write) return length + 1;
        if (write_limit <= 0) return write_limit;
        int n = length < write_limit ? length : write_limit;
        if (n > (int)sizeof(sent) - sent_length) return -1;
        memcpy(sent + sent_length, buffer, n);
        sent_length += n;
        return n;
    }
    void close_session() { close_count++; }
};

static void test_fragments(void)
{
    const char response[] = "HTTP/1.1 201 Created\r\nContent-Length: 5\r\n\r\nhello";
    FragmentConnection backend(response);
    {
        HttpRequest request(&backend);
        expect(request.connect_to_server("fixture.invalid", 443) == 0, "open injected backend");
        expect(request.connect_to_server("fixture.invalid", 443) < 0 && backend.open_count == 1,
               "reject second open without replacing the live session");
        char bytes[300];
        for (int i = 0; i < (int)sizeof(bytes); ++i) bytes[i] = (char)i;
        StreamRamFile payload(64);
        payload.write((uint8_t *)bytes, sizeof(bytes));
        expect(request.send_request(&payload) == 0, "short writes complete");
        expect(backend.sent_length == sizeof(bytes) && memcmp(bytes, backend.sent, sizeof(bytes)) == 0,
               "short writes preserve every byte across request blocks");
        expect(request.recv_response(), "fragmented status, headers and body parse");
        char received[8] = {};
        expect(request.read_response_data(2, (uint8_t *)received) == 2, "first body fragment");
        expect(request.read_response_data(6, (uint8_t *)received + 2) == 3, "remaining body fragment");
        expect(memcmp(received, "hello", 5) == 0, "response body is unchanged");
        expect(request.read_response_data(1, (uint8_t *)received) == 0, "body exhausted");
        expect(!request.recv_response(), "reject repeated parsing of an owned response");
        expect(backend.close_count == 0, "session lives until request destruction");
    }
    expect(backend.close_count == 1, "destroy closes borrowed backend session exactly once");
}

static void test_failures(void)
{
    const char *responses[] = { "", "HTTP/1.1 200 OK\r\nContent-Len",
                               "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab" };
    for (unsigned i = 0; i < sizeof(responses) / sizeof(responses[0]); ++i) {
        FragmentConnection backend(responses[i]);
        {
            HttpRequest request(&backend);
            request.connect_to_server("fixture.invalid", 443);
            expect(!request.recv_response(), "reject incomplete response");
            expect(backend.close_count == 1, "read failure closes immediately");
            expect(!request.recv_response(), "failed parser cannot read again");
        }
        expect(backend.close_count == 1, "read failure is not closed twice");
    }
    FragmentConnection refused("");
    refused.open_result = -1;
    {
        HttpRequest request(&refused);
        expect(request.connect_to_server("fixture.invalid", 443) < 0, "propagate open failure");
        expect(refused.close_count == 1, "release partially opened backend");
        expect(!request.recv_response() && refused.read_count == 0, "no read after open failure");
        expect(request.connect_to_server("fixture.invalid", 443) < 0 && refused.open_count == 1,
               "no implicit retry after failed open");
    }
    expect(refused.close_count == 1, "open failure is not closed twice");

    FragmentConnection error("");
    error.read_error = true;
    {
        HttpRequest request(&error);
        request.connect_to_server("fixture.invalid", 443);
        expect(!request.recv_response() && error.read_count == 1, "terminal backend error ignores errno");
    }
    expect(error.close_count == 1, "read error cleanup");

    for (int mode = 0; mode < 4; ++mode) {
        FragmentConnection backend("");
        backend.write_limit = mode == 0 ? 0 : mode == 1 ? -1 : 7;
        backend.oversized_write = mode == 2;
        backend.fail_after = mode == 3 ? 7 : -1;
        {
            HttpRequest request(&backend);
            request.connect_to_server("fixture.invalid", 443);
            StreamRamFile payload(64);
            payload.format("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata");
            expect(request.send_request(&payload) < 0, "reject zero, error, oversized or partial failed write");
            expect(backend.close_count == 1, "write failure closes immediately");
            int calls = backend.write_count;
            payload.reset_read();
            expect(request.send_request(&payload) < 0 && backend.write_count == calls,
                   "failed write cannot be replayed on this session");
            if (mode == 3) expect(backend.sent_length == 7, "preserve partial-delivery evidence");
        }
        expect(backend.close_count == 1, "write failure is not closed twice");
    }
    FragmentConnection unused("");
    { HttpRequest request(&unused); }
    expect(unused.close_count == 0, "unused request does not close another owner's backend");
}

static void test_isolation(void)
{
    FragmentConnection first("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"), second("");
    HttpRequest live(&first);
    live.connect_to_server("fixture.invalid", 443);
    {
        HttpRequest failed(&second);
        second.open_result = -1;
        failed.connect_to_server("fixture.invalid", 443);
    }
    expect(second.close_count == 1 && first.close_count == 0, "sessions have independent ownership");
    expect(live.recv_response(), "one failure does not break another request");
    JSON *json = live.get_json();
    expect(json != NULL, "JSON response still uses existing parser");
    delete json;
}

static void test_borrowed_socket(void)
{
    int sockets[2];
    int result = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    expect(result == 0, "prepare borrowed socket fixture");
    if (result != 0) return;
    const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    expect(send(sockets[0], response, sizeof(response) - 1, 0) == sizeof(response) - 1,
           "send legacy parser fixture");
    shutdown(sockets[0], SHUT_WR);
    HTTPReqMessage message;
    InitReqMessage(&message);
    message.usedAsResponseFromServer = 1;
    expect(get_response(sockets[1], collect_in_buffer, message), "legacy socket parser completes");
    expect(fcntl(sockets[1], F_GETFD) >= 0, "legacy parser does not close caller's descriptor");
    t_BufferedBody *body = (t_BufferedBody *)message.userContext;
    expect(body && body->size == 2 && memcmp(body->buffer, "{}", 2) == 0,
           "legacy callback receives exact response");
    delete body;
    close(sockets[0]);
    close(sockets[1]);
}

int test_http_transport(void)
{
    test_fragments();
    test_failures();
    test_isolation();
    test_borrowed_socket();
    printf("HTTP transport: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
