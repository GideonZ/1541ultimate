/* Portable regression gate for the production HTTPD response parser. */
#include "server.h"
#include <assert.h>
#include <string.h>

typedef struct { unsigned ended; size_t length; char bytes[128]; } Capture;

static int absorb(void *context, const uint8_t *data, int size)
{
    Capture *c = context;
    assert(size >= 0 && !c->ended);
    if (!size) { c->ended++; return 0; }
    assert(data && c->length + (size_t)size <= sizeof(c->bytes));
    memcpy(c->bytes + c->length, data, (size_t)size);
    c->length += (size_t)size;
    return size;
}

static void collect(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    (void)resp;
    req->BodyContext = req->userContext;
    req->BodyCB = absorb;
}

static void check(const char *wire, int valid, unsigned fragment)
{
    HTTPReqMessage req;
    Capture capture = {0};
    memset(&req, 0, sizeof req);
    InitReqMessage(&req);
    req.usedAsResponseFromServer = HTTP_RESPONSE_STRICT;
    req.userContext = &capture;
    unsigned state = READING_SOCKET;
    size_t left = strlen(wire);
    while (left && state == READING_SOCKET) {
        size_t n = left < fragment ? left : fragment;
        assert(req._valid >= 0 && req._valid + (int)n <= HTTP_BUFFER_SIZE);
        memcpy(req._buf + req._valid, wire, n);
        req._valid += (int)n;
        wire += n;
        left -= n;
        state = ProcessClientData(&req, NULL, collect);
    }
    int completed = state >= WRITING_SOCKET &&
                    req.protocol_state != eReq_HeaderTooBig && !req.BodyCB && capture.ended == 1;
    assert(completed == valid);
    if (valid) assert(capture.length == 3 && !memcmp(capture.bytes, "abc", 3));
}

int main(void)
{
    static const char *valid[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length:\t003 \t\r\n\r\nabc",
        "HTTP/1.0 404 Missing\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n1\r\na\r\n2\r\nbc\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;ext=\"a\\\"b\"\r\nabc\r\n0\r\nX: yes\r\n\r\n",
    };
    static const char *invalid[] = {
        "INVALID 200 OK\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0x3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: 3x\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab",
        "HTTP/1.1 200 OK\r\nMalformed\r\nContent-Length: 3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: notchunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\n3\r\nabc\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3x\r\nabc\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffffffffffff\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcBAD\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\nX: yes\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\nContent-Length: 3\r\n\r\n",
    };
    unsigned count = 0;
    for (unsigned fragment = 1; fragment <= 96; ++fragment) {
        for (unsigned i = 0; i < sizeof(valid)/sizeof(*valid); ++i) { check(valid[i], 1, fragment); ++count; }
        for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) { check(invalid[i], 0, fragment); ++count; }
    }
    printf("%u response framing/fragmentation checks passed.\n", count);
    return 0;
}
