/* Host tests for the TLS commands ($21-$25, and READ/WRITE/CLOSE on a TLS handle) of the real
 * network_target.cc in the world of net_world.cc. Run: make -C target/pc/linux/wifitls test
 * Without the TLS commands every group is RED: the target answers 21,UNKNOWN COMMAND. */
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "net_world.h"
#include "network_target.h"
#include "check.h"

int failures;
int checks;

void check(bool condition, const char *what, ...)
{
    char line[400];
    va_list ap;
    va_start(ap, what);
    vsnprintf(line, sizeof(line), what, ap);
    va_end(ap);
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", line);
    } else {
        printf("  ok    %s\n", line);
    }
}

extern NetworkTarget net;

struct Res {
    bytes data;
    std::string status;
};

// One command as the C64 sends it, and every block of its reply as it reads them.
// The longest status any command of the run answered (the cap is 40 bytes).
static std::string longest_status;

static Res run(const bytes &command)
{
    static uint8_t buf[1024];
    memcpy(buf, command.data(), command.size());
    Message m = { (int)command.size(), true, buf };
    Message *reply, *status;
    net.parse_command(&m, &reply, &status);
    Res r;
    r.data.assign(reply->message, reply->message + reply->length);
    r.status.assign((const char *)status->message, status->length);
    if ((size_t)status->length > longest_status.size()) {
        longest_status = r.status;
    }
    int guard = 0;
    while (!reply->last_part && (guard++ < 10)) {
        net.get_more_data(&reply, &status);
        r.data.insert(r.data.end(), reply->message, reply->message + reply->length);
        r.status.append((const char *)status->message, status->length);
    }
    return r;
}

static bytes B(std::initializer_list<int> v)
{
    bytes b;
    for (int x : v) {
        b.push_back((uint8_t)x);
    }
    return b;
}

static bytes cat(bytes a, const bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static bytes S(const std::string &s)
{
    return bytes(s.begin(), s.end());
}

static bytes PIN(uint8_t fill = 0x5A)
{
    return bytes(32, fill);
}

static bytes open_cmd(uint8_t trust, uint8_t flags, const std::string &tail, const bytes &pin = bytes(),
                      uint16_t port = 443, uint8_t first = 0x03)
{
    bytes c = B({ first, 0x21, port & 0xFF, port >> 8, trust, flags });
    c = cat(c, pin);
    return cat(c, S(tail));
}

static std::string num(int n)
{
    char t[16];
    snprintf(t, sizeof(t), "%d", n);
    return t;
}

// Byte i of a reply, or $EE past its end: a red run must not read beyond it.
static uint8_t at(const Res &r, size_t i)
{
    return (i < r.data.size()) ? r.data[i] : 0xEE;
}

static uint8_t info_byte(size_t i);

static uint16_t header(const Res &r)
{
    return (r.data.size() >= 2) ? (uint16_t)(r.data[0] | (r.data[1] << 8)) : 0xEEEE;
}

static bytes body(const Res &r)
{
    return (r.data.size() >= 2) ? bytes(r.data.begin() + 2, r.data.end()) : bytes();
}

// Socket h of the world, or a spare one when h is no socket number: a red run must not crash.
static FakeSock &sock(int h)
{
    static FakeSock spare;
    if ((h < 0) || (h >= 16)) {
        spare = FakeSock();
        return spare;
    }
    return world_socks[h];
}

static Res read_cmd(int h, int maxlen)
{
    return run(B({ 0x03, 0x10, h, maxlen & 0xFF, maxlen >> 8 }));
}

static Res write_cmd(int h, const bytes &data)
{
    return run(cat(B({ 0x03, 0x11, h }), data));
}

static Res close_cmd(int h)
{
    return run(B({ 0x03, 0x09, h }));
}

static Res info(int h)
{
    return run(B({ 0x03, 0x23, h }));
}

static Res release(void)
{
    return run(B({ 0x03, 0x25 }));
}

static void fresh(void)
{
    world_reset();
    net.c64_reset();
    world_module.calls.clear();
    world_module.close_alls = 0;
    for (int i = 0; i < 16; i++) {
        world_socks[i] = FakeSock();
        world_socks[i].srv.saw_alert = -1;
        world_socks[i].srv.flight = 2000;
        world_socks[i].srv.delay_ms = 20;
        world_socks[i].srv.chunk = 1460;
    }
}

// An OPEN that should succeed; the handle, or -1.
static int open_ok(uint8_t trust = 0, const std::string &tail = "example.com", const bytes &pin = bytes())
{
    Res r = run(open_cmd(trust, 0, tail, pin));
    if ((r.status != "00,OK") || (r.data.size() != 8)) {
        printf("        open: %s (%d bytes)\n", r.status.c_str(), (int)r.data.size());
        return -1;
    }
    return r.data[0];
}

// READs until the stream ends or nothing more comes: the data, and the last status.
static bytes read_all(int h, int maxlen, std::string *last, int *reads = NULL, int limit = 400)
{
    bytes all;
    int n = 0;
    for (; n < limit; n++) {
        Res r = read_cmd(h, maxlen);
        *last = r.status;
        if (header(r) == 0xFFFF) {
            if (r.status.compare(0, 3, "02,") != 0) {
                break;
            }
            continue;
        }
        if (header(r) == 0) {
            break;
        }
        bytes b = body(r);
        all.insert(all.end(), b.begin(), b.end());
    }
    if (reads) {
        *reads = n;
    }
    return all;
}

static bytes expected(uint32_t seed, size_t n)
{
    bytes b;
    for (size_t i = 0; i < n; i++) {
        b.push_back(payload_byte(seed, i));
    }
    return b;
}

// The i-th module call, or an empty one: a red run must not crash.
static RpcCall call_no(size_t i)
{
    return (i < world_module.calls.size()) ? world_module.calls[i] : RpcCall();
}

static RpcCall last_call(void)
{
    return world_module.calls.empty() ? RpcCall() : world_module.calls.back();
}

static uint8_t info_byte(size_t i)
{
    return at(info(0xFF), i);
}

static int steps_logged(void)
{
    return world_count(0x31);
}

// --- TLS_INFO ($23) ------------------------------------------------------------

static void test_info(void)
{
    printf("TLS_INFO $FF and on a handle\n");
    fresh();
    Res r = info(0xFF);
    check(r.status == "00,OK" && r.data.size() == 16, "info: $FF answers 16 bytes, 00,OK (got %s, %d)",
          r.status.c_str(), (int)r.data.size());
    if (r.data.size() == 16) {
        const uint8_t want[16] = { 1, 2, 0, 7, 0x07, 3, 1, 0x32, 0x01, 0xFF, 0xFF, 0xFF, 12, 0, 0, 0 };
        check(memcmp(r.data.data(), want, 16) == 0,
              "info: v1, 2 sessions, 0 in use, modes 7, ready 07 (image, entropy, time), versions 3, module 1.306, "
              "CA date FF FF FF (unknown), block 12 ms, 0 unclaimed");
    }
    check(world_count(0x33) == 1 && last_call().slot == 0xFF && last_call().tag == 0,
          "info: one global STATS (slot $FF, tag 0)");
    world_module.available = false;
    world_module.calls.clear();
    r = info(0xFF);
    check(r.data.size() == 16 && !(r.data[4] & 0x01) && world_module.calls.empty(),
          "info: no module -> ready bit0 clear, no RPC");
    world_module.available = true;
    world_module.entropy = false;
    world_module.module_time = false;
    world_sntp = 0;
    r = info(0xFF);
    check(r.data.size() == 16 && r.data[4] == 0x01, "info: no entropy, no time anywhere -> ready 01 (got %02x)",
          r.data.size() == 16 ? r.data[4] : 0xEE);
    world_sntp = 1790000000u;
    r = info(0xFF);
    check(r.data.size() == 16 && r.data[4] == 0x05, "info: Nios SNTP alone gives the time bit");
    check(run(B({ 0x03, 0x23 })).status == "81,INVALID PARAMS", "info: 2 bytes -> 81");
    check(run(B({ 0x03, 0x23, 0xFF, 0x00 })).status == "81,INVALID PARAMS", "info: 4 bytes -> 81");
    check(info(5).status == "81,INVALID PARAMS" && info(5).data.empty(), "info: not a TLS handle -> empty, 81");

    // The last good STATS stands through a STATS that gets no
    // answer, within one module generation.
    fresh();
    world_module.block_max_ms = 33;
    r = info(0xFF);
    world_module.block_max_ms = 44;
    world_module.stats_result = WIFI_TLS_BUSY;
    r = info(0xFF);
    check(r.data.size() == 16 && (r.data[4] & 0x03) == 0x03 && r.data[12] == 33 && r.data[9] == 0xFF,
          "info (cache): a STATS timeout keeps b0/b1, block_max 33 and the CA date from the last good STATS");
    world_module.stats_result = WIFI_TLS_UNSENT;
    check((info_byte(4) & 0x01) == 0x01, "info (cache): an unsent STATS keeps b0 too");
    world_module.stats_result = WIFI_TLS_BUSY;
    world_module.generation++;
    r = info(0xFF);
    check(r.data.size() == 16 && !(r.data[4] & 0x03) && r.data[12] == 0 && r.data[9] == 0,
          "info (cache): after a module restart a STATS timeout clears b0/b1, block_max and the CA date");
    world_module.stats_result = -1;
    check((info_byte(4) & 0x01) == 0x01 && info_byte(12) == 44, "info (cache): the next good STATS sets them again");
    world_module.stats_result = WIFI_TLS_INVALID;
    check((info_byte(4) & 0x01) == 0x01, "info (cache): a malformed STATS answer keeps b0 (only NO_MODULE, "
          "NOT_SUPPORTED or a new generation clear it)");
    world_module.stats_result = WIFI_TLS_REFUSED; // a module without TLS
    world_module.stats_esp_err = WIFI_TLS_ESP_NOT_SUPPORTED;
    check(!(info_byte(4) & 0x01), "info (cache): a STATS refused with NOT_SUPPORTED clears b0");
    world_module.stats_esp_err = 0;
    world_module.stats_result = -1;
    info(0xFF);
    world_module.available = false;
    r = info(0xFF);
    world_module.available = true;
    world_module.stats_result = WIFI_TLS_BUSY;
    check(r.data.size() == 16 && !(r.data[4] & 0x01) && r.data[9] == 0 && r.data[10] == 0 && r.data[11] == 0 &&
          !(info_byte(4) & 0x01), "info (cache): no module clears b0 and the cache; the CA date is 0,0,0");
    world_module.stats_result = -1;
}

// --- OPEN_TLS: the happy path --------------------------------------------------

static void test_open(void)
{
    printf("OPEN_TLS ($21): reply, RPC fields, socket\n");
    fresh();
    Res r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "00,OK", "open: BUNDLE example.com -> 00,OK (got %s)", r.status.c_str());
    int h = r.data.empty() ? -1 : r.data[0];
    const uint8_t want[8] = { (uint8_t)h, 0x04, 0x03, 0x01, 0x13, 0x1D, 0x00, 0x00 };
    check(r.data.size() == 8 && memcmp(r.data.data(), want, 8) == 0,
          "open: reply is handle, 04 03, 01 13, 1D 00, 00 (8 bytes, LE)");
    RpcCall *o = NULL;
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        if (world_module.calls[i].cmd == 0x30) {
            o = &world_module.calls[i];
        }
    }
    check(o != NULL, "open: one OPEN RPC");
    if (o) {
        check(o->open.trust == 0 && o->sni == "example.com" && o->open.slot == 0 && o->open.tag != 0,
              "open: RPC trust 0, sni example.com, slot 0, tag %u != 0", o->open.tag);
        check(o->open.time_source == WIFI_TLS_TIME_NIOS && o->open.unix_time == world_sntp,
              "open: the Nios SNTP time goes along (time_source 1)");
        check(o->open.opts == 0 && o->open.ca_tag == 0 && o->pin.empty(), "open: opts 0, ca_tag 0, no pin");
        check(o->cancel_given, "open: the RPC wait has a cancel predicate");
    }
    check(world_dns_calls == 1 && world_dns_unlocked_calls == 0, "open: dns_gethostbyname under the core lock");
    if (h >= 0 && h < 16) {
        check(sock(h).nodelay, "open: TCP_NODELAY on the TLS socket");
        check(sock(h).sndtimeo_ms == 500, "open: SO_SNDTIMEO 500 ms caps lwIP's close (%d)", sock(h).sndtimeo_ms);
        check(sock(h).nonblocking, "open: the TLS socket is non-blocking (SEND_MS)");
        check(sock(h).srv.hellos == 1, "open: the server saw one ClientHello");
    }
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        const RpcCall &c = world_module.calls[i];
        if (c.cmd == 0x31) {
            check(o && c.flags == 0 && c.slot == 0 && c.tag == o->open.tag, "open: handshake step %d: flags 0, slot and tag",
                  (int)i);
        }
    }
    r = info(h);
    check(r.status == "00,OK" && r.data.size() == 48, "info: a TLS handle answers 48 bytes");
    if (r.data.size() == 48) {
        check(r.data[0] == 0x04 && r.data[1] == 0x03 && r.data[2] == 0x01 && r.data[3] == 0x13 &&
              r.data[4] == 0x1D && r.data[5] == 0 && r.data[6] == 0 && r.data[7] == 0,
              "info: version, suite, group LE, trust 0, bits 0");
        check(memcmp(&r.data[8], world_module.spki, 32) == 0, "info: SPKI hash from the module, digest order");
        uint32_t ms = r.data[40] | (r.data[41] << 8) | (r.data[42] << 16) | (r.data[43] << 24);
        check(ms > 0 && ms < 2000 && r.data[44] == 0 && r.data[47] == 0, "info: handshake %u ms, then 4 zero bytes", ms);
    }

    fresh();
    world_module.version = 0x0303;
    world_module.suite = 0xC02B;
    world_module.group = 0x0017;
    world_module.ems = 1;
    r = run(open_cmd(0, 0x01, "example.com"));
    check(r.data.size() == 8 && r.data[1] == 0x03 && r.data[2] == 0x03 && r.data[3] == 0x2B && r.data[4] == 0xC0 &&
          r.data[5] == 0x17 && r.data[7] == 0x01, "open: TLS 1.2 with EMS -> 03 03, 2B C0, 17 00, bit0 set");
    check(call_no(0).open.opts == WIFI_TLS_OPT_REQUIRE_TLS13, "open: flags bit0 -> opts REQUIRE_TLS13");

    fresh();
    world_module.group = 0;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "17,TLS MODULE NOT RESPONDING" && r.data.empty() && world_open_sockets() == 0 &&
          world_count(0x32) == 1, "open: STATS with group 0 -> 17, socket and session closed (an established group is never 0)");

    fresh();
    world_module.hello_size = 1400;
    r = run(open_cmd(0, 0, "example.com"));
    int first = -1, second = -1;
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        if (world_module.calls[i].cmd == 0x31) {
            if (first < 0) {
                first = i;
            } else if (second < 0) {
                second = i;
            }
        }
    }
    check(r.status == "00,OK" && second > 0 && call_no(second).ct_in_len == 0,
          "open: a 1403-B ClientHello goes in two steps, the second input-less (HANDSHAKE_MS)");
    h = r.data.empty() ? 0 : r.data[0];
    check(sock(h).srv.hellos == 1, "open: the server got the whole ClientHello");

    fresh();
    for (int i = 0; i < 3; i++) {
        uint16_t last = 0;
        int a = open_ok();
        for (size_t k = 0; k < world_module.calls.size(); k++) {
            if (world_module.calls[k].cmd == 0x30) {
                last = world_module.calls[k].open.tag;
            }
        }
        check(a >= 0 && last != 0, "open: open %d has a new non-zero tag %u", i, last);
        close_cmd(a);
    }
    uint16_t t1 = 0, t2 = 0;
    for (size_t k = 0; k < world_module.calls.size(); k++) {
        if (world_module.calls[k].cmd == 0x30) {
            t1 = t2;
            t2 = world_module.calls[k].open.tag;
        }
    }
    check(t1 != t2, "open: consecutive OPENs of one slot carry different tags");
}

// --- OPEN_TLS: the grammar, and 81 before 82 ------------------------------------

static void expect_open(const bytes &cmd, const char *want, const char *what)
{
    world_module.calls.clear();
    int dns = world_dns_calls;
    Res r = run(cmd);
    check(r.status == want && (strcmp(want, "00,OK") == 0 || (r.data.empty() && world_module.calls.empty() &&
                                                             world_dns_calls == dns && world_open_sockets() == 0)),
          "grammar: %s -> %s (got %s)", what, want, r.status.c_str());
    if (r.status == "00,OK" && !r.data.empty()) {
        close_cmd(at(r, 0));
    }
}

static std::string last_sni(void)
{
    for (size_t i = world_module.calls.size(); i-- > 0;) {
        if (world_module.calls[i].cmd == 0x30) {
            return world_module.calls[i].sni;
        }
    }
    return "<none>";
}

static void test_grammar(void)
{
    printf("OPEN_TLS: tail grammar, names and refusals (S19)\n");
    fresh();
    std::string h254(254, 'a');
    std::string h253(253, 'a');
    expect_open(B({ 0x03, 0x21, 0xBB, 0x01, 0 }), "81,INVALID PARAMS", "5 bytes");
    expect_open(open_cmd(0, 0, ""), "81,INVALID PARAMS", "empty host");
    expect_open(open_cmd(0, 0, std::string("\0", 1)), "81,INVALID PARAMS", "host is a lone $00");
    expect_open(open_cmd(0, 0, std::string("example.com\0\0", 13)), "81,INVALID PARAMS", "host $00 $00");
    expect_open(open_cmd(0, 0, std::string("example.com\0a\0b", 15)), "81,INVALID PARAMS", "a $00 inside the name");
    expect_open(open_cmd(0, 0, std::string("example.com\0name\0\0", 18)), "81,INVALID PARAMS",
                "bytes after the name's $00");
    expect_open(open_cmd(0, 0, h254), "81,INVALID PARAMS", "a 254-byte host");
    expect_open(open_cmd(0, 0, std::string("x.com\0", 6) + h254), "81,INVALID PARAMS", "a 254-byte name");
    expect_open(open_cmd(2, 0, "example.com", bytes(20, 1)), "81,INVALID PARAMS", "a short pin");
    expect_open(open_cmd(3, 0, "example.com"), "82,PARAMETER(S) OUT OF RANGE", "trust 3");
    expect_open(open_cmd(0, 0x02, "example.com"), "82,PARAMETER(S) OUT OF RANGE", "flags $02 (reserved)");
    expect_open(open_cmd(0, 0x80, "example.com"), "82,PARAMETER(S) OUT OF RANGE", "flags $80");
    expect_open(open_cmd(2, 0, "example.com", PIN(0)), "82,PARAMETER(S) OUT OF RANGE", "an all-zero pin");
    expect_open(open_cmd(0, 0x02, ""), "81,INVALID PARAMS", "a reserved bit AND an empty host: 81 first");
    expect_open(open_cmd(3, 0x02, std::string("a\0\0", 3)), "81,INVALID PARAMS", "trust 3 AND a bad tail: 81 first");
    expect_open(open_cmd(0, 0, "1.2.3.4"), "81,INVALID PARAMS", "BUNDLE, IPv4 host, no name");
    expect_open(open_cmd(1, 0, "1.2.3.4"), "81,INVALID PARAMS", "CUSTOM, IPv4 host, no name");
    expect_open(open_cmd(0, 0, "127.1"), "81,INVALID PARAMS", "BUNDLE, host 127.1 (ip4addr_aton)");
    expect_open(open_cmd(0, 0, "0x7f.0.0.1"), "81,INVALID PARAMS", "BUNDLE, host 0x7f.0.0.1");
    expect_open(open_cmd(0, 0, "1.2.3.4 x"), "81,INVALID PARAMS", "BUNDLE, host '1.2.3.4 x'");
    expect_open(open_cmd(0, 0, std::string("example.com\0" "10.0.0.1", 20)), "81,INVALID PARAMS",
                "BUNDLE, an explicit IPv4 name");
    expect_open(open_cmd(0, 0, std::string("1.2.3.4\0", 8)), "81,INVALID PARAMS", "BUNDLE, IPv4 host $00");

    expect_open(open_cmd(0, 0x01, "example.com"), "00,OK", "flags $01 (REQUIRE_TLS13) is accepted");
    expect_open(open_cmd(0, 0, std::string("example.com\0", 12)), "00,OK", "host $00 (dos-lib habit)");
    check(last_sni() == "example.com", "name: host $00 -> the host is the name");
    expect_open(open_cmd(0, 0, std::string("1.2.3.4\0srv.example", 19)), "00,OK", "BUNDLE, IPv4 host with a name");
    check(last_sni() == "srv.example", "name: the explicit name goes to SNI");
    expect_open(open_cmd(0, 0, std::string("example.com\0srv\0", 16)), "00,OK", "host $00 name $00");
    check(last_sni() == "srv", "name: host $00 name $00 -> name");
    expect_open(open_cmd(0, 0, h253), "00,OK", "a 253-byte host");
    expect_open(open_cmd(2, 0, "10.1.2.3", PIN()), "00,OK", "PIN to an IPv4 host, no name");
    check(last_sni() == "", "name: PIN with an IPv4 host sends no SNI");
    expect_open(open_cmd(2, 0, std::string("host.lan\0" "10.1.2.3", 17), PIN()), "00,OK", "PIN, explicit IPv4 name");
    check(last_sni() == "", "name: PIN with an explicit IPv4 name sends no SNI");
    expect_open(open_cmd(2, 0, "host.lan", PIN()), "00,OK", "PIN to a DNS name");
    check(last_sni() == "host.lan", "name: PIN with a DNS name sends SNI");
    RpcCall o = call_no(0);
    check(o.pin == PIN() && o.open.trust == 2, "pin: the 32 pin bytes go to the module unchanged");

    // No reply, nothing opened.
    world_module.calls.clear();
    int dns = world_dns_calls;
    Res r = run(open_cmd(0, 0, "example.com", bytes(), 443, 0x83));
    check(r.status == "81,INVALID PARAMS" && world_module.calls.empty() && world_dns_calls == dns &&
          world_open_sockets() == 0, "no-reply: $83 $21 opens nothing");
}

// --- OPEN_TLS: the refusal order ------------------------------------------------

static void test_precedence(void)
{
    printf("OPEN_TLS: refusal precedence, nothing on the network before the checks pass\n");
    fresh();
    world_module.available = false;
    expect_open(open_cmd(0, 0, "example.com"), "90,TLS NOT AVAILABLE", "no module");
    fresh();
    int a = open_ok(), b = open_ok();
    world_dns.push_back({ "nowhere.invalid", DNS_ASYNC_NO, 0, 10 });
    world_module.calls.clear();
    int dns = world_dns_calls;
    int sockets = world_open_sockets();
    Res r = run(open_cmd(0, 0, "nowhere.invalid"));
    check(r.status == "91,TLS SESSIONS IN USE" && world_module.calls.empty() && world_dns_calls == dns &&
          world_open_sockets() == sockets, "precedence: table full + unresolvable host -> 91, no DNS, no socket");
    close_cmd(a);
    close_cmd(b);
    fresh();
    sock(0).srv.connect_err = ECONNREFUSED;
    expect_open(open_cmd(1, 0, "example.com"), "97,NO CUSTOM CA", "CUSTOM with nothing loaded + closed port");
    fresh();
    world_module.entropy = false;
    world_module.calls.clear();
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "87,NO ENTROPY SOURCE" && world_dns_calls == 0 && world_open_sockets() == 0,
          "precedence: module reason 1 -> 87, before DNS");
    fresh();
    world_module.module_time = false;
    world_sntp = 0;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "92,NO TRUSTED TIME" && world_dns_calls == 0, "precedence: module reason 2 -> 92");
    r = run(open_cmd(2, 0, "10.0.0.5", PIN()));
    check(r.status == "00,OK", "precedence: PIN needs no time");
    fresh();
    world_module.open_result = WIFI_TLS_REFUSED;
    world_module.open_esp_err = WIFI_TLS_ESP_INVALID_STATE;
    world_module.open_reason = WIFI_TLS_REASON_NO_CUSTOM_CA;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "97,NO CUSTOM CA" && world_dns_calls == 0, "precedence: module reason 3 -> 97 NO CUSTOM CA");
    fresh();
    world_module.open_result = WIFI_TLS_REFUSED;
    world_module.open_esp_err = WIFI_TLS_ESP_NOT_SUPPORTED;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "90,TLS NOT AVAILABLE" && world_dns_calls == 0,
          "precedence: a module without TLS (NOT_SUPPORTED) -> 90");
    fresh();
    world_module.open_result = WIFI_TLS_REFUSED;
    world_module.open_esp_err = WIFI_TLS_ESP_NO_MEM;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "17,TLS MODULE NOT RESPONDING" && world_dns_calls == 0, "precedence: another refusal -> 17");
    fresh();
    world_module.open_delay_ms = 6000;
    uint32_t t0 = world_ms();
    r = run(open_cmd(0, 0, "example.com"));
    uint32_t spent = world_ms() - t0;
    check(r.status == "17,TLS MODULE NOT RESPONDING" && world_dns_calls == 0 && world_count(0x32) == 1,
          "precedence: OPEN RPC timeout -> 17, best-effort CLOSE(slot, tag), no DNS");
    check(spent >= 5000 && spent <= 6200, "precedence: OPEN_RPC_MS 5 s + CLOSE (%u ms)", spent);
    fresh();
    world_module.unsent_next = 3;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "00,OK", "precedence: an unsent OPEN is sent again (UNSENT_RETRIES)");
}

// --- the verify classification, and other handshake ends -------------

static void expect_fail(uint8_t trust, uint8_t reason, uint32_t flags, uint8_t alert, int err, const char *want,
                        const char *what)
{
    fresh();
    world_module.hs_fail = true;
    world_module.hs_fail_reason = reason;
    world_module.hs_verify_flags = flags;
    world_module.hs_alert_in = alert;
    world_module.hs_fail_err = err;
    world_module.hs_alert_out = true;
    Res r = (trust == 2) ? run(open_cmd(2, 0, "host.lan", PIN())) : run(open_cmd(trust, 0, "example.com"));
    check(r.status == want && r.data.empty() && world_open_sockets() == 0 && world_count(0x32) == 0 &&
          sock(0).srv.saw_alert == 40,
          "classify: %s -> %s (got %s); socket closed, no CLOSE RPC, our alert went out", what, want, r.status.c_str());
}

static void test_classify(void)
{
    printf("OPEN_TLS: handshake failures to codes\n");
    const uint8_t N = WIFI_TLS_ALERT_NONE;
    expect_fail(0, 0, 0, 70, -0x7780, "14,TLS ALERT RECEIVED: 70", "reason 0 with alert 70");
    expect_fail(0, 0, 0, N, -0x7280, "13,TLS HANDSHAKE FAILED: -0x7280", "reason 0, no alert");
    expect_fail(0, 1, 0x0303, N, -0x7700, "18,TLS VERSION REFUSED: 0x0303", "reason 1 (TLS 1.2 under REQUIRE_TLS13)");
    expect_fail(0, 1, 0x0302, N, -0x7700, "18,TLS VERSION REFUSED: 0x0302", "reason 1 (below 1.2)");
    expect_fail(0, 2, 0, N, -0x7700, "19,TLS DOWNGRADE DETECTED", "reason 2");
    expect_fail(0, 3, 0, N, -0x7700, "13,TLS HANDSHAKE FAILED: NO EMS", "reason 3");
    expect_fail(0, 4, 0x08, N, -0x2700, "93,CERTIFICATE NOT TRUSTED: 0x00000008", "BUNDLE, unknown root (exact)");
    expect_fail(0, 4, 0xFFFFFFFF, N, -0x3000, "93,CERTIFICATE NOT TRUSTED: 0xFFFFFFFF", "w = 0xFFFFFFFF");
    expect_fail(0, 4, 0x0C, N, -0x2700, "94,CERTIFICATE NAME MISMATCH: 0x0000000C", "BUNDLE, w 0x0C (name mismatch first)");
    expect_fail(0, 4, 0x05, N, -0x2700, "94,CERTIFICATE NAME MISMATCH: 0x00000005", "BUNDLE, name and expiry");
    expect_fail(0, 4, 0x01, N, -0x2700, "95,CERTIFICATE EXPIRED: 0x00000001", "BUNDLE, expired");
    expect_fail(0, 4, 0x200, N, -0x2700, "95,CERTIFICATE EXPIRED: 0x00000200", "BUNDLE, not yet valid");
    expect_fail(0, 4, 0x4000, N, -0x7A00, "93,CERTIFICATE NOT TRUSTED: 0x00004000", "BUNDLE, BAD_MD");
    expect_fail(2, 4, 0x04, N, -0x7A00, "93,CERTIFICATE NOT TRUSTED: 0x00000004", "PIN, w 0x04 (no name check)");
    expect_fail(2, 4, 0x01, N, -0x7A00, "93,CERTIFICATE NOT TRUSTED: 0x00000001", "PIN, w 0x01");
    expect_fail(2, 4, 0x200, N, -0x7A00, "93,CERTIFICATE NOT TRUSTED: 0x00000200", "PIN, w 0x200");
    expect_fail(2, 5, 0x08, N, -0x2700, "96,PINNED KEY MISMATCH", "PIN, reason 5");
    expect_fail(0, 5, 0x08, N, -0x2700, "17,TLS MODULE NOT RESPONDING", "BUNDLE, reason 5 (PIN only: malformed)");
    expect_fail(0, 6, 0, N, -0x7280, "13,TLS HANDSHAKE FAILED: PEER CLOSED", "reason 6");
    expect_fail(0, 9, 0, N, -0x7280, "17,TLS MODULE NOT RESPONDING", "an unknown reason");

    fresh();
    sock(0).srv.on_hello = SRV_RST;
    Res r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: PEER RESET: " + num(ECONNRESET) && world_open_sockets() == 0 &&
          world_count(0x32) == 1, "handshake: RST after the ClientHello -> 13 PEER RESET: errno, CLOSE RPC (S7c)");
    fresh();
    sock(0).srv.on_hello = SRV_FIN;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: PEER CLOSED" && world_open_sockets() == 0 && world_count(0x32) == 1,
          "handshake: FIN mid-handshake -> 13 PEER CLOSED");
    fresh();
    sock(0).srv.on_hello = SRV_STALL;
    uint32_t t0 = world_ms();
    r = run(open_cmd(0, 0, "example.com"));
    uint32_t spent = world_ms() - t0;
    check(r.status == "15,TLS HANDSHAKE TIMEOUT" && world_open_sockets() == 0 && world_count(0x32) == 1,
          "handshake: a stalled server -> 15, CLOSE RPC, socket closed (S10)");
    check(spent >= 15000 && spent <= 16300, "handshake: within HANDSHAKE_MS + CLOSE (%u ms)", spent);
    fresh();
    world_module.forced.push_back(Forced());
    world_module.forced.back().result = WIFI_TLS_BUSY;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "17,TLS MODULE NOT RESPONDING" && world_count(0x32) == 1 && world_open_sockets() == 0,
          "handshake: a STEP timeout -> 17 and CLOSE(slot, tag)");
}

// --- DNS and connect ------------------------------------------------------

static void test_dns_connect(void)
{
    printf("OPEN_TLS: DNS inside OPEN, connect\n");
    fresh();
    world_dns.push_back({ "slow.example", DNS_NEVER, 0, 0 });
    uint32_t t0 = world_ms();
    Res r = run(open_cmd(0, 0, "slow.example"));
    uint32_t spent = world_ms() - t0;
    check(r.status == "84,UNRESOLVED HOST" && world_count(0x32) == 1 && world_open_sockets() == 0,
          "dns: no answer -> 84 and the module session CLOSEd");
    check(spent >= 10000 && spent <= 11200, "dns: after DNS_MS (%u ms)", spent);
    fresh();
    world_dns.push_back({ "bad.example", DNS_ASYNC_NO, 0, 30 });
    r = run(open_cmd(0, 0, "bad.example"));
    check(r.status == "84,UNRESOLVED HOST", "dns: a failed lookup -> 84");
    fresh();
    world_dns.push_back({ "val.example", DNS_ERR_VAL, 0, 0 });
    r = run(open_cmd(0, 0, "val.example"));
    check(r.status == "84,UNRESOLVED HOST", "dns: ERR_VAL -> 84");
    // A late answer to an abandoned lookup is never taken for the next one.
    fresh();
    world_dns.push_back({ "a.example", DNS_ASYNC_OK, 0x01010101, 12000 });
    world_dns.push_back({ "b.example", DNS_ASYNC_OK, 0x02020202, 3000 });
    r = run(open_cmd(0, 0, "a.example"));
    check(r.status == "84,UNRESOLVED HOST", "dns: a.example answers after DNS_MS -> 84");
    r = run(open_cmd(0, 0, "b.example"));
    int h = r.data.empty() ? 0 : r.data[0];
    check(r.status == "00,OK" && sock(h).connect_addr == 0x02020202,
          "dns: the next OPEN connects to b's address, not a's late answer (the slot seq)");
    check(world_lock_bad == 0 && world_dns_unlocked_calls == 0, "dns: lookups start under the lock");
    fresh();
    r = run(open_cmd(2, 0, "10.9.8.7", PIN()));
    h = r.data.empty() ? 0 : r.data[0];
    check(r.status == "00,OK" && sock(h).connect_addr == htonl(0x0A090807),
          "dns: an IPv4 literal resolves at once");

    fresh();
    sock(0).srv.connect_err = ECONNREFUSED;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "11,ERROR ON CONNECT: " + num(ECONNREFUSED) && world_count(0x32) == 1 &&
          world_open_sockets() == 0, "connect: refused -> 11 with errno, session CLOSEd, socket closed");
    fresh();
    sock(0).srv.connect_never = true;
    t0 = world_ms();
    r = run(open_cmd(0, 0, "example.com"));
    spent = world_ms() - t0;
    check(r.status == "11,ERROR ON CONNECT: " + num(ETIMEDOUT) && spent >= 10000 && spent <= 11300,
          "connect: no answer -> 11 ETIMEDOUT after CONNECT_MS (%u ms)", spent);

    // The whole OPEN is bounded (OPEN_TOTAL_MS + CLOSE_RPC_MS + a slice).
    fresh();
    world_module.open_delay_ms = 4900;
    world_dns.push_back({ "slow.example", DNS_ASYNC_OK, 0x03030303, 9900 });
    sock(0).srv.connect_delay_ms = 9900;
    sock(0).srv.on_hello = SRV_STALL;
    t0 = world_ms();
    r = run(open_cmd(0, 0, "slow.example"));
    spent = world_ms() - t0;
    check(r.status == "15,TLS HANDSHAKE TIMEOUT" && spent <= 41100,
          "budget: slow OPEN RPC, DNS, connect and a stalled server end within 41.1 s (%u ms, %s)", spent,
          r.status.c_str());
}

// --- a C64 reset during OPEN --------------------------------------------------

static void test_reset_during_open(void)
{
    printf("OPEN_TLS: a reset ends it at once\n");
    fresh();
    sock(0).srv.on_hello = SRV_STALL;
    world_reset_at = world_tick + 600; // 3 s in
    Res r = run(open_cmd(0, 0, "example.com"));
    uint32_t late = (world_tick - world_reset_at) * 5;
    check(late <= 110, "reset: the OPEN returns within a slice of the reset (%u ms late)", late);
    check(world_open_sockets() == 0 && world_count(0x32) == 0, "reset: socket closed, no CLOSE RPC waited for");
    net.c64_reset();
    world_reset_at = 0xFFFFFFFF;
    check(world_module.close_alls == 1, "reset: c64_reset sends CLOSE_ALL once");
    r = info(0xFF);
    check(r.data.size() == 16 && r.data[2] == 0, "reset: no entries afterwards");
    fresh();
    world_dns.push_back({ "never.example", DNS_NEVER, 0, 0 });
    world_reset_at = world_tick + 200;
    r = run(open_cmd(0, 0, "never.example"));
    late = (world_tick - world_reset_at) * 5;
    check(late <= 110 && world_open_sockets() == 0, "reset: during DNS too (%u ms late)", late);
    world_reset_at = 0xFFFFFFFF;
}

// --- START_TLS ($24) --------------------------------------------------------------------

static int open_tcp(void)
{
    Res r = run(B({ 0x03, 0x07, 0x19, 0x00, 'm', 'a', 'i', 'l' }));
    return (r.status == "00,OK") ? at(r, 0) : -1;
}

static bytes start_cmd(int h, uint8_t trust, uint8_t flags, const std::string &tail, const bytes &pin = bytes(),
                       uint8_t first = 0x03)
{
    return cat(cat(B({ first, 0x24, h, trust, flags }), pin), S(tail));
}

static void expect_kept(int h, const bytes &cmd, const char *want, const char *what)
{
    Res r = run(cmd);
    bool untouched = sock(h).open && !sock(h).nodelay && !sock(h).nonblocking && !sock(h).sndtimeo_ms;
    check(r.status == want && r.data.empty() && untouched, "start: %s -> %s, TCP handle unchanged (got %s)", what,
          want, r.status.c_str());
}

static void test_start_tls(void)
{
    printf("START_TLS ($24): handle checks first, kept codes, success\n");
    fresh();
    int t = open_tcp();
    Res u = run(B({ 0x03, 0x08, 0x35, 0x00, 'n', 's' }));
    int udp = u.data.empty() ? -1 : u.data[0];
    check(t >= 0 && udp >= 0, "start: a TCP and a UDP handle to work with");
    check(!sock(t).sndtimeo_ms && !sock(t).nodelay, "start: OPEN_TCP's socket gets neither (TLS sockets only)");
    Res r = run(start_cmd(udp, 2, 0, "", PIN()));
    check(r.status == "81,INVALID PARAMS" && sock(udp).open, "start: on a UDP handle -> 81, UDP untouched");
    r = run(start_cmd(12, 2, 0, "", PIN()));
    check(r.status == "81,INVALID PARAMS", "start: on an unowned number -> 81");
    r = run(start_cmd(udp, 3, 0x80, ""));
    check(r.status == "81,INVALID PARAMS", "start: UDP handle AND trust 3: the handle check comes first -> 81");
    r = run(start_cmd(12, 3, 0, "x"));
    check(r.status == "81,INVALID PARAMS", "start: unowned number AND trust 3 -> 81, not 82");
    r = run(B({ 0x03, 0x24 }));
    check(r.status == "81,INVALID PARAMS", "start: no handle byte -> 81");
    expect_kept(t, start_cmd(t, 3, 0, "mail.example"), "82,PARAMETER(S) OUT OF RANGE", "trust 3");
    expect_kept(t, start_cmd(t, 0, 2, "mail.example"), "82,PARAMETER(S) OUT OF RANGE", "flags $02");
    expect_kept(t, start_cmd(t, 2, 0, "", PIN(0)), "82,PARAMETER(S) OUT OF RANGE", "zero pin");
    expect_kept(t, start_cmd(t, 0, 0, ""), "81,INVALID PARAMS", "BUNDLE without a name");
    expect_kept(t, start_cmd(t, 0, 0, std::string("\0", 1)), "81,INVALID PARAMS", "BUNDLE, a lone $00");
    expect_kept(t, start_cmd(t, 0, 0, "10.0.0.1"), "81,INVALID PARAMS", "BUNDLE, an IPv4 name");
    expect_kept(t, start_cmd(t, 0, 0, std::string("a\0b", 3)), "81,INVALID PARAMS", "bytes after the name's $00");
    expect_kept(t, start_cmd(t, 1, 0, "mail.example"), "97,NO CUSTOM CA", "CUSTOM, nothing loaded");
    world_module.available = false;
    expect_kept(t, start_cmd(t, 0, 0, "mail.example"), "90,TLS NOT AVAILABLE", "no module");
    world_module.available = true;
    world_module.entropy = false;
    expect_kept(t, start_cmd(t, 0, 0, "mail.example"), "87,NO ENTROPY SOURCE", "module reason 1");
    world_module.entropy = true;
    world_module.module_time = false;
    world_sntp = 0;
    expect_kept(t, start_cmd(t, 0, 0, "mail.example"), "92,NO TRUSTED TIME", "module reason 2");
    world_module.module_time = true;
    world_sntp = 1790000000u;
    // The TCP handle still carries plaintext after all of that.
    server_push(t, EV_DATA, S("220 hello\r\n"));
    r = read_cmd(t, 100);
    check(r.status == "00,OK" && body(r) == S("220 hello\r\n"), "start: the TCP handle still reads plaintext");

    world_module.calls.clear();
    r = run(start_cmd(t, 2, 0, std::string("\0", 1), PIN()));
    check(r.status == "00,OK" && r.data.size() == 8 && r.data[0] == t, "start: PIN, a lone $00 -> OK, the same handle");
    check(last_sni() == "", "start: no name -> no SNI");
    check(sock(t).nodelay && sock(t).nonblocking && (sock(t).sndtimeo_ms == 500),
          "start: TCP_NODELAY and SO_SNDTIMEO 500 ms once every refusal check passed");
    r = run(start_cmd(t, 2, 0, "", PIN()));
    check(r.status == "81,INVALID PARAMS" && sock(t).open, "start: on a TLS handle -> 81, session untouched");
    check(info(t).data.size() == 48, "start: the handle is a TLS handle now");
    close_cmd(t);

    fresh();
    t = open_tcp();
    world_module.hs_fail = true;
    world_module.hs_fail_reason = 4;
    world_module.hs_verify_flags = 0x08;
    r = run(start_cmd(t, 0, 0, "mail.example"));
    check(r.status == "93,CERTIFICATE NOT TRUSTED: 0x00000008" && !sock(t).open,
          "start: a 93 closes the handle (every code but the kept ones)");
    r = close_cmd(t);
    check(r.status == "12,ERROR ON CLOSE: " + num(EBADF), "start: CLOSE after it -> 12,ERROR ON CLOSE: 9");
    fresh();
    t = open_tcp();
    world_module.open_delay_ms = 6000;
    r = run(start_cmd(t, 0, 0, "mail.example"));
    check(r.status == "17,TLS MODULE NOT RESPONDING" && !sock(t).open,
          "start: a 17 from the OPEN RPC closes the handle");
    fresh();
    t = open_tcp();
    int a = open_ok(), b = open_ok();
    expect_kept(t, start_cmd(t, 0, 0, "mail.example"), "91,TLS SESSIONS IN USE", "table full");
    close_cmd(a);
    close_cmd(b);
    // No reply: nothing happens.
    world_module.calls.clear();
    r = run(start_cmd(t, 2, 0, "", PIN(), 0x83));
    check(world_module.calls.empty() && sock(t).open && !sock(t).nodelay,
          "start: $83 $24 does nothing");
}

// --- READ, WRITE and CLOSE on a TLS handle -----------------------------------------------

static void test_read(void)
{
    printf("READ on a TLS handle\n");
    fresh();
    int h = open_ok();
    uint32_t t0 = world_ms();
    Res r = read_cmd(h, 100);
    check(header(r) == 0xFFFF && r.status == "02,NO DATA: " + num(EWOULDBLOCK),
          "read: nothing yet -> $FFFF + 02,NO DATA: 11 (%s)", r.status.c_str());
    check(world_ms() - t0 <= 300, "read: an idle READ is short (%u ms)", world_ms() - t0);
    check(read_cmd(h, 0).status == "82,PARAMETER(S) OUT OF RANGE", "read: maxlen 0 -> 82");
    check(read_cmd(h, 1473).status == "82,PARAMETER(S) OUT OF RANGE", "read: maxlen 1473 -> 82");

    server_push(h, EV_DATA, record('D', 3000, 5));
    server_push(h, EV_DATA, record('C', 0));
    server_push(h, EV_FIN, bytes());
    std::string last;
    bytes got = read_all(h, 1472, &last);
    check(got == expected(5, 3000), "read: all 3000 B of one record, in order (%d)", (int)got.size());
    check(last == "01,CONNECTION CLOSED BY HOST" && !sock(h).open, "read: then $0000 + 01, handle GONE (S6)");
    check(sock(h).srv.saw_notify, "read: the peer's close_notify is answered with ours (RFC 8446 6.1)");
    check(close_cmd(h).status == "12,ERROR ON CLOSE: " + num(EBADF), "read: CLOSE afterwards -> 12,…: 9");
    r = read_cmd(h, 10);
    check(header(r) == 0xFFFF && r.status == "02,NO DATA: " + num(EBADF), "read: READ afterwards -> 02,NO DATA: 9");

    // The peer closed TCP behind its close_notify: our answer finds a dead socket, which is not an error.
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, cat(record('D', 100, 7), record('C', 0)));
    sock(h).srv.send_err_from = sock(h).send_calls + 1;
    sock(h).srv.send_err_late = EPIPE;
    got = read_all(h, 1472, &last);
    check(got == expected(7, 100) && last == "01,CONNECTION CLOSED BY HOST",
          "read: our close_notify refused by a closed peer -> all bytes, then 01 (%s)", last.c_str());

    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('D', 3000, 6));
    server_push(h, EV_FIN, bytes());
    got = read_all(h, 1472, &last);
    check(got == expected(6, 3000) && last == "05,TLS CLOSED WITHOUT NOTIFY" && !sock(h).open,
          "read: 3000 B, then FIN -> all data, then 05, GONE (S7)");
    check(sock(h).srv.saw_notify, "read: 05 closes the live session with close_notify");

    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('D', 100, 7));
    uint8_t alert = 80;
    server_push(h, EV_DATA, bytes({ 'A', 1, 0, alert }));
    got = read_all(h, 1472, &last);
    check(got == expected(7, 100) && last == "14,TLS ALERT RECEIVED: 80", "read: data, then an alert -> 14");
    r = read_cmd(h, 10);
    check(header(r) == 0 && r.status == "14,TLS ALERT RECEIVED: 80", "read: sticky: $0000 + 14 again");
    r = write_cmd(h, S("x"));
    check(header(r) == 0xFFFF && r.status == "14,TLS ALERT RECEIVED: 80", "read: WRITE -> $FFFF + the same");
    check(info_byte(2) == 1, "read: INFO counts the sticky entry");
    world_module.calls.clear();
    check(close_cmd(h).status == "00,OK" && world_count(0x32) == 0 && !sock(h).open,
          "read: CLOSE -> 00,OK, no RPC (module freed it)");
    check(info_byte(2) == 0, "read: entry freed");

    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('B', 40));
    got = read_all(h, 1472, &last);
    check(last == "16,TLS SESSION FAILED: -0x7180", "read: a bad record -> 16 with the mbedTLS code (%s)",
          last.c_str());

    fresh();
    h = open_ok();
    world_module.forced.push_back(Forced());
    world_module.forced.back().result = WIFI_TLS_BUSY;
    server_push(h, EV_DATA, record('D', 10, 1));
    t0 = world_ms();
    r = read_cmd(h, 100);
    check(header(r) == 0 && r.status == "17,TLS MODULE NOT RESPONDING" && world_count(0x32) == 1,
          "read: a STEP timeout -> $0000 + 17, and CLOSE(slot, tag) at once");
    check(world_ms() - t0 <= 11100, "read: within READ_TOTAL_MS + CLOSE (%u ms)", world_ms() - t0);
    check(read_cmd(h, 10).status == "17,TLS MODULE NOT RESPONDING", "read: 17 is sticky");
    world_module.calls.clear();
    check(close_cmd(h).status == "00,OK" && world_count(0x32) == 0, "read: CLOSE after a cleanup CLOSE sends no RPC");

    fresh();
    h = open_ok();
    world_module.forced.push_back(Forced());
    world_module.forced.back().result = WIFI_TLS_REFUSED;
    world_module.forced.back().esp_err = WIFI_TLS_ESP_NOT_FOUND;
    server_push(h, EV_DATA, record('D', 10, 1));
    r = read_cmd(h, 100);
    check(r.status == "17,TLS MODULE NOT RESPONDING" && world_count(0x32) == 0,
          "read: NOT_FOUND -> 17, no CLOSE (the module has no session)");

    // A big record over many READs, and the byte-exact slicing (S13).
    int sizes[] = { 1, 100, 893, 894, 1472 };
    for (int i = 0; i < 5; i++) {
        fresh();
        h = open_ok();
        server_push(h, EV_DATA, record('D', 16000, 9));
        server_push(h, EV_DATA, record('C', 0));
        got = read_all(h, sizes[i], &last, NULL, 20000);
        check(got == expected(9, 16000) && last == "01,CONNECTION CLOSED BY HOST",
              "read: a 16 KB record with maxlen %d is byte exact (%d)", sizes[i], (int)got.size());
    }
    // A reply of more than one block carries the whole count in its header.
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('D', 960, 3));
    r = read_cmd(h, 1472);
    check(header(r) == 960 && body(r) == expected(3, 960) && r.status == "00,OK",
          "read: 960 B over two blocks, header 960, status once");

    // Tickets ahead of data: the READs go on stepping until the data arrives.
    fresh();
    h = open_ok();
    for (int i = 0; i < 10; i++) {
        server_push(h, EV_DATA, record('T', 200, i));
    }
    server_push(h, EV_DATA, record('D', 50, 4));
    got = read_all(h, 1472, &last, NULL, 60);
    check(got == expected(4, 50), "read: 10 tickets, then the data arrives (S13)");

    // READ never steps while rx is non-empty.
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('D', 900, 2));
    r = read_cmd(h, 100);
    int before = steps_logged();
    r = read_cmd(h, 100);
    bytes e900 = expected(2, 900);
    check(steps_logged() == before && body(r) == bytes(e900.begin() + 100, e900.begin() + 200),
          "read: the second READ comes from rx with no STEP");

    // AGAIN: the same input goes again, and nothing is lost.
    fresh();
    h = open_ok();
    world_module.again_next = 1;
    server_push(h, EV_DATA, record('D', 500, 8));
    got = read_all(h, 1472, &last, NULL, 5);
    check(got == expected(8, 500), "read: after AGAIN the same ciphertext is sent again (%d)", (int)got.size());

    // READ_BUDGET: at most 8 steps per READ.
    fresh();
    h = open_ok();
    for (int i = 0; i < 30; i++) {
        server_push(h, EV_DATA, record('T', 20, i));
    }
    before = steps_logged();
    r = read_cmd(h, 100);
    check(steps_logged() - before <= 8 && header(r) == 0xFFFF, "read: 8 steps at most, then NO DATA (%d steps)",
          steps_logged() - before);
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        if ((world_module.calls[i].cmd == 0x31) && (world_module.calls[i].flags != 0)) {
            check(false, "read: READ steps never carry NO_READ");
            break;
        }
    }
}

static void test_write(void)
{
    printf("WRITE on a TLS handle\n");
    fresh();
    int h = open_ok();
    int before = steps_logged();
    Res r = write_cmd(h, bytes());
    check(header(r) == 0 && r.status == "00,OK" && steps_logged() == before, "write: 0 bytes -> 0000, OK, no step");
    bytes data = expected(11, 892);
    world_module.calls.clear();
    r = write_cmd(h, data);
    check(header(r) == 892 && r.status == "00,OK", "write: 892 B -> header 892, 00,OK");
    check(sock(h).srv.got == data, "write: the server got exactly those bytes");
    bool no_read = true;
    int steps = 0;
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        const RpcCall &c = world_module.calls[i];
        if (c.cmd == 0x31) {
            steps++;
            no_read = no_read && (c.flags == WIFI_TLS_STEP_NO_READ) && (c.ct_in_len == 0);
        }
    }
    check(no_read && steps <= 4, "write: every step has NO_READ and ct_in_len 0 (%d steps)", steps);

    // WRITE between READs of one record.
    for (int w = 0; w < 2; w++) {
        fresh();
        h = open_ok();
        server_push(h, EV_DATA, record('D', 3000, 12));
        r = read_cmd(h, 100);
        bytes got = body(r);
        r = write_cmd(h, w ? expected(1, 892) : S("x"));
        check(r.status == "00,OK", "write: a %d-B WRITE mid-record -> OK", w ? 892 : 1);
        std::string last;
        bytes rest = read_all(h, 1472, &last, NULL, 20);
        got.insert(got.end(), rest.begin(), rest.end());
        check(got == expected(12, 3000), "write: the READs still give all 3000 B in order (%d)", (int)got.size());
    }

    // Budget, AGAIN, send failures, failures from the module.
    fresh();
    h = open_ok();
    world_module.again_next = 10;
    r = write_cmd(h, S("hello"));
    check(header(r) == 0xFFFF && r.status == "17,TLS MODULE NOT RESPONDING" && world_count(0x32) == 1,
          "write: AGAIN past WRITE_BUDGET -> $FFFF + 17, cleanup CLOSE");
    check(write_cmd(h, S("x")).status == "17,TLS MODULE NOT RESPONDING", "write: sticky");
    fresh();
    h = open_ok();
    world_module.again_next = 1;
    r = write_cmd(h, S("hello"));
    check(header(r) == 5 && sock(h).srv.got == S("hello"), "write: one AGAIN, sent again, once");
    fresh();
    h = open_ok();
    sock(h).send_err = EPIPE;
    r = write_cmd(h, S("hello"));
    check(header(r) == 0xFFFF && r.status == "12,SEND ERROR: " + num(EPIPE), "write: send error -> $FFFF + 12");
    r = read_cmd(h, 10);
    check(header(r) == 0 && r.status == "12,SEND ERROR: " + num(EPIPE), "write: then READ -> $0000 + the same 12");
    fresh();
    h = open_ok();
    sock(h).send_blocked = true;
    uint32_t t0 = world_ms();
    r = write_cmd(h, S("hello"));
    uint32_t spent = world_ms() - t0;
    check(header(r) == 0xFFFF && r.status == "12,SEND ERROR: " + num(ETIMEDOUT) && spent >= 5000 && spent <= 5300,
          "write: a stalled send -> 12,SEND ERROR: ETIMEDOUT after SEND_MS (%u ms, S32)", spent);
    fresh();
    h = open_ok();
    world_module.forced.push_back(Forced());
    world_module.forced.back().pt_on_no_read = true;
    r = write_cmd(h, S("hello"));
    check(r.status == "17,TLS MODULE NOT RESPONDING", "write: plaintext on a NO_READ step -> 17 (malformed)");
    fresh();
    h = open_ok();
    world_module.forced.push_back(Forced());
    world_module.forced.back().fail = true;
    world_module.forced.back().alert_in = 21;
    world_module.forced.back().fail_err = -0x7780;
    r = write_cmd(h, S("hello"));
    check(header(r) == 0xFFFF && r.status == "14,TLS ALERT RECEIVED: 21", "write: FAILED with an alert -> 14");
    fresh();
    h = open_ok();
    world_module.forced.push_back(Forced());
    world_module.forced.back().result = WIFI_TLS_BUSY;
    world_module.forced.back().applied = true;
    r = write_cmd(h, S("hello"));
    check(header(r) == 0xFFFF && r.status == "17,TLS MODULE NOT RESPONDING" && world_count(0x32) == 1,
          "write: a STEP timeout -> 17, cleanup CLOSE, never a partial count");
    // The peer's close_notify ended the session on the module (our reading of the protocol).
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, cat(record('D', 20, 1), record('C', 0)));
    r = read_cmd(h, 5);
    r = write_cmd(h, S("late"));
    check(header(r) == 0xFFFF && r.status == "12,SEND ERROR: " + num(EPIPE),
          "write: after the peer's close_notify -> $FFFF + 12,SEND ERROR: EPIPE (sticky)");
}

static void test_close(void)
{
    printf("CLOSE on a TLS handle\n");
    fresh();
    int h = open_ok();
    uint16_t tag = 0;
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        if (world_module.calls[i].cmd == 0x30) {
            tag = world_module.calls[i].open.tag;
        }
    }
    world_module.calls.clear();
    Res r = close_cmd(h);
    check(r.status == "00,OK" && r.data.empty(), "close: 00,OK");
    check(world_count(0x32) == 1 && call_no(0).slot == 0 && call_no(0).tag == tag,
          "close: CLOSE(slot, tag) RPC");
    check(sock(h).srv.saw_notify && !sock(h).open, "close: close_notify sent, then the socket closed");
    check(info_byte(2) == 0, "close: the entry is free");
    world_module.close_result = WIFI_TLS_BUSY;
    h = open_ok();
    r = close_cmd(h);
    check(r.status == "00,OK" && !sock(h).open, "close: 00,OK even when the module does not answer");
}

// --- latch-and-drain after a receive error -------------------------------

// An established session whose module holds n bytes of plaintext and then
// `terminal`, and whose socket answers the first recv with ECONNRESET.
static int latch_setup(size_t n, char terminal, uint8_t alert = 80)
{
    fresh();
    int h = open_ok();
    FakeSession &s = world_module.sess[0];
    for (size_t i = 0; i < n; i++) {
        s.pt.push_back(payload_byte(21, i));
    }
    s.terminal = terminal;
    s.alert = alert;
    server_push(h, EV_ERR, bytes(), ECONNRESET);
    return h;
}

static void test_latch(void)
{
    printf("READ: latch-and-drain after a receive error\n");
    const std::string rst = "12,RECEIVE ERROR: " + num(ECONNRESET);
    std::string last;
    int reads;
    int h = latch_setup(2500, 0);
    bytes got = read_all(h, 1472, &last, &reads, 50);
    check(got == expected(21, 2500), "latch: every held byte is delivered first (%d of 2500)", (int)got.size());
    check(last == rst, "latch: then $0000 + %s (got %s)", rst.c_str(), last.c_str());
    Res r = read_cmd(h, 10);
    check(header(r) == 0 && r.status == rst, "latch: sticky on the next READ");
    check(sock(h).recv_after_err == 0, "latch: no lwip_recv after the latch; the FIRST errno is kept");
    check(close_cmd(h).status == "00,OK" && world_count(0x32) >= 1, "latch: CLOSE -> 00,OK (module session freed)");

    h = latch_setup(2500, 'C');
    got = read_all(h, 1472, &last, NULL, 50);
    check(got == expected(21, 2500) && last == "01,CONNECTION CLOSED BY HOST" && !sock(h).open,
          "latch: close_notify held behind the data wins -> all bytes, then 01");

    h = latch_setup(2500, 'A', 80);
    got = read_all(h, 1472, &last, NULL, 50);
    check(got == expected(21, 2500) && last == "14,TLS ALERT RECEIVED: 80",
          "latch: a fatal alert held behind the data wins -> all bytes, then 14 (%s)", last.c_str());

    // One quiet input-less step does not end the drain.
    h = latch_setup(1500, 0);
    world_module.quiet_next = 1;
    got = read_all(h, 1472, &last, NULL, 50);
    check(got == expected(21, 1500) && last == rst,
          "latch: a quiet step while the module still holds plaintext does not end the drain (%d)", (int)got.size());

    // Quiet and plaintext steps alternating: plaintext breaks the run of quiet steps, so the drain goes on.
    h = latch_setup(3000, 0);
    world_module.quiet_alternate = true;
    got = read_all(h, 1472, &last, NULL, 50);
    check(got == expected(21, 3000) && last == rst,
          "latch: quiet steps between plaintext steps never add up to the end of the drain (%d)", (int)got.size());

    h = latch_setup(0, 0);
    r = read_cmd(h, 10);
    check(header(r) == 0 && r.status == rst, "latch: nothing held -> 12 at once");

    // (a) WRITE while latched: nothing sent; the end is fixed to that 12.
    h = latch_setup(2500, 'C');
    r = read_cmd(h, 100);
    bytes first = body(r);
    int sends = sock(h).send_calls;
    r = write_cmd(h, S("ping"));
    check(header(r) == 0xFFFF && r.status == rst && sock(h).send_calls == sends,
          "latch (a): WRITE while latched -> $FFFF + %s, no lwip_send", rst.c_str());
    got = read_all(h, 1472, &last, NULL, 50);
    first.insert(first.end(), got.begin(), got.end());
    check(first == expected(21, 2500) && last == rst && sock(h).open,
          "latch (a): READs deliver every byte and end in that 12, not 01, handle still owned");
    // (a2) the same with a fatal alert behind, READs of 1 byte.
    h = latch_setup(1200, 'A', 80);
    r = read_cmd(h, 1);
    first = body(r);
    write_cmd(h, S("ping"));
    got = read_all(h, 1, &last, NULL, 3000);
    first.insert(first.end(), got.begin(), got.end());
    check(first == expected(21, 1200) && last == rst,
          "latch (a2): alert behind the data after a WRITE's 12 -> every byte (%d), then 12 not 14",
          (int)first.size());
    // (a3) a record that fails its MAC.
    h = latch_setup(1200, 'B');
    r = read_cmd(h, 1);
    first = body(r);
    write_cmd(h, S("ping"));
    got = read_all(h, 1, &last, NULL, 3000);
    first.insert(first.end(), got.begin(), got.end());
    check(first == expected(21, 1200) && last == rst, "latch (a3): bad MAC behind -> every byte, then 12 not 16");
    // (b) the RST found by a WRITE first: the held plaintext is discarded.
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('D', 1200, 21));
    r = read_cmd(h, 10);
    sock(h).send_err = ECONNRESET;
    server_push(h, EV_ERR, bytes(), ECONNRESET);
    r = write_cmd(h, S("ping"));
    const std::string sent = "12,SEND ERROR: " + num(ECONNRESET);
    check(header(r) == 0xFFFF && r.status == sent, "latch (b): WRITE meets the RST -> $FFFF + %s", sent.c_str());
    r = read_cmd(h, 100);
    check(header(r) == 0 && r.status == sent, "latch (b): READ -> $0000 + the same 12, held plaintext discarded");
}

// --- claims, $25 and ABORT ---------------------------------------------

static void test_claims(void)
{
    printf("$25 TLS_RELEASE, claims and ABORT (S19e)\n");
    fresh();
    int a = open_ok();
    Res r = release();
    check(r.status == "00,OK" && r.data == B({ 1 }), "release: right after an OPEN -> 1");
    check(!sock(a).open && sock(a).srv.saw_notify && world_count(0x32) == 1,
          "release: CLOSE RPC, close_notify, socket closed");
    check(release().data == B({ 0 }), "release: again -> 0 (idempotent)");
    r = read_cmd(a, 10);
    check(header(r) == 0xFFFF && r.status == "02,NO DATA: " + num(EBADF), "release: READ on the number -> 02,NO DATA: 9");

    fresh();
    a = open_ok();
    check(info_byte(14) == 1, "claims: INFO $FF [14] = 1 after an OPEN");
    check(info_byte(14) == 1, "claims: $23 $FF does not claim");
    check(release().data == B({ 1 }), "claims: $23 $FF, then $25 -> 1");

    fresh();
    a = open_ok();
    read_cmd(a, 10);
    check(release().data == B({ 0 }) && sock(a).open, "claims: any command claims (READ) -> $25 answers 0");
    fresh();
    a = open_ok();
    run(B({ 0x03, 0x01 }));
    check(release().data == B({ 0 }), "claims: even IDENTIFY claims");
    fresh();
    a = open_ok();
    info(a);
    check(release().data == B({ 0 }), "claims: INFO on a handle claims");
    fresh();
    a = open_ok();
    r = run(B({ 0x03, 0x25, 0x00 }));
    check(r.status == "81,INVALID PARAMS" && info_byte(14) == 1, "claims: `03 25 00` -> 81, claims nothing");
    check(release().data == B({ 1 }), "claims: the corrected $25 still closes it");
    fresh();
    a = open_ok();
    run(B({ 0x03, 0x23, 0xFF, 0x00 }));
    check(release().data == B({ 0 }), "claims: `03 23 FF 00` claims");
    fresh();
    a = open_ok();
    run(B({ 0x03, 0x23 }));
    check(release().data == B({ 0 }), "claims: `03 23` claims");
    fresh();
    a = open_ok();
    r = run(B({ 0x83, 0x25 }));
    check(!sock(a).open, "claims: $25 with the no-reply bit still closes");

    // $25 spares claimed sessions (S19e2) and TCP sockets.
    fresh();
    int t = open_tcp();
    a = open_ok();
    read_cmd(a, 10);
    int b = open_ok();
    check(release().data == B({ 1 }) && sock(a).open && !sock(b).open && sock(t).open,
          "claims: A claimed, B unclaimed -> $25 closes B only, TCP untouched");
    sock(a).srv.echo = true;
    r = write_cmd(a, S("still"));
    std::string last;
    bytes got = read_all(a, 100, &last, NULL, 5);
    check(got == S("still"), "claims: A still echoes");
    // START_TLS creates an unclaimed entry too.
    fresh();
    t = open_tcp();
    run(start_cmd(t, 2, 0, "", PIN()));
    check(release().data == B({ 1 }) && !sock(t).open, "claims: START_TLS's entry is unclaimed until the next command");

    // ABORT never closes, frees or claims; does not touch a CA upload.
    fresh();
    a = open_ok();
    net.abort(0);
    check(sock(a).open && info_byte(14) == 1, "abort: the session stays, and stays unclaimed");
    check(release().data == B({ 1 }), "abort: $25 after an ABORTed OPEN closes it (S19e)");
    fresh();
    world_module.set_ca_result = 0;
    run(cat(B({ 0x03, 0x22, 0, 0, 0x64, 0 }), bytes(50, 0x30)));
    net.abort(0);
    r = run(cat(B({ 0x03, 0x22, 50, 0, 0x64, 0 }), bytes(50, 0x31)));
    check(r.status == "00,OK" && world_count(0x35) == 1, "abort: an ABORT between CA chunks does not end the upload");
}

// --- TRUST_CA ($22) ---------------------------------------------------------------------------

static Res chunk(uint16_t offset, uint16_t total, const bytes &data)
{
    return run(cat(B({ 0x03, 0x22, offset & 0xFF, offset >> 8, total & 0xFF, total >> 8 }), data));
}

static bool loaded(void)
{
    Res r = info(0xFF);
    return (r.data.size() == 16) && (r.data[4] & 0x08);
}

static void test_ca(void)
{
    printf("TRUST_CA ($22): the Nios's CA state\n");
    fresh();
    world_module.set_ca_result = 0;
    bytes der = expected(33, 1400);
    Res r = chunk(0, 1400, bytes(der.begin(), der.begin() + 600));
    check(r.status == "00,OK" && r.data.empty(), "ca: first chunk -> 00,OK, empty");
    r = chunk(600, 1400, bytes(der.begin() + 600, der.begin() + 1200));
    check(r.status == "00,OK" && world_count(0x35) == 0, "ca: an intermediate chunk sends nothing");
    r = chunk(1200, 1400, bytes(der.begin() + 1200, der.end()));
    check(r.status == "00,OK" && world_count(0x35) == 1 && world_module.ca_der == der,
          "ca: final chunk -> SET_CA with all 1400 bytes -> LOADED");
    uint16_t tag = world_module.ca_tag;
    check(tag != 0 && loaded(), "ca: INFO bit3 set, ca_tag %u != 0", tag);
    world_module.calls.clear();
    int h = open_ok(1);
    check(h >= 0 && call_no(0).open.ca_tag == tag, "ca: OPEN CUSTOM carries the ca_tag");
    close_cmd(h);
    r = chunk(1200, 1400, bytes(der.begin() + 1200, der.end()));
    check(r.status == "81,INVALID PARAMS" && loaded(), "ca: a duplicate final chunk -> 81, still LOADED");
    r = chunk(700, 1473, bytes(10, 1));
    check(r.status == "81,INVALID PARAMS" && loaded(), "ca: total > 1472 at a non-zero offset -> 81, CA untouched");
    r = chunk(0, 1473, bytes());
    check(r.status == "82,PARAMETER(S) OUT OF RANGE" && !loaded(), "ca: offset 0, total 1473 over a LOADED CA -> 82, EMPTY");
    chunk(0, 10, bytes(10, 1));
    uint16_t old_tag = world_module.ca_tag;
    check(loaded(), "ca: loaded again");
    world_module.calls.clear();
    chunk(0, 10, bytes(10, 2));
    check(last_call().cmd == 0x35 && last_call().tag != old_tag, "ca: a new upload is sent under a new ca_tag (%u, was %u)",
          last_call().tag, old_tag);
    r = chunk(0, 1400, bytes(10, 1));
    check(!loaded(), "ca: offset 0 of a new upload drops the CA at once");
    r = run(cat(B({ 0x03, 0x22, 10, 0, 0x78, 0x05 }), bytes(10, 2)));
    check(r.status == "81,INVALID PARAMS", "ca: (the INFO above ended that upload) a continuation -> 81");

    fresh();
    world_module.set_ca_result = 0;
    chunk(0, 100, bytes(50, 1));
    info(0xFF);
    r = chunk(50, 100, bytes(50, 1));
    check(r.status == "81,INVALID PARAMS" && world_count(0x35) == 0, "ca: chunk, INFO, chunk -> 81, EMPTY (interleave)");
    chunk(0, 100, bytes(50, 1));
    release();
    check(chunk(50, 100, bytes(50, 1)).status == "81,INVALID PARAMS", "ca: $25 ends an upload too");
    chunk(0, 100, bytes(50, 1));
    check(chunk(40, 100, bytes(50, 1)).status == "81,INVALID PARAMS", "ca: a wrong offset -> 81");
    check(chunk(50, 100, bytes(50, 1)).status == "81,INVALID PARAMS", "ca: ... and the upload is gone");
    chunk(0, 100, bytes(50, 1));
    check(chunk(50, 100, bytes(51, 1)).status == "81,INVALID PARAMS", "ca: a chunk past total -> 81");
    check(chunk(0, 0, bytes(3, 1)).status == "81,INVALID PARAMS", "ca: total 0 with data -> 81");
    check(chunk(0, 1473, bytes()).status == "82,PARAMETER(S) OUT OF RANGE", "ca: offset 0, total 1473 -> 82");
    check(run(B({ 0x03, 0x22, 0, 0, 0 })).status == "81,INVALID PARAMS", "ca: 5 bytes -> 81");
    chunk(0, 10, bytes(10, 1));
    check(loaded(), "ca: a one-chunk upload loads");
    uint16_t before = world_module.ca_tag;
    check(chunk(0, 0, bytes()).status == "00,OK" && !loaded(), "ca: offset 0 total 0 clears -> EMPTY");
    world_module.calls.clear();
    chunk(0, 10, bytes(10, 1));
    check(last_call().tag != before, "ca: every new upload has a new ca_tag");

    struct { int result; int err; uint8_t reject; const char *want; } cases[] = {
        { WIFI_TLS_REFUSED, -0x2180, 0, "97,CUSTOM CA INVALID: -0x2180" },
        { WIFI_TLS_REFUSED, WIFI_TLS_ESP_INVALID_ARG, 1, "97,CUSTOM CA REJECTED: RSA KEY < 2048" },
        { WIFI_TLS_REFUSED, WIFI_TLS_ESP_INVALID_ARG, 3, "97,CUSTOM CA REJECTED: CURVE" },
        { WIFI_TLS_REFUSED, WIFI_TLS_ESP_INVALID_ARG, 4, "97,CUSTOM CA REJECTED: NOT A CA" },
        { WIFI_TLS_REFUSED, WIFI_TLS_ESP_NOT_SUPPORTED, 0, "90,TLS NOT AVAILABLE" },
        { WIFI_TLS_INVALID, 0, 0, "17,TLS MODULE NOT RESPONDING" },
        { WIFI_TLS_BUSY, 0, 0, "17,TLS MODULE NOT RESPONDING" },
        { WIFI_TLS_NO_MODULE, 0, 0, "90,TLS NOT AVAILABLE" },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fresh();
        world_module.set_ca_result = cases[i].result;
        world_module.set_ca_esp_err = cases[i].err;
        world_module.set_ca_reject = cases[i].reject;
        r = chunk(0, 20, bytes(20, 7));
        check(r.status == cases[i].want && !loaded(), "ca: SET_CA answer %d/%d/%d -> %s, EMPTY (got %s)",
              cases[i].result, cases[i].err, cases[i].reject, cases[i].want, r.status.c_str());
    }
    fresh();
    world_module.available = false;
    r = chunk(0, 20, bytes(20, 7));
    check(r.status == "90,TLS NOT AVAILABLE", "ca: no module at the final chunk -> 90");
}

// --- c64_reset, run_prg and module restarts ---------------------------------------------

static void test_cleanup(void)
{
    printf("Cleanup: C64 reset and module restart (S14)\n");
    fresh();
    world_module.set_ca_result = 0;
    chunk(0, 10, bytes(10, 1));
    int a = open_ok(1), b = open_ok(0);
    world_module.calls.clear();
    net.c64_reset();
    check(!sock(a).open && !sock(b).open, "reset: both TLS sockets closed");
    check(world_module.close_alls == 1, "reset: CLOSE_ALL once");
    Res r = info(0xFF);
    check(at(r, 2) == 0 && !(at(r, 4) & 0x08), "reset: no entries, CA bit clear");
    world_module.ca_loaded = true; // CLOSE_ALL lost: the module still has the old CA
    world_module.calls.clear();
    r = run(open_cmd(1, 0, "example.com"));
    check(r.status == "97,NO CUSTOM CA" && world_module.calls.empty(), "reset: OPEN CUSTOM -> 97, even if CLOSE_ALL was lost");
    a = open_ok(0);
    check(a >= 0 && last_call().slot <= 1, "reset: the table is usable again");

    fresh();
    world_module.set_ca_result = 0;
    chunk(0, 10, bytes(10, 1));
    a = open_ok();
    world_module.generation++;
    r = read_cmd(a, 10);
    check(header(r) == 0 && r.status == "17,TLS MODULE NOT RESPONDING", "restart: a new generation -> $0000 + 17");
    check(!loaded(), "restart: the CA is EMPTY");
    world_module.calls.clear();
    check(close_cmd(a).status == "00,OK" && world_count(0x32) == 0 && !sock(a).open,
          "restart: CLOSE sends no RPC and closes the socket");
}

// Every TLS status line is at most 40 bytes. The longest
// templates, with the widest values each field can take (a 3-digit newlib
// errno, 4 hex digits of mbedTLS code, 8 of flags).
static void test_status_lengths(void)
{
    printf("Status lines: at most 40 bytes\n");
    const int E = 999;
    const uint8_t N = WIFI_TLS_ALERT_NONE;
    fresh();
    world_socks[0].srv.on_hello = SRV_RST;
    world_socks[0].srv.rst_err = E;
    Res r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: PEER RESET: 999" && r.status.size() == 40,
          "status cap: '%s' is %d bytes", r.status.c_str(), (int)r.status.size());
    fresh();
    world_socks[0].srv.send_err = E;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: 999" && r.status.size() == 40,
          "status cap: '%s' is %d bytes (SEND ERROR wording, 3-digit errno)", r.status.c_str(), (int)r.status.size());
    expect_fail(0, 4, 0xFFFFFFFE, N, -0x2700, "94,CERTIFICATE NAME MISMATCH: 0xFFFFFFFE", "widest 94");
    expect_fail(0, 4, 0xFFFFFFFF, N, -0x2700, "93,CERTIFICATE NOT TRUSTED: 0xFFFFFFFF", "widest 93");
    expect_fail(0, 4, 0xFFFFFFFB, N, -0x2700, "95,CERTIFICATE EXPIRED: 0xFFFFFFFB", "widest 95");
    expect_fail(0, 0, 0, N, -0xFFFF, "13,TLS HANDSHAKE FAILED: -0xFFFF", "widest 13");
    expect_fail(0, 0, 0, 254, -0x7780, "14,TLS ALERT RECEIVED: 254", "widest 14 (255 means none)");
    expect_fail(0, 1, 0xFFFF, N, -0x7700, "18,TLS VERSION REFUSED: 0xFFFF", "widest 18");
    fresh();
    world_socks[0].srv.connect_err = E;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "11,ERROR ON CONNECT: 999", "status cap: %s", r.status.c_str());
    fresh();
    int h = open_ok();
    server_push(h, EV_ERR, bytes(), E);
    r = read_cmd(h, 10);
    check(r.status == "12,RECEIVE ERROR: 999", "status cap: %s", r.status.c_str());
    fresh();
    h = open_ok();
    sock(h).send_err = E;
    r = write_cmd(h, S("x"));
    check(r.status == "12,SEND ERROR: 999", "status cap: %s", r.status.c_str());
    fresh();
    world_module.set_ca_result = WIFI_TLS_REFUSED;
    world_module.set_ca_esp_err = -0xFFFF;
    r = chunk(0, 5, bytes(5, 1));
    check(r.status == "97,CUSTOM CA INVALID: -0xFFFF", "status cap: %s", r.status.c_str());
    world_module.set_ca_esp_err = WIFI_TLS_ESP_INVALID_ARG;
    world_module.set_ca_reject = 1;
    r = chunk(0, 5, bytes(5, 1));
    check(r.status == "97,CUSTOM CA REJECTED: RSA KEY < 2048", "status cap: %s", r.status.c_str());
    fresh();
    h = open_ok();
    server_push(h, EV_DATA, record('B', 10));
    r = read_cmd(h, 10);
    check(r.status.compare(0, 23, "16,TLS SESSION FAILED: ") == 0, "status cap: %s", r.status.c_str());
}

// A READ on a TLS handle refused before any data (81 wrong
// length, 82 maxlen) has an EMPTY reply, which reads as $00 $00, and changes
// nothing.
static void test_empty_read_reply(void)
{
    printf("READ refusals on a TLS handle: an empty reply, the handle unchanged\n");
    fresh();
    int h = open_ok();
    server_push(h, EV_DATA, record('D', 30, 13));
    Res r = run(B({ 0x03, 0x10, h, 10 }));
    check(r.status == "81,INVALID PARAMS" && r.data.empty(), "empty reply: a 4-byte READ -> 81, empty reply");
    r = run(B({ 0x03, 0x10, h, 10, 0, 0 }));
    check(r.status == "81,INVALID PARAMS" && r.data.empty(), "empty reply: a 6-byte READ -> 81, empty reply");
    r = read_cmd(h, 0);
    check(r.status == "82,PARAMETER(S) OUT OF RANGE" && r.data.empty(), "empty reply: maxlen 0 -> 82, empty reply");
    r = read_cmd(h, 1473);
    check(r.status == "82,PARAMETER(S) OUT OF RANGE" && r.data.empty(), "empty reply: maxlen 1473 -> 82, empty reply");
    std::string last;
    bytes got = read_all(h, 1472, &last, NULL, 5);
    check(got == expected(13, 30) && sock(h).open, "empty reply: the handle is unchanged; its data still arrives");
}


// --- edge cases: latch, STATS cache, drain, WRITE budget, send failures -------------------------
static void test_edge_cases(void)
{
    printf("Edge cases: latch, STATS cache, drain, WRITE budget, send failures\n");
    const std::string rst = "12,RECEIVE ERROR: " + num(ECONNRESET);
    std::string last;

    // A close_notify found by the drain BEFORE the WRITE, with plaintext
    // still in rx. The WRITE reports the latched 12; the end must then be that 12, never 01/GONE.
    fresh();
    int h = open_ok();
    FakeSession &s = world_module.sess[0];
    for (size_t i = 0; i < 500; i++) {
        s.pt.push_back(payload_byte(21, i));
    }
    s.terminal = 'C';
    server_push(h, EV_ERR, bytes(), ECONNRESET);
    Res r = read_cmd(h, 100);                // latches; the step hands out 500 B AND CLOSED
    bytes all = body(r);
    r = write_cmd(h, S("ping"));
    check(header(r) == 0xFFFF && r.status == rst, "latch (c): WRITE while latched -> $FFFF + %s (got %s)",
          rst.c_str(), r.status.c_str());
    bytes got = read_all(h, 100, &last, NULL, 50);
    all.insert(all.end(), got.begin(), got.end());
    check(all == expected(21, 500), "latch (c): every byte delivered (%d of 500)", (int)all.size());
    check(last == rst && sock(h).open,
          "latch (c): after a WRITE said 12, the end is that 12 and the handle is still owned (got '%s', socket %s)",
          last.c_str(), sock(h).open ? "open" : "CLOSED");

    // The STATS cache is cleared only on NO_MODULE, NOT_SUPPORTED or a new
    // generation. The ESP32 answers a STATS that finds the TLS queue full with a full-size reply,
    // esp_err ESP_ERR_TIMEOUT (tls_rpc.c cmd_tls_busy), which take_stats makes REFUSED.
    fresh();
    info(0xFF);
    world_module.stats_result = WIFI_TLS_REFUSED; // cmd_tls_busy's full-size reply
    world_module.stats_esp_err = 0x107;           // ESP_ERR_TIMEOUT
    r = info(0xFF);
    world_module.stats_result = -1;
    world_module.stats_esp_err = 0;
    check((at(r, 4) & 0x01) == 0x01,
          "info (cache): a STATS refused with ESP_ERR_TIMEOUT (TLS queue full = a timeout) keeps b0 (b4=0x%02X)", at(r, 4));

    // The drain ends after TWO CONSECUTIVE quiet steps, not two per READ. With
    // every step taking 260 ms (> READ_BUDGET 250 ms), each READ gets one step in.
    fresh();
    h = open_ok();
    server_push(h, EV_ERR, bytes(), ECONNRESET);
    for (int i = 0; i < 60; i++) {
        Forced f = Forced();
        f.result = 0;
        f.delay_ms = 260;
        world_module.forced.push_back(f);
    }
    int reads = 0;
    std::string st;
    for (reads = 1; reads <= 20; reads++) {
        r = read_cmd(h, 100);
        st = r.status;
        if (header(r) != 0xFFFF) {
            break;
        }
    }
    world_module.forced.clear();
    check(st == rst, "latch: a peer RST with slow (260 ms) steps ends in %s within 20 READs (got '%s' after %d)",
          rst.c_str(), st.c_str(), reads > 20 ? 20 : reads);

    // A WRITE ends with a step that returns no
    // ciphertext, so 892 B is exactly 2 NO_READ steps; within WRITE_BUDGET 4 that leaves room for 2
    // AGAINs, and a third makes the WRITE a sticky 17, never a partial count.
    fresh();
    h = open_ok();
    bytes data = expected(12, 892);
    world_module.calls.clear();
    r = write_cmd(h, data);
    check(header(r) == 892 && steps_logged() == 2 && last_call().pt_in_len == 0,
          "write: 892 B is 2 steps, the last input-less (got %04X, %d steps)", header(r), steps_logged());
    fresh();
    h = open_ok();
    world_module.again_next = 2;
    r = write_cmd(h, data);
    check(header(r) == 892 && r.status == "00,OK" && sock(h).srv.got == data,
          "write: 892 B after 2 AGAINs is 4 steps -> 892, 00,OK (got %04X %s)", header(r), r.status.c_str());
    fresh();
    h = open_ok();
    world_module.again_next = 3;
    r = write_cmd(h, data);
    check(header(r) == 0xFFFF && r.status == "17,TLS MODULE NOT RESPONDING",
          "write: 892 B after 3 AGAINs runs out of WRITE_BUDGET -> $FFFF + sticky 17 (got %04X %s)", header(r),
          r.status.c_str());

    // A send failure during the handshake is a 13; 12 is for READ and WRITE only.
    fresh();
    int t = open_tcp();
    sock(t).send_err = ECONNRESET;
    r = run(start_cmd(t, 2, 0, "", PIN()));
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: " + num(ECONNRESET) && !sock(t).open,
          "start: START_TLS whose ClientHello send meets an RST -> 13 SEND ERROR: errno, handle closed "
          "(got '%s', handle %s)", r.status.c_str(), sock(t).open ? "open" : "closed");
    fresh();
    world_socks[0].srv.send_err = EPIPE; // OPEN_TLS: the new socket's first send fails
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: " + num(EPIPE) && world_open_sockets() == 0 &&
          world_count(0x32) == 1, "open: OPEN_TLS whose ClientHello send fails -> 13 SEND ERROR: errno, CLOSE RPC (got '%s')",
          r.status.c_str());

    // 17 is not a kept code. The module's queue-full answer to OPEN (esp_err
    // ESP_ERR_TIMEOUT, reason 0) is a REFUSED 17: START_TLS must close the TCP handle.
    fresh();
    t = open_tcp();
    world_module.open_result = WIFI_TLS_REFUSED;
    world_module.open_esp_err = 0x107;
    world_module.open_reason = 0;
    r = run(start_cmd(t, 2, 0, "", PIN()));
    world_module.open_result = -1;
    check(r.status == "17,TLS MODULE NOT RESPONDING" && !sock(t).open,
          "start: START_TLS refused by a busy module -> 17 and the TCP handle is closed (got '%s', %s)",
          r.status.c_str(), sock(t).open ? "open" : "closed");
}

// Send failures late in the handshake, NO_MODULE from STATS, and a module failure in the drain
// after a WRITE's 12.
static void test_late_send_failures(void)
{
    printf("Late handshake send failures, STATS NO_MODULE, a drain failure after a WRITE\n");
    Res r;

    // A STATS call that finds the module application gone (NO_MODULE, the race with
    // wifi_tls_module_available()) clears the cache.
    fresh();
    info(0xFF);
    world_module.stats_result = WIFI_TLS_NO_MODULE;
    r = info(0xFF);
    world_module.stats_result = WIFI_TLS_BUSY;
    Res r2 = info(0xFF);
    world_module.stats_result = -1;
    check(!(at(r, 4) & 1) && !(at(r2, 4) & 1), "info (cache): NO_MODULE from STATS clears b0, and it stays 0 (b4=0x%02X, 0x%02X)",
          at(r, 4), at(r2, 4));

    // The Finished's send fails (the 2nd send of the handshake) -> 13 SEND ERROR, never 12,
    // never 00,OK.
    fresh();
    world_socks[0].srv.send_err_from = 2;
    world_socks[0].srv.send_err_late = ECONNRESET;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: " + num(ECONNRESET) && world_open_sockets() == 0,
          "open: OPEN whose Finished send meets an RST -> 13 SEND ERROR (got '%s')", r.status.c_str());
    fresh();
    int t = open_tcp();
    sock(t).srv.send_err_from = sock(t).send_calls + 2;
    sock(t).srv.send_err_late = EPIPE;
    r = run(start_cmd(t, 2, 0, "", PIN()));
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: " + num(EPIPE) && !sock(t).open,
          "start: START whose Finished send fails -> 13 SEND ERROR, handle closed (got '%s')", r.status.c_str());

    // A ClientHello send stalled for SEND_MS, well before the handshake deadline ->
    // 13 SEND ERROR: ETIMEDOUT (116 on the Nios); a Finished send stalled into the deadline -> 15.
    fresh();
    world_socks[0].srv.send_err_from = 1;
    world_socks[0].srv.send_err_late = EWOULDBLOCK;
    uint32_t t0 = world_ms();
    r = run(open_cmd(0, 0, "example.com"));
    uint32_t spent = world_ms() - t0;
    check(r.status == "13,TLS HANDSHAKE FAILED: SEND ERROR: " + num(ETIMEDOUT) && spent >= 5000 && spent < 6000,
          "open: a stalled ClientHello send -> 13 SEND ERROR: ETIMEDOUT after SEND_MS (got '%s', %u ms)",
          r.status.c_str(), spent);
    fresh();
    world_socks[0].srv.delay_ms = 12000;
    world_socks[0].srv.send_err_from = 2;
    world_socks[0].srv.send_err_late = EWOULDBLOCK;
    r = run(open_cmd(0, 0, "example.com"));
    check(r.status == "15,TLS HANDSHAKE TIMEOUT", "open: a Finished send stalled into the handshake deadline -> 15 (got '%s')",
          r.status.c_str());

    // Pinned as the current behaviour: after a WRITE has reported the latched 12, a module
    // failure in the drain (here a malformed step) ends in a sticky 17, not 12; the handle stays owned.
    fresh();
    int h = open_ok();
    for (size_t i = 0; i < 1200; i++) {
        world_module.sess[0].pt.push_back(payload_byte(21, i));
    }
    server_push(h, EV_ERR, bytes(), ECONNRESET);
    read_cmd(h, 1);
    r = write_cmd(h, S("ping"));
    check(r.status == "12,RECEIVE ERROR: " + num(ECONNRESET), "latch: setup: the WRITE reports the latched 12");
    Forced f = Forced();
    f.result = WIFI_TLS_INVALID;
    world_module.forced.push_back(f);
    std::string last;
    read_all(h, 1472, &last, NULL, 50);
    r = read_cmd(h, 10);
    check(last == "17,TLS MODULE NOT RESPONDING" && header(r) == 0 && r.status == last && sock(h).open,
          "latch: a module failure in the drain after the WRITE's 12 -> sticky 17, handle still owned (got '%s')",
          last.c_str());
    check(close_cmd(h).status == "00,OK" && !sock(h).open, "latch: CLOSE -> 00,OK");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_info();
    test_open();
    test_grammar();
    test_precedence();
    test_classify();
    test_dns_connect();
    test_reset_during_open();
    test_start_tls();
    test_read();
    test_write();
    test_close();
    test_latch();
    test_claims();
    test_ca();
    test_cleanup();
    test_status_lengths();
    test_empty_read_reply();
    test_edge_cases();
    test_late_send_failures();
    check(longest_status.size() <= 40, "status cap: the longest status of the whole run is %d bytes: '%s'",
          (int)longest_status.size(), longest_status.c_str());
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
