/* rpc_tls.h: the wire contract of the CMD_TLS_* commands: their opcodes and the layout of their requests and answers.
 * One definition for every module firmware that runs the TLS engine (software/wifi/tls) and for the Ultimate side
 * that decodes them. Include rpc_calls.h, which includes this after the types it builds on. */

#ifndef RPC_TLS_H_
#define RPC_TLS_H_

#include <stddef.h>
#include <stdint.h>
#include "rpc_calls.h"

// TLS sessions, named by (slot, tag): the ESP32 runs the engine, the Ultimate moves the ciphertext. Fixed fields first,
// variable data last; the asserts at the end of this file and wifi_tls.cc check these layouts at fixed offsets.
#define TLS_MAX_SESSIONS   2
#define TLS_SNI_MAX_LEN    253  // longest DNS name
#define TLS_STEP_IN_MAX    1536 // ct_in_len + pt_in_len per step
#define TLS_STEP_OUT_MAX   1024 // ct_out_len + pt_out_len per step
#define TLS_SLOT_GLOBAL    0xFF // CMD_TLS_STATS without a session (tag 0)
#define TLS_PIN_LEN        32   // SHA-256 of the server's SubjectPublicKeyInfo, digest order
#define TLS_CA_MAX_LEN     1472 // one DER certificate for trust mode CUSTOM
#define TLS_ALERT_NONE     0xFF

typedef enum {
    TLS_TRUST_BUNDLE = 0, // the ESP-IDF certificate bundle
    TLS_TRUST_CUSTOM = 1, // the one CA loaded with CMD_TLS_SET_CA, matched by ca_tag
    TLS_TRUST_PIN    = 2, // the leaf key's SHA-256 only
} tls_trust_t;

#define TLS_OPT_REQUIRE_TLS13 0x01 // rpc_tls_open_req.opts: offer TLS 1.3 only
#define TLS_STEP_NO_READ      0x01 // rpc_tls_step_req.flags: no plaintext out of this step

// rpc_tls_open_resp.reason, telling the ESP_ERR_INVALID_STATE refusals apart
#define TLS_OPEN_REASON_NONE         0
#define TLS_OPEN_REASON_NO_ENTROPY   1 // the radio has not started: no RNG seed
#define TLS_OPEN_REASON_NO_TIME      2 // BUNDLE or CUSTOM, but no trustworthy clock
#define TLS_OPEN_REASON_NO_CUSTOM_CA 3 // CUSTOM, but no CA held under the request's ca_tag

typedef enum {
    TLS_TIME_NONE = 0, // clock not later than the build date
    TLS_TIME_NIOS = 1, // set from unix_time in a CMD_TLS_OPEN (the Ultimate's SNTP time)
    TLS_TIME_SNTP = 2, // already valid without the Ultimate: SNTP over the ESP32's own Wi-Fi (STATS only)
} tls_time_source_t;

typedef enum {
    TLS_STATE_HANDSHAKING = 0,
    TLS_STATE_ESTABLISHED = 1,
    TLS_STATE_CLOSED      = 2,
    TLS_STATE_FAILED      = 3, // esp_err holds the mbedTLS error code, fail_reason says more
} tls_state_t;

// rpc_tls_step_resp.fail_reason, set with FAILED
typedef enum {
    TLS_FAIL_NONE             = 0, // generic: esp_err has the mbedTLS code (alert_in the alert, if any)
    TLS_FAIL_VERSION_REFUSED  = 1, // the ServerHello chose a version not offered; it is in verify_flags' low 16 bits
    TLS_FAIL_DOWNGRADE        = 2, // TLS 1.2 chosen by a ServerHello carrying the RFC 8446 downgrade sentinel
    TLS_FAIL_NO_EMS           = 3, // a TLS 1.2 server without the extended master secret
    TLS_FAIL_CERT_VERIFY      = 4, // verify_flags = mbedtls_ssl_get_verify_result(), 0xFFFFFFFF if it never completed
    TLS_FAIL_PIN_MISMATCH     = 5, // PIN: the leaf key is not the pinned one
    TLS_FAIL_PEER_CLOSED      = 6, // close_notify during the handshake
} tls_fail_reason_t;

// rpc_tls_set_ca_resp.reject, with ESP_ERR_INVALID_ARG
#define TLS_CA_REJECT_NONE          0
#define TLS_CA_REJECT_RSA_KEY_SHORT 1 // RSA key below 2048 bits
#define TLS_CA_REJECT_RESERVED      2 // never sent
#define TLS_CA_REJECT_CURVE         3 // EC key on a curve the X.509 profile does not accept
#define TLS_CA_REJECT_NOT_CA        4 // v3 without basicConstraints cA, or a keyUsage without keyCertSign

typedef struct {
    rpc_header_t hdr;
    uint8_t trust;        // tls_trust_t
    uint8_t sni_len;      // 0..TLS_SNI_MAX_LEN, no terminator; 0 = no name (PIN only)
    uint8_t slot;         // 0..TLS_MAX_SESSIONS-1, chosen by the Ultimate
    uint8_t time_source;  // TLS_TIME_NONE, or TLS_TIME_NIOS when unix_time is the Ultimate's SNTP time
    uint32_t unix_time;   // UTC seconds
    uint16_t tag;         // names the session with slot; never 0
    uint8_t opts;         // TLS_OPT_*
    uint8_t reserved;
    uint16_t ca_tag;      // CUSTOM: the Ultimate's current ca_tag; 0 otherwise
    uint16_t reserved2;
    uint8_t pin[TLS_PIN_LEN]; // read only when trust is PIN
    char sni[];
} rpc_tls_open_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;     // ESP_OK, an esp_err_t, or a negative mbedTLS code
    uint8_t slot;    // TLS_SLOT_GLOBAL when esp_err != ESP_OK
    uint8_t reason;  // TLS_OPEN_REASON_*
    uint16_t reserved; // 0
} rpc_tls_open_resp;

typedef struct {
    rpc_header_t hdr;
    uint8_t slot;
    uint8_t flags;   // TLS_STEP_NO_READ
    uint16_t tag;
    uint16_t ct_in_len;
    uint16_t pt_in_len;
    uint8_t data[];  // ct_in, then pt_in
} rpc_tls_step_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint8_t state;         // tls_state_t
    uint8_t alert_in;      // fatal alert received, TLS_ALERT_NONE if none
    uint16_t ct_out_len;
    uint16_t pt_out_len;
    uint8_t alert_out;     // alert this engine sent unencrypted, TLS_ALERT_NONE if none (or encrypted)
    uint8_t fail_reason;   // tls_fail_reason_t, with FAILED
    uint32_t verify_flags; // see tls_fail_reason_t
    uint8_t data[];        // ct_out, then pt_out
} rpc_tls_step_resp;

typedef struct {
    rpc_header_t hdr;
    uint8_t slot;
    uint8_t reserved;
    uint16_t tag;
} rpc_tls_session_req; // CMD_TLS_CLOSE and CMD_TLS_STATS

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint16_t ct_out_len;
    uint16_t reserved;
    uint8_t data[];  // the close_notify record
} rpc_tls_close_resp;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint32_t heap_free;     // internal 8-bit capable heap
    uint32_t heap_min;      // low-water mark since the OPEN that found no session open
    uint32_t largest_block;
    uint32_t ct_in_bytes;   // session counters, 0 for TLS_SLOT_GLOBAL
    uint32_t ct_out_bytes;
    uint32_t pt_in_bytes;
    uint32_t pt_out_bytes;
    uint32_t handshake_us;  // ESP32 time spent in handshake steps
    uint16_t tls_version;   // 0x0304 for TLS 1.3
    uint16_t ciphersuite;   // IANA id
    uint16_t group;         // IANA NamedGroup, never 0 once ESTABLISHED
    uint8_t state;          // tls_state_t
    uint8_t sessions_open;
    uint32_t stack_free;    // TLS task stack high-water mark, bytes
    uint32_t esp_time;      // ESP32 clock at the session's OPEN (now, for TLS_SLOT_GLOBAL), 0 = no time
    uint8_t time_source;    // tls_time_source_t of that clock
    uint8_t entropy_source; // entropy_source_t, now
    uint8_t ems;            // 1: TLS 1.2 with the extended master secret
    uint8_t reserved;
    uint16_t block_max_ms;  // longest dispatcher iteration since boot
    uint16_t reserved2;
    uint8_t spki_sha256[32]; // SHA-256 of the leaf's SubjectPublicKeyInfo, digest order
} rpc_tls_stats_resp;

typedef struct {
    rpc_header_t hdr;
    uint16_t der_len; // 1..TLS_CA_MAX_LEN
    uint16_t ca_tag;  // never 0
    uint8_t der[];
} rpc_tls_set_ca_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;     // ESP_OK, a negative mbedTLS parse code, or ESP_ERR_INVALID_ARG with a reject reason
    uint8_t reject;  // TLS_CA_REJECT_*
    uint8_t reserved[3];
} rpc_tls_set_ca_resp;

#define CMD_TLS_OPEN          0x30
#define CMD_TLS_STEP          0x31
#define CMD_TLS_CLOSE         0x32
#define CMD_TLS_STATS         0x33
#define CMD_TLS_CLOSE_ALL     0x34
#define CMD_TLS_SET_CA        0x35

// TLS. The layout is part of the wire, the header is echoed, and the fixed part of every reply is zeroed
// before it is filled. The Ultimate decodes these by offset and asserts the same ones (wifi_tls.cc): a layout
// change must break the build, not the wire. The asserts here are C only: GCC's C++ front end has no
// _Static_assert, and glibc, unlike newlib, does not supply a macro for it.
#ifndef __cplusplus
_Static_assert(sizeof(rpc_tls_open_req) == 52, "rpc_tls_open_req size");
_Static_assert(offsetof(rpc_tls_open_req, trust) == 4, "rpc_tls_open_req.trust");
_Static_assert(offsetof(rpc_tls_open_req, sni_len) == 5, "rpc_tls_open_req.sni_len");
_Static_assert(offsetof(rpc_tls_open_req, slot) == 6, "rpc_tls_open_req.slot");
_Static_assert(offsetof(rpc_tls_open_req, time_source) == 7, "rpc_tls_open_req.time_source");
_Static_assert(offsetof(rpc_tls_open_req, unix_time) == 8, "rpc_tls_open_req.unix_time");
_Static_assert(offsetof(rpc_tls_open_req, tag) == 12, "rpc_tls_open_req.tag");
_Static_assert(offsetof(rpc_tls_open_req, opts) == 14, "rpc_tls_open_req.opts");
_Static_assert(offsetof(rpc_tls_open_req, reserved) == 15, "rpc_tls_open_req.reserved");
_Static_assert(offsetof(rpc_tls_open_req, ca_tag) == 16, "rpc_tls_open_req.ca_tag");
_Static_assert(offsetof(rpc_tls_open_req, reserved2) == 18, "rpc_tls_open_req.reserved2");
_Static_assert(offsetof(rpc_tls_open_req, pin) == 20, "rpc_tls_open_req.pin");
_Static_assert(offsetof(rpc_tls_open_req, sni) == 52, "rpc_tls_open_req.sni");
_Static_assert(sizeof(rpc_tls_open_resp) == 12, "rpc_tls_open_resp size");
_Static_assert(offsetof(rpc_tls_open_resp, esp_err) == 4, "rpc_tls_open_resp.esp_err");
_Static_assert(offsetof(rpc_tls_open_resp, slot) == 8, "rpc_tls_open_resp.slot");
_Static_assert(offsetof(rpc_tls_open_resp, reason) == 9, "rpc_tls_open_resp.reason");
_Static_assert(offsetof(rpc_tls_open_resp, reserved) == 10, "rpc_tls_open_resp.reserved");
_Static_assert(sizeof(rpc_tls_step_req) == 12, "rpc_tls_step_req size");
_Static_assert(offsetof(rpc_tls_step_req, slot) == 4, "rpc_tls_step_req.slot");
_Static_assert(offsetof(rpc_tls_step_req, flags) == 5, "rpc_tls_step_req.flags");
_Static_assert(offsetof(rpc_tls_step_req, tag) == 6, "rpc_tls_step_req.tag");
_Static_assert(offsetof(rpc_tls_step_req, ct_in_len) == 8, "rpc_tls_step_req.ct_in_len");
_Static_assert(offsetof(rpc_tls_step_req, pt_in_len) == 10, "rpc_tls_step_req.pt_in_len");
_Static_assert(offsetof(rpc_tls_step_req, data) == 12, "rpc_tls_step_req.data");
_Static_assert(sizeof(rpc_tls_step_resp) == 20, "rpc_tls_step_resp size");
_Static_assert(offsetof(rpc_tls_step_resp, esp_err) == 4, "rpc_tls_step_resp.esp_err");
_Static_assert(offsetof(rpc_tls_step_resp, state) == 8, "rpc_tls_step_resp.state");
_Static_assert(offsetof(rpc_tls_step_resp, alert_in) == 9, "rpc_tls_step_resp.alert_in");
_Static_assert(offsetof(rpc_tls_step_resp, ct_out_len) == 10, "rpc_tls_step_resp.ct_out_len");
_Static_assert(offsetof(rpc_tls_step_resp, pt_out_len) == 12, "rpc_tls_step_resp.pt_out_len");
_Static_assert(offsetof(rpc_tls_step_resp, alert_out) == 14, "rpc_tls_step_resp.alert_out");
_Static_assert(offsetof(rpc_tls_step_resp, fail_reason) == 15, "rpc_tls_step_resp.fail_reason");
_Static_assert(offsetof(rpc_tls_step_resp, verify_flags) == 16, "rpc_tls_step_resp.verify_flags");
_Static_assert(offsetof(rpc_tls_step_resp, data) == 20, "rpc_tls_step_resp.data");
_Static_assert(sizeof(rpc_tls_session_req) == 8, "rpc_tls_session_req size");
_Static_assert(offsetof(rpc_tls_session_req, slot) == 4, "rpc_tls_session_req.slot");
_Static_assert(offsetof(rpc_tls_session_req, reserved) == 5, "rpc_tls_session_req.reserved");
_Static_assert(offsetof(rpc_tls_session_req, tag) == 6, "rpc_tls_session_req.tag");
_Static_assert(sizeof(rpc_tls_close_resp) == 12, "rpc_tls_close_resp size");
_Static_assert(offsetof(rpc_tls_close_resp, esp_err) == 4, "rpc_tls_close_resp.esp_err");
_Static_assert(offsetof(rpc_tls_close_resp, ct_out_len) == 8, "rpc_tls_close_resp.ct_out_len");
_Static_assert(offsetof(rpc_tls_close_resp, reserved) == 10, "rpc_tls_close_resp.reserved");
_Static_assert(offsetof(rpc_tls_close_resp, data) == 12, "rpc_tls_close_resp.data");
_Static_assert(sizeof(rpc_espcmd_resp) == 8, "rpc_espcmd_resp size");
_Static_assert(offsetof(rpc_espcmd_resp, esp_err) == 4, "rpc_espcmd_resp.esp_err");
_Static_assert(sizeof(rpc_tls_stats_resp) == 96, "rpc_tls_stats_resp size");
_Static_assert(offsetof(rpc_tls_stats_resp, esp_err) == 4, "rpc_tls_stats_resp.esp_err");
_Static_assert(offsetof(rpc_tls_stats_resp, heap_free) == 8, "rpc_tls_stats_resp.heap_free");
_Static_assert(offsetof(rpc_tls_stats_resp, heap_min) == 12, "rpc_tls_stats_resp.heap_min");
_Static_assert(offsetof(rpc_tls_stats_resp, largest_block) == 16, "rpc_tls_stats_resp.largest_block");
_Static_assert(offsetof(rpc_tls_stats_resp, ct_in_bytes) == 20, "rpc_tls_stats_resp.ct_in_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, ct_out_bytes) == 24, "rpc_tls_stats_resp.ct_out_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, pt_in_bytes) == 28, "rpc_tls_stats_resp.pt_in_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, pt_out_bytes) == 32, "rpc_tls_stats_resp.pt_out_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, handshake_us) == 36, "rpc_tls_stats_resp.handshake_us");
_Static_assert(offsetof(rpc_tls_stats_resp, tls_version) == 40, "rpc_tls_stats_resp.tls_version");
_Static_assert(offsetof(rpc_tls_stats_resp, ciphersuite) == 42, "rpc_tls_stats_resp.ciphersuite");
_Static_assert(offsetof(rpc_tls_stats_resp, group) == 44, "rpc_tls_stats_resp.group");
_Static_assert(offsetof(rpc_tls_stats_resp, state) == 46, "rpc_tls_stats_resp.state");
_Static_assert(offsetof(rpc_tls_stats_resp, sessions_open) == 47, "rpc_tls_stats_resp.sessions_open");
_Static_assert(offsetof(rpc_tls_stats_resp, stack_free) == 48, "rpc_tls_stats_resp.stack_free");
_Static_assert(offsetof(rpc_tls_stats_resp, esp_time) == 52, "rpc_tls_stats_resp.esp_time");
_Static_assert(offsetof(rpc_tls_stats_resp, time_source) == 56, "rpc_tls_stats_resp.time_source");
_Static_assert(offsetof(rpc_tls_stats_resp, entropy_source) == 57, "rpc_tls_stats_resp.entropy_source");
_Static_assert(offsetof(rpc_tls_stats_resp, ems) == 58, "rpc_tls_stats_resp.ems");
_Static_assert(offsetof(rpc_tls_stats_resp, reserved) == 59, "rpc_tls_stats_resp.reserved");
_Static_assert(offsetof(rpc_tls_stats_resp, block_max_ms) == 60, "rpc_tls_stats_resp.block_max_ms");
_Static_assert(offsetof(rpc_tls_stats_resp, reserved2) == 62, "rpc_tls_stats_resp.reserved2");
_Static_assert(offsetof(rpc_tls_stats_resp, spki_sha256) == 64, "rpc_tls_stats_resp.spki_sha256");
_Static_assert(sizeof(rpc_tls_set_ca_req) == 8, "rpc_tls_set_ca_req size");
_Static_assert(offsetof(rpc_tls_set_ca_req, der_len) == 4, "rpc_tls_set_ca_req.der_len");
_Static_assert(offsetof(rpc_tls_set_ca_req, ca_tag) == 6, "rpc_tls_set_ca_req.ca_tag");
_Static_assert(offsetof(rpc_tls_set_ca_req, der) == 8, "rpc_tls_set_ca_req.der");
_Static_assert(sizeof(rpc_tls_set_ca_resp) == 12, "rpc_tls_set_ca_resp size");
_Static_assert(offsetof(rpc_tls_set_ca_resp, esp_err) == 4, "rpc_tls_set_ca_resp.esp_err");
_Static_assert(offsetof(rpc_tls_set_ca_resp, reject) == 8, "rpc_tls_set_ca_resp.reject");
_Static_assert((TLS_TIME_NONE == 0) && (TLS_TIME_NIOS == 1) && (TLS_TIME_SNTP == 2), "tls_time_source_t");
_Static_assert((TLS_STATE_HANDSHAKING == 0) && (TLS_STATE_ESTABLISHED == 1) &&
               (TLS_STATE_CLOSED == 2) && (TLS_STATE_FAILED == 3), "tls_state_t");
_Static_assert((TLS_TRUST_BUNDLE == 0) && (TLS_TRUST_CUSTOM == 1) && (TLS_TRUST_PIN == 2), "tls_trust_t");
_Static_assert((TLS_FAIL_VERSION_REFUSED == 1) && (TLS_FAIL_DOWNGRADE == 2) && (TLS_FAIL_NO_EMS == 3) &&
               (TLS_FAIL_CERT_VERIFY == 4) && (TLS_FAIL_PIN_MISMATCH == 5) && (TLS_FAIL_PEER_CLOSED == 6), "tls_fail_reason_t");
_Static_assert((TLS_OPEN_REASON_NO_ENTROPY == 1) && (TLS_OPEN_REASON_NO_TIME == 2) && (TLS_OPEN_REASON_NO_CUSTOM_CA == 3),
               "rpc_tls_open_resp.reason");
_Static_assert((TLS_CA_REJECT_RSA_KEY_SHORT == 1) && (TLS_CA_REJECT_CURVE == 3) && (TLS_CA_REJECT_NOT_CA == 4),
               "rpc_tls_set_ca_resp.reject");
_Static_assert(TLS_SNI_MAX_LEN <= 255, "sni_len is a byte");
#endif /* __cplusplus */

#endif /* RPC_TLS_H_ */
