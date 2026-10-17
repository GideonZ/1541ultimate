// wifi_tls.h: TLS sessions run on the WiFi module (U64 only). It holds the keys and runs the protocol; this side moves
// the ciphertext between a socket and the module, one step at a time, and the module never speaks unasked. Apart
// from wifi_cmd.h; wifi_tls.cc asserts the values repeated here match rpc_tls.h's layout.

#ifndef WIFI_TLS_H_
#define WIFI_TLS_H_

#include <stdint.h>

#define WIFI_TLS_MAX_SESSIONS   2    // the module's cap
#define WIFI_TLS_MAX_SNI        253
#define WIFI_TLS_STEP_MAX_IN    1536 // ct_in_len + pt_in_len in one step
#define WIFI_TLS_STEP_MAX_OUT   1024 // ct_out_len + pt_out_len in one step
#define WIFI_TLS_CA_MAX         1472 // one DER certificate for SET_CA
#define WIFI_TLS_PIN_LEN        32
#define WIFI_TLS_SLOT_GLOBAL    0xFF // for wifi_tls_stats: the module as a whole, with tag 0

#define WIFI_TLS_TRUST_BUNDLE   0
#define WIFI_TLS_TRUST_CUSTOM   1
#define WIFI_TLS_TRUST_PIN      2

#define WIFI_TLS_OPT_REQUIRE_TLS13 0x01 // offer TLS 1.3 only
#define WIFI_TLS_STEP_NO_READ      0x01 // the module takes no plaintext out of the session in this step

// Why an OPEN was refused.
#define WIFI_TLS_REASON_NO_ENTROPY   1 // the radio has not started, so there is no seed
#define WIFI_TLS_REASON_NO_TIME      2 // BUNDLE or CUSTOM, and neither side has a trustworthy clock
#define WIFI_TLS_REASON_NO_CUSTOM_CA 3 // CUSTOM, and the module holds no CA under the given ca_tag

// Where the time in an OPEN came from.
#define WIFI_TLS_TIME_NONE 0
#define WIFI_TLS_TIME_NIOS 1 // SNTP on this side

#define WIFI_TLS_HANDSHAKING    0
#define WIFI_TLS_ESTABLISHED    1
#define WIFI_TLS_CLOSED         2
#define WIFI_TLS_FAILED         3 // esp_err holds the mbedTLS error, fail_reason says more
#define WIFI_TLS_UNKNOWN        0xFF // the answer did not say

// Why a step FAILED, besides esp_err.
#define WIFI_TLS_FAIL_NONE            0 // generic: esp_err has the mbedTLS code, alert_in any alert
#define WIFI_TLS_FAIL_VERSION_REFUSED 1 // the version chosen is in verify_flags' low 16 bits
#define WIFI_TLS_FAIL_DOWNGRADE       2
#define WIFI_TLS_FAIL_NO_EMS          3
#define WIFI_TLS_FAIL_CERT_VERIFY     4 // verify_flags is the final verify result
#define WIFI_TLS_FAIL_PIN_MISMATCH    5
#define WIFI_TLS_FAIL_PEER_CLOSED     6

#define WIFI_TLS_ALERT_NONE     0xFF

// Why SET_CA refused a certificate that parsed.
#define WIFI_TLS_REJECT_NONE          0
#define WIFI_TLS_REJECT_RSA_KEY_SHORT 1
#define WIFI_TLS_REJECT_CURVE         3
#define WIFI_TLS_REJECT_NOT_CA        4

// esp_err values the callers tell apart (esp_err.h).
#define WIFI_TLS_ESP_FAIL          (-1)
#define WIFI_TLS_ESP_NO_MEM        0x101
#define WIFI_TLS_ESP_INVALID_ARG   0x102
#define WIFI_TLS_ESP_INVALID_STATE 0x103
#define WIFI_TLS_ESP_NOT_FOUND     0x105
#define WIFI_TLS_ESP_NOT_SUPPORTED 0x106

// What the calls below return besides 0.
#define WIFI_TLS_BUSY      1 // sent, but no answer in time, or cancelled; the module may still act on it
#define WIFI_TLS_REFUSED   2 // the module answered with an error, which is in esp_err
#define WIFI_TLS_INVALID   3 // the answer is not a well formed reply
#define WIFI_TLS_NO_MODULE 4 // the module application is not running; nothing was sent
#define WIFI_TLS_FOREIGN   5 // (from the checks only) not the answer to this request
#define WIFI_TLS_UNSENT    6 // another request was outstanding, or no buffer; nothing was sent
#define WIFI_TLS_TOO_LONG  7 // the request does not fit; nothing was sent
#define WIFI_TLS_AGAIN     8 // STEP only: the module had no room and did not take the input, but its
                             // output is handed over as for 0; send the same input again

// Polled between the slices of a wait; true ends the wait with BUSY. NULL
// for no cancelling.
typedef struct {
    bool (*cancelled)(void *context);
    void *context;
} wifi_tls_cancel_t;

typedef struct {
    uint8_t trust;        // WIFI_TLS_TRUST_*
    uint8_t slot;         // 0 .. WIFI_TLS_MAX_SESSIONS - 1
    uint16_t tag;         // never 0; names the session together with slot
    uint8_t opts;         // WIFI_TLS_OPT_*
    uint8_t time_source;  // WIFI_TLS_TIME_*
    uint32_t unix_time;   // UTC seconds, 0 without a time source
    uint16_t ca_tag;      // CUSTOM: the CA the module must hold; 0 otherwise
    const uint8_t *pin;   // PIN: WIFI_TLS_PIN_LEN bytes; NULL otherwise (sent as zeros)
    const char *sni;      // the server name, or NULL for none
    uint8_t sni_len;
} wifi_tls_open_t;

typedef struct {
    uint32_t heap_free;
    uint32_t heap_min_ever;
    uint32_t largest_block;
    uint32_t ct_in_bytes; // the session's counters; 0 for the module as a whole
    uint32_t ct_out_bytes;
    uint32_t pt_in_bytes;
    uint32_t pt_out_bytes;
    uint32_t handshake_us;
    uint16_t tls_version; // as on the wire: 0x0304 is TLS 1.3
    uint16_t ciphersuite; // IANA number
    uint16_t group;       // IANA NamedGroup
    uint8_t state;
    uint8_t sessions_open;
    uint32_t stack_free;
    uint32_t esp_time;
    uint8_t time_source;
    uint8_t entropy_source; // 0: no live entropy source
    uint8_t ems;          // 1: extended master secret in use
    uint16_t block_max_ms; // the longest the module's dispatcher was blocked since boot
    uint8_t spki_sha256[32]; // of the leaf's SubjectPublicKeyInfo, digest order
} wifi_tls_stats_t;

// One step; the module answers into the second block. Input is taken all or nothing: no room is AGAIN with the output
// handed over and the same input to resend (0 and CLOSED if the peer closed). FAILED is an answer (0, reasons set,
// session freed). Outputs only on 0/AGAIN; plaintext only from ESTABLISHED or just CLOSED; with NO_READ it is invalid.
typedef struct {
    uint8_t flags;        // WIFI_TLS_STEP_*
    const uint8_t *ct_in;
    uint16_t ct_in_len;
    const uint8_t *pt_in;
    uint16_t pt_in_len;
    uint8_t *ct_out;
    uint16_t ct_out_room;
    uint8_t *pt_out;
    uint16_t pt_out_room;

    uint16_t ct_out_len;
    uint16_t pt_out_len;
    uint8_t state;        // WIFI_TLS_HANDSHAKING .. WIFI_TLS_FAILED, or WIFI_TLS_UNKNOWN
    int esp_err;
    uint8_t alert_in;     // WIFI_TLS_ALERT_NONE when none
    uint8_t alert_out;
    uint8_t fail_reason;  // WIFI_TLS_FAIL_*
    uint32_t verify_flags;
} wifi_tls_step_t;

// Each returns 0 or a code above after at most timeout_ms, or BUSY earlier when cancel says so. esp_err (where there
// is one), reason and reject are set to 0 first, then to what the module answered when it failed or refused.
int wifi_tls_open(const wifi_tls_open_t *open, uint32_t timeout_ms, const wifi_tls_cancel_t *cancel, int *esp_err,
                  uint8_t *reason);
int wifi_tls_step(uint8_t slot, uint16_t tag, wifi_tls_step_t *step, uint32_t timeout_ms,
                  const wifi_tls_cancel_t *cancel);
// ct_out receives the close_notify record, at most room bytes; *ct_out_len is 0 without one.
int wifi_tls_close(uint8_t slot, uint16_t tag, uint8_t *ct_out, uint16_t room, uint16_t *ct_out_len, int *esp_err,
                   uint32_t timeout_ms, const wifi_tls_cancel_t *cancel);
int wifi_tls_stats(uint8_t slot, uint16_t tag, wifi_tls_stats_t *stats, int *esp_err, uint32_t timeout_ms,
                   const wifi_tls_cancel_t *cancel);
int wifi_tls_close_all(int *esp_err, uint32_t timeout_ms);
int wifi_tls_set_ca(const uint8_t *der, uint16_t der_len, uint16_t ca_tag, int *esp_err, uint8_t *reject,
                    uint32_t timeout_ms, const wifi_tls_cancel_t *cancel);

// The answer checks, apart for host tests. Exact sizes: OPEN 12, CLOSE 12 + record, STATS 96, CLOSE_ALL 8, SET_CA 12,
// STEP 20 + its bytes; a refusal may be a bare 8-byte rpc_espcmd_resp (no TLS: NOT_SUPPORTED). Each returns 0,
// WIFI_TLS_FOREIGN, REFUSED or INVALID (or AGAIN for a step). OPEN's answer must name the slot asked for.
int wifi_tls_take_open(const uint8_t *reply, int size, uint16_t sequence, uint8_t slot, int *esp_err,
                       uint8_t *reason);
int wifi_tls_take_step(const uint8_t *reply, int size, uint16_t sequence, wifi_tls_step_t *step);
int wifi_tls_take_close(const uint8_t *reply, int size, uint16_t sequence, uint8_t *ct_out, uint16_t room,
                        uint16_t *ct_out_len, int *esp_err);
int wifi_tls_take_stats(const uint8_t *reply, int size, uint16_t sequence, wifi_tls_stats_t *stats, int *esp_err);
int wifi_tls_take_close_all(const uint8_t *reply, int size, uint16_t sequence, int *esp_err);
int wifi_tls_take_set_ca(const uint8_t *reply, int size, uint16_t sequence, int *esp_err, uint8_t *reject);

// The UART buffer generation, which changes when the module restarts: every
// session it held is then gone. In wifi_dedicated.cc.
uint32_t wifi_tls_generation(void);

// Whether TLS calls can be sent at all (the module application runs and WiFi
// is not disabled), and the version the module reported. In wifi.cc.
bool wifi_tls_module_available(void);
void wifi_tls_module_version(uint16_t *major, uint16_t *minor);

#endif /* WIFI_TLS_H_ */
