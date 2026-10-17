/* tls_engine.h: TLS client sessions whose ciphertext the Ultimate carries; the ESP32 never touches a socket.
 * Every call is lock-step, and all of them run on the TLS task only. */

#ifndef TLS_ENGINE_H_
#define TLS_ENGINE_H_

#include <stdint.h>
#include "rpc_calls.h"

#define TLS_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) // what mbedTLS allocates from (esp_heap_caps.h)

typedef struct {
    uint32_t ct_in_bytes;
    uint32_t ct_out_bytes;
    uint32_t pt_in_bytes;
    uint32_t pt_out_bytes;
    uint32_t handshake_us;
    uint16_t tls_version;
    uint16_t ciphersuite;
    uint16_t group;
    uint8_t state;
    uint8_t time_source;
    uint8_t ems;
    uint32_t esp_time;
    uint8_t spki_sha256[TLS_PIN_LEN];
} tls_session_stats_t;

typedef struct {
    uint8_t trust;        // tls_trust_t
    uint8_t opts;         // TLS_OPT_*
    uint8_t time_source;  // TLS_TIME_NONE or TLS_TIME_NIOS: whether unix_time is a clock
    uint16_t ca_tag;      // CUSTOM: must equal the held CA's
    uint32_t unix_time;
    const uint8_t *pin;   // TLS_PIN_LEN bytes, read for PIN only
    const char *sni;      // sni_len bytes, no terminator; none (sni_len 0) only with PIN
    int sni_len;
} tls_open_params_t;

typedef struct {
    uint8_t state;        // tls_state_t
    uint8_t alert_in;     // TLS_ALERT_NONE unless a fatal alert was received
    uint8_t alert_out;    // TLS_ALERT_NONE unless this engine sent an unencrypted fatal alert
    uint8_t fail_reason;  // tls_fail_reason_t, with FAILED
    uint32_t verify_flags;
} tls_step_result_t;

// All return ESP_OK, an esp_err_t, or a negative mbedTLS code. A (slot, tag) that is not the live session in that slot
// gets ESP_ERR_NOT_FOUND and touches nothing. A session is freed on FAILED, on CLOSED, by tls_close() and
// tls_close_all(), and when an OPEN names its slot again.

// unix_time sets the ESP32 clock (time_source TLS_TIME_NIOS, and no SNTP time of its own). BUNDLE and CUSTOM need a
// trustworthy clock: without one OPEN fails with ESP_ERR_INVALID_STATE, reason NO_TIME.
int tls_open(uint8_t slot, uint16_t tag, const tls_open_params_t *params, uint8_t *reason);

// Consumes all of ct_in (pt_in only when ESTABLISHED) or none, then writes ct_out and pt_out (no pt with NO_READ) to out,
// at most TLS_STEP_OUT_MAX; out may overlap the inputs. ESP_ERR_NO_MEM: buffer full, nothing consumed, output returned,
// resend. INVALID_ARG/INVALID_STATE/NOT_FOUND: nothing consumed or returned. A negative code comes with state FAILED.
int tls_step(uint8_t slot, uint16_t tag, uint8_t flags, const uint8_t *ct_in, int ct_in_len, const uint8_t *pt_in,
             int pt_in_len, uint8_t *out, uint16_t *ct_out_len, uint16_t *pt_out_len, tls_step_result_t *result);

// Frees the session; out gets its pending ciphertext and a close_notify (at most TLS_STEP_OUT_MAX)
int tls_close(uint8_t slot, uint16_t tag, uint8_t *out, uint16_t *ct_out_len);
// Frees every session and the CUSTOM CA
void tls_close_all(void);
// Parses one DER certificate as the CUSTOM CA under ca_tag, replacing the one held. On any error none is held.
int tls_set_ca(const uint8_t *der, int der_len, uint16_t ca_tag, uint8_t *reject);
// slot TLS_SLOT_GLOBAL with tag 0: the clock only
int tls_get_stats(uint8_t slot, uint16_t tag, tls_session_stats_t *stats);
int tls_sessions_open(void);

#endif /* TLS_ENGINE_H_ */
