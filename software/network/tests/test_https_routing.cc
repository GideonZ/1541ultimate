#include <string>
#include <vector>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "http_target.h"

static int checks, failures;
static void expect(bool ok, const char *label)
{
    ++checks;
    if (!ok) { ++failures; printf("FAIL %s\n", label); }
}

// Link wrappers make any plaintext route observable without opening a socket.
static int dns_calls, socket_calls, plain_closes;
static std::string wire, response, host;
static unsigned offset;
static uint16_t selected_port;
extern "C" int __wrap_gethostbyname_r(const char *name, hostent *entry, char *, size_t, hostent **result, int *)
{
    static char address[4] = {127, 0, 0, 1};
    static char *addresses[] = {address, NULL};
    ++dns_calls; host = name;
    entry->h_addr_list = addresses; entry->h_length = 4;
    *result = entry;
    return 0;
}
extern "C" int __wrap_socket(int, int, int) { ++socket_calls; return 901; }
extern "C" int __wrap_connect(int fd, const sockaddr *address, socklen_t)
{
    expect(fd == 901, "plain connection descriptor");
    selected_port = ntohs(reinterpret_cast<const sockaddr_in *>(address)->sin_port);
    return 0;
}
static int collect(const void *data, int size)
{
    int n = size > 7 ? 7 : size;
    wire.append(static_cast<const char *>(data), n);
    return n;
}
static int deliver(void *data, int size)
{
    int n = response.size() - offset;
    if (n > size) n = size;
    if (n > 31) n = 31;
    memcpy(data, response.data() + offset, n);
    offset += n;
    return n;
}
extern "C" ssize_t __wrap_send(int, const void *data, size_t size, int) { return collect(data, size); }
extern "C" ssize_t __wrap_recv(int, void *data, size_t size, int) { return deliver(data, size); }
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd)
{
    if (fd == 901) { ++plain_closes; return 0; }
    return __real_close(fd);
}

struct Provider : HttpSecureProvider {
    enum Mode { Good, Missing, OpenError, WriteError, ReadError } mode;
    int created, opened, closed, destroyed;
    CancellationSource *interrupt;
    int signal_at;
    OperationCancellation cancellation;
    CancellationSource epoch;
    bool online;
    Provider(Mode m = Good) : mode(m), created(0), opened(0), closed(0), destroyed(0), interrupt(NULL), signal_at(0), online(true) {}
    bool available() const override { return online; }
    OperationCancellation lifetime() const override { return epoch.token(); }
    class Connection : public HttpConnection {
        Provider& owner;
        bool live;
        OperationCancellation lifetime;
    public:
        Connection(Provider& p) : owner(p), live(false), lifetime(p.lifetime()) {}
        ~Connection() { close_session(); ++owner.destroyed; }
        int open(const char *name, uint16_t port) {
            if (owner.signal_at == 1) owner.interrupt->signal();
            live = true; ++owner.opened; host = name; selected_port = port;
            return owner.mode == OpenError || owner.cancellation.cancelled() || lifetime.cancelled() ? -1 : 0;
        }
        int read(void *p, int n) {
            int result = owner.mode == ReadError ? -1 : deliver(p, n);
            // Deliberately return data despite cancellation to exercise the
            // target's final check independently of a cooperative provider.
            if (owner.signal_at == 3) owner.interrupt->signal();
            return result;
        }
        int write(const void *p, int n) {
            if (owner.signal_at == 2) owner.interrupt->signal();
            return owner.mode == WriteError || owner.cancellation.cancelled() || lifetime.cancelled() ? -1 : collect(p, n);
        }
        void close_session() { if (live) { live = false; ++owner.closed; } }
    };
    HttpConnection *create_connection(const OperationCancellation& token) {
        cancellation = token;
        ++created;
        if (signal_at == 4) epoch.signal();
        return mode == Missing ? NULL : new Connection(*this);
    }
};

static void reset(const std::string& body, const char *status = "200 OK")
{
    dns_calls = socket_calls = plain_closes = 0;
    wire.clear(); host.clear(); offset = selected_port = 0;
    response = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body;
}
struct Reply { std::string data, status; bool last; };
static CancellationSource *before_publication;
static Reply snapshot(Message *data, Message *status)
{
    expect(data->length <= HTTP_MAX_REPLY_BYTES && status->length <= HTTP_MAX_STATUS_BYTES, "channel bounds");
    return {std::string(reinterpret_cast<char *>(data->message), data->length),
            std::string(reinterpret_cast<char *>(status->message), status->length), data->last_part};
}
static Reply command(HttpTarget& target, std::vector<uint8_t> bytes)
{
    Message cmd = {int(bytes.size()), true, bytes.data()}, *data, *status;
    target.parse_command(&cmd, &data, &status);
    if (before_publication) { before_publication->signal(); before_publication = NULL; }
    target.prepare_reply(&data, &status);
    auto result = snapshot(data, status);
    target.finish_reply(true);
    return result;
}
static uint8_t header(HttpTarget& target, const std::string& url, int verb = 1)
{
    std::vector<uint8_t> bytes = {6, HTTP_CMD_HEADER_CREATE, uint8_t(verb)};
    bytes.insert(bytes.end(), url.begin(), url.end());
    Reply r = command(target, bytes);
    expect(r.status == "000 OK" && r.data.size() == 1, "create legacy header handle");
    return r.data.empty() ? 0 : uint8_t(r.data[0]);
}
static Reply exchange(HttpTarget& target, uint8_t hdr, bool raw, uint8_t body = 255)
{
    return command(target, {6, uint8_t(raw ? HTTP_CMD_DO_EXCHANGE_RAW : HTTP_CMD_DO_EXCHANGE_OBJ), hdr, body});
}
static Reply more(HttpTarget& target)
{
    Message *data, *status;
    target.get_more_data(&data, &status);
    if (before_publication) { before_publication->signal(); before_publication = NULL; }
    target.prepare_reply(&data, &status);
    auto result = snapshot(data, status);
    target.finish_reply(true);
    return result;
}
static void failure_cases()
{
    for (bool raw : {false, true}) {
        for (int kind = -1; kind <= Provider::ReadError; ++kind) {
            if (kind == Provider::Good) continue;
            reset("{\"ok\":1}");
            Provider provider(kind < 0 ? Provider::Good : Provider::Mode(kind));
            HttpTarget target(7, kind < 0 ? NULL : &provider);
            auto hdr = header(target, "https://tls.lab.test/probe");
            Reply r = exchange(target, hdr, raw);
            expect(r.status == "503 SERVICE UNAVAILABLE" && r.data.empty() && r.last, "secure failure has no response body");
            expect(dns_calls == 0 && socket_calls == 0, "secure failure never falls back to plaintext");
            expect(provider.closed == provider.opened && provider.destroyed == (provider.mode == Provider::Missing ? 0 : provider.created), "failed provider destroyed exactly once");
            expect(more(target).data.empty(), "no stale bytes after secure failure");
            // A later explicit HTTP request remains available after a secure error.
            auto plain = header(target, "http://tls.lab.test/probe");
            r = exchange(target, plain, true);
            expect(r.data == "{\"ok\":1}" && socket_calls == 1, "explicit HTTP recovery");
        }
    }
}
static void scheme_cases()
{
    for (const char *url : {"https://tls.lab.test/p", "HTTPS://tls.lab.test/p", "hTtPs://tls.lab.test:8443/p"}) {
        reset("x"); Provider provider; HttpTarget target(7, &provider);
        auto r = exchange(target, header(target, url), true);
        expect(r.data == "x" && provider.created == 1 && !dns_calls && !socket_calls, "case-insensitive HTTPS routing");
        expect(selected_port == (strstr(url, "8443") ? 8443 : 443) && host == "tls.lab.test", "secure authority passed to provider");
    }
    for (const char *url : {"https:/tls.lab.test/p", "https:tls.lab.test", "ftp://tls.lab.test/p", "https:///p",
                           "https://tls.lab.test:0/p", "https://tls.lab.test:65536/p", "https://tls.lab.test:44x/p",
                           "https://tls.lab.test:/p", "https://user@tls.lab.test/p", "https://bad host/p"}) {
        reset("x"); Provider provider; HttpTarget target(7, &provider);
        auto r = exchange(target, header(target, url), true);
        expect(r.status == "500 BAD FORMAT" && r.data.empty(), "invalid secure URL rejected");
        expect(!dns_calls && !socket_calls && !provider.created, "invalid URL does not open any transport");
    }
    for (const char *url : {"http://tls.lab.test/p", "tls.lab.test/p", "http://tls.lab.test:8080/p", "tls.lab.test/p?next=https://other/p"}) {
        reset("x"); Provider provider; HttpTarget target(7, &provider);
        auto r = exchange(target, header(target, url), true);
        expect(r.data == "x" && !provider.created && dns_calls == 1 && socket_calls == 1 && plain_closes == 1, "legacy HTTP routing unchanged");
        expect(selected_port == (strstr(url, "8080") ? 8080 : 80), "legacy default and explicit HTTP port");
    }
}
static void parity_and_cleanup()
{
    for (bool raw : {false, true}) {
        std::string plain_wire, plain_data, plain_status, plain_query;
        for (bool secure : {false, true}) {
            std::string body = raw ? std::string(HTTP_MAX_REPLY_BYTES * 2, 'x') : "{\"answer\":42}";
            reset(body, "503 Service Unavailable");
            Provider provider;
            {
                HttpTarget target(7, &provider);
                auto hdr = header(target, std::string(secure ? "https://" : "http://") + "tls.lab.test/probe", 3);
                Reply b = command(target, {6, HTTP_CMD_BODY_CREATE, HTTP_TYPE_BINARY});
                expect(b.data.size() == 1, "body handle");
                uint8_t handle = b.data[0];
                command(target, {6, HTTP_CMD_BODY_ADD_BINARY, handle, 'a', 0, 'b'});
                Reply r = exchange(target, hdr, raw, handle);
                std::string first_status = r.status, data = r.data, query;
                if (raw) {
                    int blocks = 0;
                    while (!r.last && blocks++ < 5) { r = more(target); data += r.data; }
                    expect(r.last && data == body && blocks == 2, "exact block-boundary response and terminal empty block");
                } else {
                    expect(data.size() == 2, "structured response handles");
                    Reply q = command(target, {6, HTTP_CMD_BODY_QUERY, uint8_t(data[1])});
                    query = q.data;
                    expect(!query.empty() && query[0] == HTTP_DATA_OBJECT, "structured JSON remains queryable");
                }
                expect(first_status.find("503 Service Unavailable") != std::string::npos, "remote HTTP failure response retained");
                if (!secure) { plain_wire = wire; plain_data = data; plain_status = first_status; plain_query = query; }
                else expect(wire == plain_wire && data == plain_data && first_status == plain_status && query == plain_query, "HTTP/HTTPS wire and command reply parity");
            }
            expect(provider.closed == provider.opened && provider.destroyed == provider.created, "completed exchange releases provider");
        }
    }
    for (int action = 0; action < 5; ++action) {
        reset(std::string(HTTP_MAX_REPLY_BYTES + 1, 'x')); Provider provider;
        {
            HttpTarget target(7, &provider);
            auto hdr = header(target, "https://tls.lab.test/probe");
            expect(!exchange(target, hdr, true).last && provider.destroyed == 0, "pending response retains provider");
            if (action == 0) { target.abort(0); target.abort(0); }
            if (action == 1) { target.c64_reset(); target.c64_reset(); }
            if (action == 2) { command(target, {6, HTTP_CMD_FREE_ALL}); command(target, {6, HTTP_CMD_FREE_ALL}); }
            if (action == 3) { offset = 0; exchange(target, hdr, true); target.abort(0); }
            if (action != 4) expect(more(target).data.empty(), "cleanup clears pending data");
        }
        expect(provider.opened == provider.closed && provider.created == provider.destroyed, "abort reset replacement and destruction own provider cleanup");
    }
    for (int outcome = 0; outcome < 3; ++outcome) {
        reset(outcome == 0 ? "invalid-json" : "{\"answer\":42}");
        Provider provider; HttpTarget target(7, &provider);
        auto hdr = header(target, "https://tls.lab.test/probe");
        for (int i = 0; i < MAX_HTTP_HANDLES; ++i) {
            if (outcome == 1) command(target, {6, HTTP_CMD_BODY_CREATE, HTTP_TYPE_JSON_OBJ});
            if (outcome == 2 && i) header(target, "https://tls.lab.test/unused");
        }
        auto r = exchange(target, hdr, false);
        const char *expected[] = {"400 NO VALID JSON", "507 NO DATA SLOT", "507 NO HEADER SLOT"};
        expect(r.data.empty() && r.status == expected[outcome], "object conversion and capacity failures");
        expect(provider.destroyed == 1 && provider.closed == 1 && !socket_calls, "object failure releases secure provider");
    }
    {
        reset("{\"answer\":42}"); Provider provider; HttpTarget target(7, &provider);
        auto r = exchange(target, header(target, "https://tls.lab.test/probe"), false);
        expect(r.data.size() == 2, "cloned response header handle");
        offset = 0;
        r = exchange(target, uint8_t(r.data[0]), true);
        expect(r.data == "{\"answer\":42}" && provider.created == 2 && provider.destroyed == 2 && !socket_calls,
               "response header clone preserves secure routing");
    }
}
static void interrupt_cases()
{
    CancellationSource source;
    auto old = source.token();
    expect(!old.cancelled(), "initial token live");
    source.signal();
    expect(old.cancelled() && !source.token().cancelled(), "old token fenced; new command admitted");
    CancellationSource exhausted(UINT32_MAX - 1);
    auto final = exhausted.token();
    exhausted.signal(); exhausted.signal();
    expect(final.cancelled() && exhausted.token().cancelled(), "generation exhaustion fails closed");
    CancellationSource other;
    expect(!other.token().cancelled(), "independent source unaffected");
    for (bool raw : {false, true}) for (int phase = 0; phase <= 3; ++phase) {
        reset("{\"answer\":42}");
        CancellationSource events;
        Provider provider;
        provider.interrupt = &events; provider.signal_at = phase;
        HttpTarget target(7, &provider);
        auto hdr = header(target, "https://tls.lab.test/probe");
        target.begin_command(events.token());
        if (!phase) events.signal();
        auto r = exchange(target, hdr, raw);
        expect(r.data.empty() && r.status == "503 SERVICE UNAVAILABLE", "interrupted exchange publishes no body or handles");
        expect(provider.created == (phase ? 1 : 0) && provider.destroyed == provider.created,
               "owner releases interrupted provider once");
        expect(provider.opened == provider.closed && !socket_calls, "no open transport or plaintext fallback");
        expect(more(target).data.empty(), "no interrupted tail");
        reset("{\"answer\":42}"); provider.signal_at = 0;
        target.begin_command(events.token());
        expect(exchange(target, hdr, true).data == "{\"answer\":42}", "fresh explicit command recovers");
    }
    reset(std::string(HTTP_MAX_REPLY_BYTES * 2, 'x'));
    CancellationSource events;
    Provider provider; HttpTarget target(7, &provider);
    auto hdr = header(target, "https://tls.lab.test/probe");
    target.begin_command(events.token());
    expect(!exchange(target, hdr, true).last, "retain secure body for continuation");
    events.signal();
    expect(provider.destroyed == 0, "signalling never destroys owner resources");
    target.begin_command(events.token());
    auto r = more(target);
    expect(r.data.empty() && r.status == "503 SERVICE UNAVAILABLE", "cancelled continuation exposes no buffered bytes");
    target.abort(0); target.c64_reset();
    expect(provider.destroyed == 1 && provider.closed == 1, "continuation abort reset cleanup idempotent");
}
static void provider_restart_cases()
{
    for (bool raw : {false, true}) {
        Provider provider; HttpTarget target(7, &provider);
        auto hdr = header(target, "https://tls.lab.test/probe");
        for (int i = 0; i < MAX_HTTP_HANDLES + 2; ++i) {
            reset("{\"answer\":42}");
            before_publication = &provider.epoch;
            auto r = exchange(target, hdr, raw);
            expect(r.data.empty() && r.status == "503 SERVICE UNAVAILABLE", "restart after parse rejected at publication");
        }
        reset("{\"answer\":42}");
        expect(exchange(target, hdr, false).data.size() == 2, "unpublished response handles reclaimed");
    }
    for (bool raw : {false, true}) for (int phase = 0; phase <= 4; ++phase) {
        reset("{\"answer\":42}"); Provider provider; HttpTarget target(7, &provider);
        provider.interrupt = &provider.epoch; provider.signal_at = phase; provider.online = phase != 0;
        auto hdr = header(target, "https://tls.lab.test/probe");
        auto r = exchange(target, hdr, raw);
        expect(r.data.empty() && r.status == "503 SERVICE UNAVAILABLE", "unavailable or restarted provider exposes no reply");
        expect(provider.destroyed == provider.created && provider.opened == provider.closed && !socket_calls,
               "provider restart released by owner without fallback");
        if (phase == 4) expect(provider.opened == 0, "restart during factory call prevents open");
        reset("{\"answer\":42}"); provider.online = true; provider.signal_at = 0;
        expect(exchange(target, hdr, true).data == "{\"answer\":42}", "new provider lifetime permits explicit recovery");
    }
    reset(std::string(HTTP_MAX_REPLY_BYTES * 2, 'x'));
    Provider provider; HttpTarget target(7, &provider);
    expect(!exchange(target, header(target, "https://tls.lab.test/probe"), true).last, "provider retains buffered reply");
    provider.epoch.signal();
    expect(provider.destroyed == 0, "restart notification does not free owner connection");
    expect(more(target).data.empty() && provider.destroyed == 1, "restarted provider cannot deliver buffered tail");
}
int main()
{
    failure_cases(); scheme_cases(); parity_and_cleanup(); interrupt_cases(); provider_restart_cases();
    printf("HTTPS routing: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
extern "C" void outbyte(int byte) { fputc(byte, stdout); }
