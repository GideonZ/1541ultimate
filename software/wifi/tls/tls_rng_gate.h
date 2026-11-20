/* tls_rng_gate.h: keeps the radio's entropy source on for the draws of the TLS task.
 *
 * mbedTLS 4 draws every random byte through mbedtls_psa_external_get_random() (MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG), which
 * is esp_fill_random() here. esp_fill_random() is random only while the radio runs or the ADC source is on, and a radio
 * start or stop or a read of the voltages switches the source off (radio_entropy.h). The module's link wraps that function
 * (-Wl,--wrap=mbedtls_psa_external_get_random, in the component's CMakeLists.txt) with tls_rng_gate.c:
 *  - a draw made on the TLS task holds the entropy gate shared for that one draw, which is microseconds, so a radio change
 *    never waits for ECC or RSA work; with no source, or no gate within the wait below, the draw fails with
 *    PSA_ERROR_INSUFFICIENT_ENTROPY and the engine ends the session with TLS_FAIL_NO_ENTROPY;
 *    The wait is RE_WAIT_ADC_MS (50 ms) rounded up to whole ticks (10 ms on the module) plus one tick, 60 ms: margin over
 *    the voltage read's hold of the gate, which nothing bounds (radio_entropy_stats_t.adc_hold_max_ms records it);
 *  - a draw made on any other task passes straight through, so the Wi-Fi stack's draws are never held up by it.
 */
#ifndef SOFTWARE_WIFI_TLS_TLS_RNG_GATE_H_
#define SOFTWARE_WIFI_TLS_TLS_RNG_GATE_H_

// The TLS task: its draws are the ones that are gated. Idempotent and cheap; tls_open() calls it first, on the TLS task, so
// that the binding is part of the engine and a test of the engine exercises it. A draw made before it is not gated
// (the Wi-Fi stack draws before the TLS task exists, and must never be held).
void tls_rng_gate_bind_task(void);

#ifdef TLS_RNG_GATE_HOST_TEST
// Host harness only: forgets the binding, so that a case starts unbound and only tls_open() can bind it.
void tls_rng_gate_unbind_task_for_test(void);
#endif

// 1 once if a draw was refused since the last call (no source, or the gate not free in time), and clears it
int tls_rng_gate_take_refusal(void);

#endif /* SOFTWARE_WIFI_TLS_TLS_RNG_GATE_H_ */
