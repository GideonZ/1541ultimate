#include "https_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[8];
    size_t size, cursor;
    int code, opens, closes, reads, step, read_error;
    int64_t time, declared;
    bool finished, cancel, cancel_on_read, overreport;
    https_status open_result;
    uint32_t previous_budget;
} fake;

static int64_t now(void *p) { return ((fake *)p)->time; }
static bool cancelled(void *p) { return ((fake *)p)->cancel; }
static https_status open_request(void *p, const https_request *r, uint32_t ms, int *code, int64_t *length)
{
    fake *f = p;
    assert(!strcmp(r->host, "example.test"));
    assert(r->port == 443 && !strcmp(r->path, "/data"));
    f->opens++;
    f->time += f->step;
    f->previous_budget = ms;
    *code = f->code;
    *length = f->declared;
    return f->open_result;
}
static int read_body(void *p, uint8_t *buffer, size_t room, uint32_t ms)
{
    fake *f = p;
    assert(ms > 0 && ms <= f->previous_budget);
    f->previous_budget = ms;
    f->reads++;
    f->time += f->step;
    if (f->cancel_on_read) f->cancel = true;
    if (f->read_error) return f->read_error;
    if (f->overreport) return (int)room + 1;
    size_t n = f->size - f->cursor;
    if (n > 2) n = 2;
    if (n > room) n = room;
    memcpy(buffer, f->bytes + f->cursor, n);
    f->cursor += n;
    return (int)n;
}
static bool complete(void *p) { return ((fake *)p)->finished; }
static void end(void *p) { ((fake *)p)->closes++; }
static fake normal(void)
{
    return (fake){ .bytes = { 'a', 0, 'b', 255 }, .size = 4, .code = 200,
                   .declared = 4, .finished = true, .step = 1 };
}
static https_request request(fake *f)
{
    return (https_request){ .host = "example.test", .port = 443, .path = "/data",
        .method = "GET", .timeout_ms = 1000, .cancelled = cancelled, .cancel_context = f };
}
static https_status run(fake *f, https_request *r, size_t capacity)
{
    uint8_t buffer[HTTPS_MAX_RESPONSE_BODY + 1];
    memset(buffer, 0xa5, sizeof buffer);
    https_response out = { .body = buffer, .capacity = capacity, .length = 999 };
    https_transport transport = { f, now, open_request, read_body, complete, end };
    https_status result = https_fetch(r, &out, &transport);
    assert(f->opens == f->closes);
    assert(buffer[capacity] == 0xa5);
    if (result == HTTPS_OK) {
        assert(out.length == f->size);
        assert(!memcmp(buffer, f->bytes, out.length));
        assert(out.http_status == f->code);
    } else assert(out.length == 0);
    return result;
}

int main(void)
{
    fake f = normal();
    https_request r = request(&f);
    assert(run(&f, &r, 4) == HTTPS_OK); /* Exact fit, binary NUL preserved. */
    const int codes[] = {201, 207, 301, 400, 401, 404, 429, 500, 599};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        f = normal(); f.code = codes[i]; assert(run(&f, &r, 4) == HTTPS_OK);
        assert(f.opens == 1); /* Redirect responses do not trigger another request. */
    }
    f = normal(); f.declared = -1; assert(run(&f, &r, 4) == HTTPS_OK);
    f = normal(); f.declared = 5; assert(run(&f, &r, 4) == HTTPS_BODY_TOO_LARGE); assert(!f.reads);
    f = normal(); f.declared = -1; assert(run(&f, &r, 3) == HTTPS_BODY_TOO_LARGE);
    f = normal(); f.declared = 5; assert(run(&f, &r, 8) == HTTPS_TRUNCATED);
    f = normal(); f.declared = 3; assert(run(&f, &r, 8) == HTTPS_PROTOCOL_ERROR);
    f = normal(); f.finished = false; assert(run(&f, &r, 4) == HTTPS_TRUNCATED);
    f = normal(); f.read_error = -1; assert(run(&f, &r, 4) == HTTPS_TRANSPORT_ERROR);
    f = normal(); f.overreport = true; assert(run(&f, &r, 4) == HTTPS_PROTOCOL_ERROR);
    f = normal(); f.open_result = HTTPS_TLS_ERROR; assert(run(&f, &r, 4) == HTTPS_TLS_ERROR); assert(!f.reads);
    f = normal(); f.open_result = HTTPS_TIME_UNAVAILABLE; assert(run(&f, &r, 4) == HTTPS_TIME_UNAVAILABLE);
    f = normal(); f.cancel = true; assert(run(&f, &r, 4) == HTTPS_CANCELLED); assert(!f.opens);
    f = normal(); f.cancel_on_read = true; assert(run(&f, &r, 4) == HTTPS_CANCELLED);
    f = normal(); f.step = 1000; assert(run(&f, &r, 4) == HTTPS_TIMEOUT); assert(!f.reads);
    f = normal(); f.step = 300; assert(run(&f, &r, 4) == HTTPS_TIMEOUT);
    f = normal(); f.time = INT64_MAX - 10000; assert(run(&f, &r, 4) == HTTPS_OK);
    f = normal(); f.step = -1; assert(run(&f, &r, 4) == HTTPS_TIMEOUT);
    f = normal(); f.code = 101; assert(run(&f, &r, 4) == HTTPS_PROTOCOL_ERROR);
    f = normal(); f.declared = -2; assert(run(&f, &r, 4) == HTTPS_PROTOCOL_ERROR);
    f = normal(); f.code = 204; f.size = 0; f.declared = 0; assert(run(&f, &r, 0) == HTTPS_OK);
    f = normal(); f.code = 304; f.size = 0; f.declared = 9999; assert(run(&f, &r, 0) == HTTPS_OK);
    f = normal(); r.method = "HEAD"; f.size = 0; f.declared = 9999; assert(run(&f, &r, 0) == HTTPS_OK);
    f = normal(); assert(run(&f, &r, 4) == HTTPS_PROTOCOL_ERROR); r.method = "GET";

    const char *bad_hosts[] = {"", "https://example.test", "a@b", "a:443", "a/b", "a..b", "-a", "a-", "a.", "a\r\nb"};
    for (size_t i = 0; i < sizeof bad_hosts / sizeof bad_hosts[0]; ++i) {
        f = normal(); r.host = bad_hosts[i];
        assert(run(&f, &r, 4) == HTTPS_INVALID_ARGUMENT); assert(!f.opens);
    }
    r = request(&f);
    const char *bad_paths[] = {"", "https://example.test/", "/a b", "/a#b", "/a\\b", "/\r\nX: a"};
    for (size_t i = 0; i < sizeof bad_paths / sizeof bad_paths[0]; ++i) {
        r.path = bad_paths[i]; assert(!https_request_valid(&r));
    }
    r = request(&f);
    https_header h = {"Authorization", "Bearer caller-supplied"};
    r.headers = &h; r.header_count = 1;
    assert(https_request_valid(&r));
    const char *reserved[] = {"HOST", "Content-Length", "Transfer-Encoding", "Connection", "Upgrade", "Expect", "bad name"};
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; ++i) {
        h.name = reserved[i]; assert(!https_request_valid(&r));
    }
    h.name = "Accept"; h.value = "x\r\nInjected: yes"; assert(!https_request_valid(&r));
    h.value = NULL; assert(!https_request_valid(&r));
    r = request(&f); r.method = "CONNECT"; assert(!https_request_valid(&r));
    r.method = "GE T"; assert(!https_request_valid(&r));
    r = request(&f); r.body_length = 1; assert(!https_request_valid(&r));
    r = request(&f); r.timeout_ms = HTTPS_MAX_TIMEOUT_MS + 1; assert(!https_request_valid(&r));
    r = request(&f); r.header_count = HTTPS_MAX_HEADERS + 1; assert(!https_request_valid(&r));
    char path[HTTPS_MAX_PATH + 2]; memset(path, 'a', sizeof path); path[0] = '/'; path[sizeof path - 1] = 0;
    r = request(&f); r.path = path; assert(!https_request_valid(&r));
    path[HTTPS_MAX_PATH] = 0; assert(https_request_valid(&r));
    char value[HTTPS_MAX_HEADER_BYTES + 1];
    memset(value, 'v', sizeof value); value[sizeof value - 1] = 0;
    r = request(&f); h.name = "X"; h.value = value; r.headers = &h; r.header_count = 1;
    assert(!https_request_valid(&r));
    value[HTTPS_MAX_HEADER_BYTES - 5] = 0; assert(https_request_valid(&r));
    char host[65]; memset(host, 'a', sizeof host); host[64] = 0;
    r = request(&f); r.host = host; assert(!https_request_valid(&r));
    host[63] = 0; assert(https_request_valid(&r));
    uint8_t request_body[HTTPS_MAX_REQUEST_BODY] = {0};
    r = request(&f); r.method = "POST"; r.body = request_body;
    r.body_length = sizeof request_body; assert(https_request_valid(&r));
    r.body_length++; assert(!https_request_valid(&r));
    r = request(&f);
    https_transport transport = { &f, now, open_request, read_body, complete, end };
    https_response empty = {0};
    assert(https_fetch(&r, NULL, &transport) == HTTPS_INVALID_ARGUMENT);
    assert(https_fetch(&r, &empty, NULL) == HTTPS_INVALID_ARGUMENT);
    transport.end = NULL;
    assert(https_fetch(&r, &empty, &transport) == HTTPS_INVALID_ARGUMENT);
    assert(!https_request_valid(NULL));
    puts("HTTPS core: binary responses, HTTP statuses, limits, malformed input, cleanup, deadlines and cancellation passed.");
    return 0;
}
