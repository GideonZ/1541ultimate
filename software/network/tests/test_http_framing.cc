#include <string>
#include <vector>
#include <stdio.h>
#include "http_request.h"

static int checks, failures;
static void expect(bool ok, const char *label) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", label); } }
struct Fixture : HttpConnection {
    std::string bytes;
    size_t offset;
    int fragment, end, closes, reads;
    bool live;
    Fixture(const std::string& s, int part, int eof) : bytes(s), offset(0), fragment(part), end(eof), closes(0), reads(0), live(false) {}
    int open(const char *, uint16_t) { live = true; return 0; }
    int write(const void *, int n) { return n; }
    int read(void *data, int length) {
        ++reads;
        if (offset == bytes.size()) return end;
        int n = bytes.size() - offset;
        if (n > length) n = length;
        if (n > fragment) n = fragment;
        memcpy(data, bytes.data() + offset, n); offset += n;
        return n;
    }
    void close_session() { if (live) { live = false; ++closes; } }
};
struct Case { const char *name; std::string response, body; bool pass; bool head; int eof; bool no_eof; };
static std::string fixed(const std::string& body) { return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; }
int test_http_framing()
{
    std::vector<Case> cases = {
        {"fixed", fixed("hello"), "hello", true, false, -1, true},
        {"close-delimited", "HTTP/1.0 200 OK\r\n\r\nhello", "hello", true, false, 0, false},
        {"empty-close", "HTTP/1.1 200 OK\r\n\r\n", "", true, false, 0, false},
        {"failed-close", "HTTP/1.1 200 OK\r\n\r\nhello", "", false, false, -1, false},
        {"HEAD", "HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n", "", true, true, -1, true},
        {"204", "HTTP/1.1 204 No Content\r\n\r\n", "", true, false, -1, true},
        {"304", "HTTP/1.1 304 Not Modified\r\nContent-Length: 99\r\n\r\n", "", true, false, -1, true},
        {"interim", "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\n\r\n" + fixed("yes"), "yes", true, false, -1, true},
        {"interim-eof", "HTTP/1.1 100 Continue\r\n\r\n", "", false, false, 0, false},
        {"upgrade", "HTTP/1.1 101 Switching Protocols\r\n\r\n", "", false, false, 0, false},
        {"conflicting-lengths", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\ncontent-length: 2\r\n\r\nxx", "", false, false, 0, false},
        {"same-lengths", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\ncontent-length: 2\r\n\r\nxx", "xx", true, false, -1, true},
        {"TE-CL", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", "", false, false, 0, false},
        {"chunked", "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n2;foo=bar\r\nhi\r\n0\r\nX-Trailer: ok\r\n\r\n", "hi", true, false, -1, true},
        {"zero-truncated", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n", "", false, false, 0, false},
        {"chunk-separator", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxgarbage\r\n0\r\n\r\n", "", false, false, 0, false},
        {"forbidden-trailer", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n", "", false, false, 0, false},
        {"truncated-fixed", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nhi", "", false, false, 0, false},
        {"invalid-status", "NOTHTTP 200 OK\r\nContent-Length: 0\r\n\r\n", "", false, false, 0, false},
    };
    const std::string chunk_header = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    cases.push_back({"quoted-extension", chunk_header + "2 ; foo = \"a\\\"b\"; flag\r\nhi\r\n0\r\n\r\n", "hi", true, false, -1, true});
    cases.push_back({"decimal-zeros", "HTTP/1.1 200 OK\r\nContent-Length:\t0002 \t\r\n\r\nhi", "hi", true, false, -1, true});
    for (const char *field : {"Content-Length : 2", " Content-Length: 2", "X-Fold: yes\r\n folded", "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked"})
        cases.push_back({"malformed-field", std::string("HTTP/1.1 200 OK\r\n") + field + "\r\n\r\nhi", "", false, false, 0, false});
    std::string nul_header = "HTTP/1.1 200 OK\r\nX-Nul: a";
    nul_header.push_back(0);
    cases.push_back({"embedded-nul", nul_header + "b\r\n\r\nhi", "", false, false, 0, false});
    cases.push_back({"chunk-line-limit", chunk_header + "1;name=" + std::string(1024, 'x') + "\r\nx\r\n0\r\n\r\n", "", false, false, 0, false});
    std::string trailers;
    for (int i = 0; i < 230; ++i) trailers += "X-Test: v\r\n";
    cases.push_back({"trailer-limit", chunk_header + "0\r\n" + trailers + "\r\n", "", false, false, 0, false});
    cases.push_back({"unterminated-quote", chunk_header + "1;name=\"bad\r\nx\r\n0\r\n\r\n", "", false, false, 0, false});
    for (const char *value : {"-1", "+1", "0x10", "1x", "2147483648", "", "1,2"})
        cases.push_back({"invalid-length", std::string("HTTP/1.1 200 OK\r\nContent-Length: ") + value + "\r\n\r\nx", "", false, false, 0, false});
    for (const char *value : {"gzip", "notchunked", "chunked, gzip", "chunked, chunked"})
        cases.push_back({"unsupported-encoding", std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: ") + value + "\r\n\r\n0\r\n\r\n", "", false, false, 0, false});
    for (const char *value : {"-1", "+1", "xyz", "0x1", "1x", "80000000", "", "1;"})
        cases.push_back({"invalid-chunk", std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") + value + "\r\nx\r\n0\r\n\r\n", "", false, false, 0, false});
    for (int fields : {20, 21, 30, 40}) {
        std::string prefix = "HTTP/1.1 200 OK\r\n";
        for (int i = 0; i < fields; ++i) prefix += "X-" + std::to_string(i) + ": v\r\n";
        cases.push_back({"late-length", prefix + "Content-Length: 2\r\n\r\nhi", "hi", true, false, -1, true});
        cases.push_back({"late-conflict", prefix + "Content-Length: 2\r\nContent-Length: 3\r\n\r\nhi!", "", false, false, 0, false});
        cases.push_back({"late-chunked", prefix + "Transfer-Encoding: chunked\r\n\r\n2\r\nhi\r\n0\r\n\r\n", "hi", true, false, -1, true});
    }
    for (int size : {16383, 16384, 16385}) {
        std::string body(size, 'x');
        cases.push_back({"body-fixed-boundary", fixed(body), size <= 16384 ? body : "", size <= 16384, false, -1, false});
        cases.push_back({"body-close-boundary", "HTTP/1.1 200 OK\r\n\r\n" + body, size <= 16384 ? body : "", size <= 16384, false, 0, false});
        char chunk[24]; snprintf(chunk, sizeof(chunk), "%x", size);
        cases.push_back({"body-chunk-boundary", std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") + chunk + "\r\n" + body + "\r\n0\r\n\r\n", size <= 16384 ? body : "", size <= 16384, false, -1, false});
    }
    int case_id = 0;
    for (auto& test : cases) {
      ++case_id;
      for (int fragment : {1, 7, 1024}) {
        printf("FRAMING BEGIN id=%d %s fragment=%d\n", case_id, test.name, fragment);
        Fixture fixture(test.response, fragment, test.eof);
        int before = failures;
        {
            HttpRequest request(&fixture);
            expect(request.connect_to_server("fixture", 80) >= 0, "fixture open");
            StreamRamFile message(test.head ? 2 : 64);
            message.format("%s / HTTP/1.1\r\nHost: fixture\r\n\r\n", test.head ? "HEAD" : "GET");
            expect(request.send_request(&message) == 0, "fixture request");
            bool pass = request.recv_response();
            expect(pass == test.pass, "framing outcome");
            std::string body;
            uint8_t buffer[895]; int n;
            while ((n = request.read_response_data(sizeof(buffer), buffer)) > 0) body.append(reinterpret_cast<char *>(buffer), n);
            expect(body == test.body, "exact body or no partial publication");
            if (pass && test.no_eof) expect(fixture.reads <= int((test.response.size() + fragment - 1) / fragment), "complete without another transport read");
            if (pass && body.size() == 16384) {
                const char *text = request.get_response_string();
                expect(text && strlen(text) == 16384, "full buffer has separate terminator");
            }
        }
        expect(fixture.closes == 1, "single session cleanup");
        printf("FRAMING %s fragment=%d failures=%d\n", test.name, fragment, failures - before);
      }
    }
    printf("HTTP framing: %d cases, %d checks, %d failures\n", int(cases.size()) * 3, checks, failures);
    return failures ? 1 : 0;
}
