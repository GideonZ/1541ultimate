/*
 * net_world.cc: see net_world.h.
 */
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "net_world.h"
#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "netdb.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "command_intf.h"
#include "sntp_time.h"

uint32_t world_tick;
uint32_t world_reset_at;
FakeSock world_socks[16];
std::vector<DnsRule> world_dns;
int world_dns_calls;
int world_dns_unlocked_calls;
int world_dns_late_writes;
int world_lock_depth;
int world_lock_bad;
FakeModule world_module;
uint32_t world_sntp;

CommandInterface cmd_if;
CommandTarget *command_targets[CMD_IF_MAX_TARGET + 1];
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };
Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" };

#define MS_TICKS(ms) (((ms) + 4) / 5)

struct PendingDns {
    uint32_t at;
    std::string name;
    bool ok;
    ip_addr_t addr;
    dns_found_callback cb;
    void *arg;
};
static std::vector<PendingDns> pending_dns;

static void fire_dns(bool all)
{
    for (size_t i = 0; i < pending_dns.size();) {
        if (all || ((int32_t)(world_tick - pending_dns[i].at) >= 0)) {
            PendingDns p = pending_dns[i];
            pending_dns.erase(pending_dns.begin() + i);
            if (world_lock_depth) {
                world_lock_bad++;
            }
            p.cb(p.name.c_str(), p.ok ? &p.addr : NULL, p.arg);
        } else {
            i++;
        }
    }
}

void world_dns_fire_all(void)
{
    fire_dns(true);
}

void world_advance(uint32_t ticks)
{
    world_tick += ticks;
    fire_dns(false);
}

uint32_t world_ms(void)
{
    return world_tick * 5;
}

uint8_t payload_byte(uint32_t seed, uint32_t i)
{
    return (uint8_t)(((seed + i) * 2654435761u) >> 24);
}

bytes record(char type, size_t length, uint32_t seed)
{
    bytes r;
    r.push_back((uint8_t)type);
    r.push_back((uint8_t)length);
    r.push_back((uint8_t)(length >> 8));
    for (size_t i = 0; i < length; i++) {
        r.push_back(payload_byte(seed, i));
    }
    return r;
}

void server_push(int s, int kind, const bytes &data, int err, uint32_t after_ms)
{
    if ((s < 0) || (s >= 16)) {
        return; // no such socket (a red run)
    }
    SockEvent e;
    e.kind = kind;
    e.data = data;
    e.err = err;
    e.at = world_tick + MS_TICKS(after_ms);
    world_socks[s].rx.push_back(e);
}

void world_reset(void)
{
    world_tick = 1000;
    world_reset_at = 0xFFFFFFFF;
    for (int i = 0; i < 16; i++) {
        world_socks[i] = FakeSock();
        world_socks[i].srv.saw_alert = -1;
        world_socks[i].srv.flight = 2000;
        world_socks[i].srv.delay_ms = 20;
        world_socks[i].srv.chunk = 1460;
    }
    world_dns.clear();
    pending_dns.clear();
    world_dns_calls = 0;
    world_dns_unlocked_calls = 0;
    world_dns_late_writes = 0;
    world_lock_depth = 0;
    world_lock_bad = 0;
    world_module = FakeModule();
    FakeModule &m = world_module;
    m.available = true;
    m.major = 1;
    m.minor = 306;
    m.generation = 7;
    m.entropy = true;
    m.module_time = true;
    m.open_result = -1;
    m.hello_size = 300;
    m.version = 0x0304;
    m.suite = 0x1301;
    m.group = 0x001D;
    for (int i = 0; i < 32; i++) {
        m.spki[i] = (uint8_t)(0xA0 + i);
    }
    m.block_max_ms = 12;
    m.stats_result = -1;
    m.close_result = -1;
    m.in_cap = 18000; // mbedTLS reads a whole record, up to 16 KB
    world_sntp = 1790000000u; // 2026
}

int world_count(int cmd)
{
    int n = 0;
    for (size_t i = 0; i < world_module.calls.size(); i++) {
        n += (world_module.calls[i].cmd == cmd);
    }
    return n;
}

int world_open_sockets(void)
{
    int n = 0;
    for (int i = 0; i < 16; i++) {
        n += world_socks[i].open;
    }
    return n;
}

// --- FreeRTOS, the command interface, SNTP ---------------------------------

TickType_t xTaskGetTickCount(void)
{
    return world_tick;
}

void vTaskDelay(TickType_t ticks)
{
    world_advance(ticks ? ticks : 1);
}

bool CommandInterface :: reset_pending(void)
{
    return (world_reset_at != 0xFFFFFFFF) && ((int32_t)(world_tick - world_reset_at) >= 0);
}

uint32_t sntp_utc_now(void)
{
    return world_sntp;
}

// --- DNS and the core lock --------------------------------------------------

void fake_lock_tcpip_core(void)
{
    world_lock_depth++;
}

void fake_unlock_tcpip_core(void)
{
    world_lock_depth--;
}

err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr, dns_found_callback found, void *callback_arg)
{
    world_dns_calls++;
    if (!world_lock_depth) {
        world_dns_unlocked_calls++;
    }
    if (ip4addr_aton(hostname, addr)) {
        return ERR_OK;
    }
    DnsRule rule = { hostname, DNS_ASYNC_OK, htonl(0x0A000002), 20 };
    for (size_t i = 0; i < world_dns.size(); i++) {
        if (world_dns[i].host == hostname) {
            rule = world_dns[i];
        }
    }
    switch (rule.mode) {
        case DNS_IMMEDIATE:
            addr->addr = rule.addr;
            return ERR_OK;
        case DNS_ERR_VAL:
            return ERR_VAL;
        case DNS_NEVER:
            return ERR_INPROGRESS;
        default: {
            PendingDns p;
            p.at = world_tick + MS_TICKS(rule.delay_ms);
            p.name = hostname;
            p.ok = (rule.mode == DNS_ASYNC_OK);
            p.addr.addr = rule.addr;
            p.cb = found;
            p.arg = callback_arg;
            pending_dns.push_back(p);
            return ERR_INPROGRESS;
        }
    }
}

// --- sockets ------------------------------------------------------------------

static bool valid(int s)
{
    return (s >= 0) && (s < 16) && world_socks[s].open;
}

int lwip_socket(int domain, int type, int protocol)
{
    for (int s = 0; s < 16; s++) {
        if (!world_socks[s].open) {
            FakeServer srv = world_socks[s].srv; // what the test set up for this number
            world_socks[s] = FakeSock();
            world_socks[s].srv = srv;
            world_socks[s].open = true;
            world_socks[s].type = type;
            return s;
        }
    }
    errno = ENFILE;
    return -1;
}

int lwip_connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    FakeSock &k = world_socks[s];
    FakeServer &v = k.srv;
    k.connect_addr = ((const struct sockaddr_in *)name)->sin_addr.s_addr;
    if (k.connected) {
        errno = EISCONN;
        return -1;
    }
    if (!k.nonblocking) {
        if (v.connect_err) {
            errno = v.connect_err;
            return -1;
        }
        k.connected = true;
        return 0;
    }
    if (!k.connect_at) {
        k.connect_at = world_tick + MS_TICKS(v.connect_delay_ms ? v.connect_delay_ms : 10);
        errno = EINPROGRESS;
        return -1;
    }
    if (v.connect_never || ((int32_t)(world_tick - k.connect_at) < 0)) {
        errno = EALREADY;
        return -1;
    }
    if (v.connect_err) {
        errno = v.connect_err;
        return -1;
    }
    k.connected = true;
    errno = EISCONN;
    return -1;
}

int lwip_ioctl(int s, long cmd, void *argp)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    if (cmd == (long)FIONBIO) {
        world_socks[s].nonblocking = *(int *)argp != 0;
    }
    return 0;
}

int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    if ((level == IPPROTO_TCP) && (optname == TCP_NODELAY)) {
        world_socks[s].nodelay = *(const int *)optval != 0;
    }
    if ((level == SOL_SOCKET) && (optname == SO_RCVTIMEO)) {
        world_socks[s].rcvtimeo_set = true;
    }
    if ((level == SOL_SOCKET) && (optname == SO_SNDTIMEO)) {
        const struct timeval *tv = (const struct timeval *)optval;
        world_socks[s].sndtimeo_ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
    }
    return 0;
}

int lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    if ((level == SOL_SOCKET) && (optname == SO_TYPE)) {
        *(int *)optval = world_socks[s].type;
        *optlen = sizeof(int);
        return 0;
    }
    errno = ENOPROTOOPT;
    return -1;
}

static bool available(FakeSock &k)
{
    return !k.rx.empty() && ((int32_t)(world_tick - k.rx.front().at) >= 0);
}

// The server's side: what the module sent, record by record.
static void server_hears(int s)
{
    FakeSock &k = world_socks[s];
    FakeServer &v = k.srv;
    while (v.client_stream.size() >= 3) {
        size_t len = v.client_stream[1] | (v.client_stream[2] << 8);
        if (v.client_stream.size() < 3 + len) {
            break;
        }
        char type = (char)v.client_stream[0];
        bytes body(v.client_stream.begin() + 3, v.client_stream.begin() + 3 + len);
        v.client_stream.erase(v.client_stream.begin(), v.client_stream.begin() + 3 + len);
        if (type == 'H') {
            v.hellos++;
            if (v.on_hello == SRV_RESPOND) {
                bytes flight = record('H', v.flight, 77);
                for (size_t off = 0; off < flight.size(); off += v.chunk) {
                    size_t n = flight.size() - off;
                    if (n > v.chunk) {
                        n = v.chunk;
                    }
                    server_push(s, EV_DATA, bytes(flight.begin() + off, flight.begin() + off + n), 0, v.delay_ms);
                }
            } else if (v.on_hello == SRV_RST) {
                server_push(s, EV_ERR, bytes(), v.rst_err ? v.rst_err : ECONNRESET, v.delay_ms);
            } else if (v.on_hello == SRV_FIN) {
                server_push(s, EV_FIN, bytes(), 0, v.delay_ms);
            }
        } else if (type == 'F') {
            for (size_t i = 0; i < v.after_finished.size(); i++) {
                SockEvent e = v.after_finished[i];
                e.at += world_tick;
                k.rx.push_back(e);
            }
        } else if (type == 'D') {
            v.got.insert(v.got.end(), body.begin(), body.end());
            if (v.echo) {
                bytes r = body;
                r.insert(r.begin(), (uint8_t)(len >> 8));
                r.insert(r.begin(), (uint8_t)len);
                r.insert(r.begin(), (uint8_t)'D');
                server_push(s, EV_DATA, r, 0, 5);
            }
        } else if (type == 'C') {
            v.saw_notify = true;
        } else if (type == 'A') {
            v.saw_alert = body.empty() ? 0 : body[0];
        }
    }
}

ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    FakeSock &k = world_socks[s];
    k.send_calls++;
    if (k.srv.send_err_from && (k.send_calls >= k.srv.send_err_from)) {
        if (k.srv.send_err_late == EWOULDBLOCK) {
            k.send_blocked = true; // from here on, a stall (select advances the clock)
        }
        errno = k.srv.send_err_late;
        return -1;
    }
    if (k.send_err || k.srv.send_err) {
        errno = k.send_err ? k.send_err : k.srv.send_err;
        return -1;
    }
    if (k.send_blocked) {
        errno = EWOULDBLOCK;
        return -1;
    }
    const uint8_t *p = (const uint8_t *)dataptr;
    k.tx.insert(k.tx.end(), p, p + size);
    if (k.type == SOCK_STREAM) {
        k.srv.client_stream.insert(k.srv.client_stream.end(), p, p + size);
        server_hears(s);
    }
    return size;
}

ssize_t lwip_recv(int s, void *mem, size_t len, int flags)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    FakeSock &k = world_socks[s];
    k.recv_calls++;
    if (k.err_taken) {
        k.recv_after_err++;
        errno = ENOTCONN; // lwIP's errno changes on later calls
        return -1;
    }
    if (!available(k)) {
        if (!(flags & MSG_DONTWAIT) && !k.nonblocking) {
            world_advance(8); // SO_RCVTIMEO 40 ms
            if (available(k)) {
                return lwip_recv(s, mem, len, flags);
            }
        }
        errno = EWOULDBLOCK;
        return -1;
    }
    SockEvent &e = k.rx.front();
    if (e.kind == EV_FIN) {
        return 0; // and again on every later call
    }
    if (e.kind == EV_ERR) {
        k.err_taken = true;
        errno = e.err;
        return -1;
    }
    size_t n = (e.data.size() < len) ? e.data.size() : len;
    memcpy(mem, e.data.data(), n);
    e.data.erase(e.data.begin(), e.data.begin() + n);
    if (e.data.empty()) {
        k.rx.pop_front();
    }
    return n;
}

ssize_t lwip_recvmsg(int s, struct msghdr *message, int flags)
{
    return lwip_recv(s, message->msg_iov[0].iov_base, message->msg_iov[0].iov_len, flags);
}

int lwip_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    int s = maxfdp1 - 1;
    uint32_t wait = MS_TICKS(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    if (wait == 0) {
        wait = 1;
    }
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    FakeSock &k = world_socks[s];
    if (writeset) {
        if (k.send_blocked) {
            world_advance(wait);
            return 0;
        }
        if (k.nonblocking && !k.connected && k.connect_at) {
            if (k.srv.connect_never) {
                world_advance(wait);
                return 0;
            }
            int32_t d = (int32_t)(k.connect_at - world_tick);
            if (d > (int32_t)wait) {
                world_advance(wait);
                return 0;
            }
            if (d > 0) {
                world_advance(d);
            }
        }
        return 1;
    }
    if (available(k)) {
        return 1;
    }
    if (!k.rx.empty()) {
        uint32_t d = k.rx.front().at - world_tick;
        if (d <= wait) {
            world_advance(d);
            return 1;
        }
    }
    world_advance(wait);
    return 0;
}

int lwip_close(int s)
{
    if (!valid(s)) {
        errno = EBADF;
        return -1;
    }
    world_socks[s].open = false;
    world_socks[s].closes++;
    return 0;
}

int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result,
                    int *h_errnop)
{
    static struct in_addr address;
    static char *list[2];
    address.s_addr = htonl(0x0A000009);
    list[0] = (char *)&address;
    list[1] = NULL;
    ret->h_addr_list = list;
    *result = ret;
    return 0;
}

// --- the WiFi module ------------------------------------------------------------

bool wifi_tls_module_available(void)
{
    return world_module.available;
}

void wifi_tls_module_version(uint16_t *major, uint16_t *minor)
{
    *major = world_module.major;
    *minor = world_module.minor;
}

uint32_t wifi_tls_generation(void)
{
    return world_module.generation;
}

// Waits as wifi_dedicated_call does: in slices when there is a cancel. The
// answer comes after answer_ms; true when it came before the timeout and
// before a cancel.
static bool module_wait(uint32_t timeout_ms, const wifi_tls_cancel_t *cancel, uint32_t answer_ms, bool *cancelled)
{
    *cancelled = false;
    uint32_t spent = 0;
    while (true) {
        if (answer_ms <= spent) {
            world_advance(1);
            return true;
        }
        if (spent >= timeout_ms) {
            return false;
        }
        uint32_t slice = timeout_ms - spent;
        if (cancel && (slice > 100)) {
            slice = 100;
        }
        if (answer_ms - spent < slice) {
            world_advance(MS_TICKS(answer_ms - spent));
            return true;
        }
        world_advance(MS_TICKS(slice));
        spent += slice;
        if (cancel && cancel->cancelled(cancel->context)) {
            *cancelled = true;
            return false;
        }
    }
}

static RpcCall &log_call(int cmd, uint8_t slot, uint16_t tag, const wifi_tls_cancel_t *cancel)
{
    RpcCall c = RpcCall();
    c.cmd = cmd;
    c.slot = slot;
    c.tag = tag;
    c.cancel_given = (cancel != NULL);
    world_module.calls.push_back(c);
    return world_module.calls.back();
}

static bool unsent(void)
{
    if (world_module.unsent_next > 0) {
        world_module.unsent_next--;
        return true;
    }
    return false;
}

int wifi_tls_open(const wifi_tls_open_t *open, uint32_t timeout_ms, const wifi_tls_cancel_t *cancel, int *esp_err,
                  uint8_t *reason)
{
    FakeModule &m = world_module;
    *esp_err = 0;
    *reason = 0;
    if (!m.available) {
        return WIFI_TLS_NO_MODULE;
    }
    if (unsent()) {
        return WIFI_TLS_UNSENT;
    }
    RpcCall &c = log_call(0x30, open->slot, open->tag, cancel);
    c.open = *open;
    c.sni.assign(open->sni ? open->sni : "", open->sni_len);
    if (open->pin) {
        c.pin.assign(open->pin, open->pin + 32);
    }
    bool cancelled;
    if (!module_wait(timeout_ms, cancel, m.open_delay_ms, &cancelled)) {
        return WIFI_TLS_BUSY;
    }
    if (m.open_result >= 0) {
        *esp_err = m.open_esp_err;
        *reason = m.open_reason;
        return m.open_result;
    }
    if (!m.entropy) {
        *esp_err = WIFI_TLS_ESP_INVALID_STATE;
        *reason = WIFI_TLS_REASON_NO_ENTROPY;
        return WIFI_TLS_REFUSED;
    }
    if ((open->trust != WIFI_TLS_TRUST_PIN) && !m.module_time && (open->time_source == WIFI_TLS_TIME_NONE)) {
        *esp_err = WIFI_TLS_ESP_INVALID_STATE;
        *reason = WIFI_TLS_REASON_NO_TIME;
        return WIFI_TLS_REFUSED;
    }
    if ((open->trust == WIFI_TLS_TRUST_CUSTOM) && (!m.ca_loaded || (m.ca_tag != open->ca_tag))) {
        *esp_err = WIFI_TLS_ESP_INVALID_STATE;
        *reason = WIFI_TLS_REASON_NO_CUSTOM_CA;
        return WIFI_TLS_REFUSED;
    }
    FakeSession &s = m.sess[open->slot];
    s = FakeSession(); // a slot held under another tag is freed first
    s.live = true;
    s.tag = open->tag;
    s.trust = open->trust;
    s.state = WIFI_TLS_HANDSHAKING;
    return 0;
}

static void emit(bytes &out, char type, const uint8_t *p, size_t n)
{
    out.push_back((uint8_t)type);
    out.push_back((uint8_t)n);
    out.push_back((uint8_t)(n >> 8));
    out.insert(out.end(), p, p + n);
}

static void parse_in(FakeSession &s, wifi_tls_step_t *st)
{
    FakeModule &m = world_module;
    while ((s.in.size() >= 3) && !s.terminal && (s.state != WIFI_TLS_FAILED)) {
        size_t len = s.in[1] | (s.in[2] << 8);
        if (s.in.size() < 3 + len) {
            break;
        }
        char type = (char)s.in[0];
        bytes body(s.in.begin() + 3, s.in.begin() + 3 + len);
        s.in.erase(s.in.begin(), s.in.begin() + 3 + len);
        if (s.state == WIFI_TLS_HANDSHAKING) {
            if (type != 'H') {
                continue;
            }
            if (m.hs_fail) {
                s.state = WIFI_TLS_FAILED;
                st->fail_reason = m.hs_fail_reason;
                st->verify_flags = m.hs_verify_flags;
                st->alert_in = m.hs_alert_in;
                st->esp_err = m.hs_fail_err;
                if (m.hs_alert_out) {
                    uint8_t a = 40;
                    emit(s.out, 'A', &a, 1);
                }
                return;
            }
            s.state = WIFI_TLS_ESTABLISHED;
            bytes fin = record('F', 32);
            s.out.insert(s.out.end(), fin.begin(), fin.end());
            continue;
        }
        if (type == 'D') {
            s.pt.insert(s.pt.end(), body.begin(), body.end());
        } else if ((type == 'C') || (type == 'B')) {
            s.terminal = type;
        } else if (type == 'A') {
            s.terminal = 'A';
            s.alert = body.empty() ? 0 : body[0];
        }
    }
}

int wifi_tls_step(uint8_t slot, uint16_t tag, wifi_tls_step_t *st, uint32_t timeout_ms,
                  const wifi_tls_cancel_t *cancel)
{
    FakeModule &m = world_module;
    st->ct_out_len = 0;
    st->pt_out_len = 0;
    st->state = WIFI_TLS_UNKNOWN;
    st->esp_err = 0;
    st->alert_in = WIFI_TLS_ALERT_NONE;
    st->alert_out = WIFI_TLS_ALERT_NONE;
    st->fail_reason = 0;
    st->verify_flags = 0;
    if (!m.available) {
        return WIFI_TLS_NO_MODULE;
    }
    if (unsent()) {
        return WIFI_TLS_UNSENT;
    }
    RpcCall &c = log_call(0x31, slot, tag, cancel);
    c.flags = st->flags;
    c.ct_in_len = st->ct_in_len;
    c.pt_in_len = st->pt_in_len;

    Forced f = Forced();
    bool forced = !m.forced.empty();
    if (forced) {
        f = m.forced.front();
        m.forced.pop_front();
    }
    bool cancelled;
    bool busy = forced && (f.result == WIFI_TLS_BUSY);
    if (busy) {
        module_wait(timeout_ms, cancel, 0xFFFFFFFF, &cancelled);
        if (!f.applied) {
            return WIFI_TLS_BUSY;
        }
    } else if (!module_wait(timeout_ms, cancel, f.delay_ms, &cancelled)) {
        return WIFI_TLS_BUSY;
    }
    if (forced && (f.result == WIFI_TLS_INVALID || f.result == WIFI_TLS_REFUSED || f.result == WIFI_TLS_UNSENT)) {
        st->esp_err = f.esp_err;
        return f.result;
    }
    FakeSession &s = m.sess[slot];
    if (!s.live || (s.tag != tag)) {
        st->esp_err = WIFI_TLS_ESP_NOT_FOUND;
        return WIFI_TLS_REFUSED; // touches nothing (the tag rule)
    }
    if ((m.quiet_next > 0) && (st->ct_in_len == 0) && (s.state == WIFI_TLS_ESTABLISHED)) {
        m.quiet_next--;
        st->state = s.state;
        return 0;
    }
    if (m.quiet_alternate && (st->ct_in_len == 0) && (s.state == WIFI_TLS_ESTABLISHED) && !s.pt.empty()) {
        m.quiet_last = !m.quiet_last;
        if (m.quiet_last) {
            st->state = s.state;
            return 0;
        }
    }
    bool again = false;
    if (m.again_next > 0) {
        m.again_next--;
        again = true;
    } else if (s.in.size() + st->ct_in_len > m.in_cap) {
        again = true;
    }
    if (!again) {
        s.in.insert(s.in.end(), st->ct_in, st->ct_in + st->ct_in_len);
        if ((s.state == WIFI_TLS_ESTABLISHED) && st->pt_in_len) {
            emit(s.out, 'D', st->pt_in, st->pt_in_len);
        }
    }
    if (s.state == WIFI_TLS_HANDSHAKING && !s.sent_hello) {
        bytes hello = record('H', m.hello_size);
        s.out.insert(s.out.end(), hello.begin(), hello.end());
        s.sent_hello = true;
    }
    if (!(st->flags & WIFI_TLS_STEP_NO_READ) || (s.state == WIFI_TLS_HANDSHAKING)) {
        parse_in(s, st);
    }
    if (forced && f.fail && (s.state == WIFI_TLS_ESTABLISHED)) {
        s.state = WIFI_TLS_FAILED;
        st->fail_reason = f.fail_reason;
        st->verify_flags = f.verify_flags;
        st->alert_in = f.alert_in;
        st->esp_err = f.fail_err;
    }
    size_t ct = s.out.size() < 1024 ? s.out.size() : 1024;
    if (ct > st->ct_out_room) {
        return WIFI_TLS_INVALID;
    }
    memcpy(st->ct_out, s.out.data(), ct);
    s.out.erase(s.out.begin(), s.out.begin() + ct);
    st->ct_out_len = ct;
    size_t pt = 0;
    if ((s.state == WIFI_TLS_ESTABLISHED) && !(st->flags & WIFI_TLS_STEP_NO_READ)) {
        size_t room = 1024 - ct;
        if (room > 960) {
            room = 960;
        }
        pt = s.pt.size() < room ? s.pt.size() : room;
        for (size_t i = 0; i < pt; i++) {
            st->pt_out[i] = s.pt.front();
            s.pt.pop_front();
        }
        if (s.terminal && s.pt.empty()) {
            if (s.terminal == 'C') {
                s.state = WIFI_TLS_CLOSED;
            } else if (pt == 0) {
                s.state = WIFI_TLS_FAILED;
                if (s.terminal == 'A') {
                    st->alert_in = s.alert;
                    st->esp_err = -0x7780; // MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE
                } else {
                    st->esp_err = -0x7180; // MBEDTLS_ERR_SSL_INVALID_MAC
                }
            }
        }
    }
    if (forced && f.pt_on_no_read && (st->flags & WIFI_TLS_STEP_NO_READ)) {
        return WIFI_TLS_INVALID; // what wifi_tls_take_step makes of it
    }
    st->pt_out_len = pt;
    st->state = s.state;
    if ((s.state == WIFI_TLS_FAILED) || (s.state == WIFI_TLS_CLOSED)) {
        s.live = false;
    }
    if (busy) {
        return WIFI_TLS_BUSY; // it ran, but its answer came too late
    }
    if (again) {
        if (s.state == WIFI_TLS_CLOSED) {
            return 0;
        }
        return WIFI_TLS_AGAIN;
    }
    return 0;
}

int wifi_tls_close(uint8_t slot, uint16_t tag, uint8_t *ct_out, uint16_t room, uint16_t *ct_out_len, int *esp_err,
                   uint32_t timeout_ms, const wifi_tls_cancel_t *cancel)
{
    FakeModule &m = world_module;
    *esp_err = 0;
    *ct_out_len = 0;
    if (!m.available) {
        return WIFI_TLS_NO_MODULE;
    }
    log_call(0x32, slot, tag, cancel);
    bool cancelled;
    module_wait(timeout_ms, cancel, 0, &cancelled);
    if (m.close_result >= 0) {
        return m.close_result;
    }
    FakeSession &s = m.sess[slot];
    if (!s.live || (s.tag != tag)) {
        *esp_err = WIFI_TLS_ESP_NOT_FOUND;
        return WIFI_TLS_REFUSED;
    }
    s.live = false;
    bytes notify = record('C', 0);
    memcpy(ct_out, notify.data(), notify.size());
    *ct_out_len = notify.size();
    return 0;
}

int wifi_tls_stats(uint8_t slot, uint16_t tag, wifi_tls_stats_t *stats, int *esp_err, uint32_t timeout_ms,
                   const wifi_tls_cancel_t *cancel)
{
    FakeModule &m = world_module;
    *esp_err = 0;
    if (!m.available) {
        return WIFI_TLS_NO_MODULE;
    }
    log_call(0x33, slot, tag, cancel);
    bool cancelled;
    module_wait(timeout_ms, cancel, 0, &cancelled);
    if (m.stats_result >= 0) {
        *esp_err = m.stats_esp_err;
        return m.stats_result;
    }
    memset(stats, 0, sizeof(*stats));
    if (slot == WIFI_TLS_SLOT_GLOBAL) {
        stats->entropy_source = m.entropy ? 1 : 0;
        stats->time_source = m.module_time ? 2 : 0;
        stats->block_max_ms = m.block_max_ms;
        return 0;
    }
    FakeSession &s = m.sess[slot];
    if (!s.live || (s.tag != tag)) {
        *esp_err = WIFI_TLS_ESP_NOT_FOUND;
        return WIFI_TLS_REFUSED;
    }
    stats->tls_version = m.version;
    stats->ciphersuite = m.suite;
    stats->group = m.group;
    stats->ems = m.ems;
    memcpy(stats->spki_sha256, m.spki, 32);
    return 0;
}

int wifi_tls_close_all(int *esp_err, uint32_t timeout_ms)
{
    FakeModule &m = world_module;
    *esp_err = 0;
    log_call(0x34, 0, 0, NULL);
    m.close_alls++;
    m.sess[0].live = false;
    m.sess[1].live = false;
    m.ca_loaded = false;
    world_advance(1);
    return 0;
}

int wifi_tls_set_ca(const uint8_t *der, uint16_t der_len, uint16_t ca_tag, int *esp_err, uint8_t *reject,
                    uint32_t timeout_ms, const wifi_tls_cancel_t *cancel)
{
    FakeModule &m = world_module;
    *esp_err = 0;
    *reject = 0;
    if (!m.available) {
        return WIFI_TLS_NO_MODULE;
    }
    RpcCall &c = log_call(0x35, 0, ca_tag, cancel);
    c.pt_in_len = der_len;
    m.ca_der.assign(der, der + der_len);
    world_advance(1);
    m.ca_loaded = false;
    if (m.set_ca_result == 0) {
        m.ca_loaded = true;
        m.ca_tag = ca_tag;
        return 0;
    }
    *esp_err = m.set_ca_esp_err;
    *reject = m.set_ca_reject;
    return m.set_ca_result;
}
