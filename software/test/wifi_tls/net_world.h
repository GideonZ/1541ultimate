// The world the host tests run network_target.cc in: a clock, lwIP's sockets and DNS, the C64 reset line, and a WiFi
// module with a pretend TLS of [type][length lo][length hi][payload] records; deterministic, time moves only on waits.
// Types: 'H' hello/flight, 'F' Finished, 'D' data, 'C' close_notify, 'A' alert, 'B' bad MAC, 'W' warning, 'T' ticket.
#ifndef NET_WORLD_H_
#define NET_WORLD_H_

#include <stdint.h>
#include <deque>
#include <string>
#include <vector>
#include "wifi_tls.h"

typedef std::vector<uint8_t> bytes;

// The clock: one tick is 5 ms, as on the U64.
extern uint32_t world_tick;
void world_advance(uint32_t ticks);
uint32_t world_ms(void);             // world_tick in ms
extern uint32_t world_reset_at;      // reset_pending() from this tick on; ~0 for never
extern uint32_t world_sntp;          // what sntp_utc_now() answers

// A record of the pretend TLS.
bytes record(char type, size_t length, uint32_t seed = 0);
uint8_t payload_byte(uint32_t seed, uint32_t i);

// What a socket delivers: data, the peer's FIN, or an error; each from a tick on.
#define EV_DATA 0
#define EV_FIN  1
#define EV_ERR  2
struct SockEvent {
    int kind;
    bytes data;
    int err;
    uint32_t at;
};

// What the server does when the module's ClientHello has arrived.
#define SRV_RESPOND 0 // its flight, flight bytes, from delay_ms on, in chunks of chunk bytes
#define SRV_STALL   1
#define SRV_RST     2
#define SRV_FIN     3
struct FakeServer {
    int on_hello;
    size_t flight;
    uint32_t delay_ms;
    size_t chunk;
    std::vector<SockEvent> after_finished; // pushed once the module's Finished arrives (at is added to now)
    bool echo;
    bytes got;             // plaintext the server received
    bool saw_notify;       // the module's close_notify
    int saw_alert;         // the module's alert, -1 for none
    int hellos;
    bytes client_stream;   // every byte the module sent, unparsed rest
    uint32_t connect_delay_ms; // a non-blocking connect takes this long
    int connect_err;       // ... and then fails with this errno
    bool connect_never;
    int rst_err;           // the errno of SRV_RST; 0 for ECONNRESET
    int send_err;          // every send on this socket fails with it (set before the socket exists)
    int send_err_from;     // sends from this call number on (1-based) fail with send_err_late
    int send_err_late;     // EWOULDBLOCK makes it a stall
};

struct FakeSock {
    bool open;
    int type;
    bool nonblocking;
    bool nodelay;
    bool rcvtimeo_set;
    int sndtimeo_ms;       // 0 when never set
    bool connected;
    uint32_t connect_at;   // a non-blocking connect completes at this tick; 0 before it started
    uint32_t connect_addr; // where to, network order
    std::deque<SockEvent> rx;
    bool err_taken;        // the error has been handed out once
    bytes tx;
    int send_err;          // every send fails with this, when set
    bool send_blocked;     // every send would block
    int recv_calls;
    int recv_after_err;    // recv calls after the first error
    int send_calls;
    int closes;
    FakeServer srv;
};
extern FakeSock world_socks[16];
void server_push(int s, int kind, const bytes &data, int err = 0, uint32_t after_ms = 0);

// DNS: how a name resolves.
#define DNS_IMMEDIATE 0
#define DNS_ASYNC_OK  1
#define DNS_ASYNC_NO  2
#define DNS_ERR_VAL   3
#define DNS_NEVER     4
struct DnsRule {
    std::string host;
    int mode;
    uint32_t addr;
    uint32_t delay_ms;
};
extern std::vector<DnsRule> world_dns;
extern int world_dns_calls;
extern int world_dns_unlocked_calls;  // dns_gethostbyname called without the core lock
extern int world_dns_late_writes;     // callbacks that fired for an older lookup
extern int world_lock_depth;
extern int world_lock_bad;            // a callback that ran while the core lock was held
void world_dns_fire_all(void);        // every pending answer, now

// A step answer forced on the module instead of the pretend TLS.
struct Forced {
    int result;          // WIFI_TLS_*: BUSY (no answer), INVALID, REFUSED, UNSENT
    int esp_err;
    bool applied;        // the module still runs the step (for BUSY)
    uint32_t delay_ms;   // how long before the answer (0: at once)
    // For an answer of 0 with FAILED
    bool fail;
    uint8_t fail_reason;
    uint32_t verify_flags;
    uint8_t alert_in;
    int fail_err;
    bool pt_on_no_read;  // hand out plaintext on a NO_READ step (malformed)
};

struct FakeSession {
    bool live;
    uint16_t tag;
    uint8_t trust;
    int state;
    bool sent_hello;
    bytes in;
    std::deque<uint8_t> pt;
    bytes out;
    char terminal;       // 'C', 'A' or 'B' waiting behind pt; 0 for none
    uint8_t alert;
};

struct RpcCall {
    int cmd;             // 0x30.. as on the wire
    uint8_t slot;
    uint16_t tag;
    uint8_t flags;
    int ct_in_len;
    int pt_in_len;
    wifi_tls_open_t open;
    std::string sni;
    bytes pin;
    bool cancel_given;
};

struct FakeModule {
    bool available;
    uint16_t major, minor;
    uint32_t generation;
    bool entropy;
    bool module_time;    // its own SNTP
    // OPEN
    int open_result;     // forced; -1 for the normal checks
    int open_esp_err;
    uint8_t open_reason;
    uint32_t open_delay_ms;
    // the handshake
    size_t hello_size;
    bool hs_fail;
    uint8_t hs_fail_reason;
    uint32_t hs_verify_flags;
    uint8_t hs_alert_in;
    int hs_fail_err;
    bool hs_alert_out;   // send alert 40 with the failure
    // STATS
    uint16_t version, suite, group;
    uint8_t ems;
    uint8_t spki[32];
    uint16_t block_max_ms;
    int stats_result;    // forced; -1 for normal
    int stats_esp_err;   // with a forced REFUSED
    // SET_CA
    int set_ca_result;   // WIFI_TLS_* to answer; 0 loads
    int set_ca_esp_err;
    uint8_t set_ca_reject;
    bytes ca_der;
    uint16_t ca_tag;
    bool ca_loaded;
    // CLOSE
    int close_result;    // forced; -1 for normal
    int close_alls;
    // steps
    std::deque<Forced> forced;
    size_t in_cap;       // what the module holds of unread ciphertext before AGAIN
    int again_next;      // answer AGAIN this many times first
    FakeSession sess[2];
    std::vector<RpcCall> calls;
    int unsent_next;     // answer UNSENT this many times first (any call)
    bool quiet_alternate; // every other input-less step is quiet while plaintext is held
    bool quiet_last;
    int quiet_next;      // input-less steps that hand out nothing although plaintext is held, first
                         // (the engine's skipped-warning WANT_READ case, tls_engine.c:503-505)
};
extern FakeModule world_module;

void world_reset(void);
int world_count(int cmd);
int world_open_sockets(void);

#endif /* NET_WORLD_H_ */
