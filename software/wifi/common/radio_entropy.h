/*
 * radio_entropy.h
 *
 * What feeds the hardware RNG of the WiFi modules, with the radio on or off.
 *
 * esp_fill_random() gives true random numbers only while Wi-Fi runs or while the
 * bootloader_random SAR ADC source is on (esp_random.h). The bootloader switches that
 * source off before the application starts. So with the radio off, this enables it again
 * and reads the hardware back, and only a source that read back correctly is reported as
 * ENTROPY_ADC.
 *
 * A writer-preferring reader/writer gate keeps those transitions away from the draws:
 *  - shared holders: a GET_RANDOM draw;
 *  - exclusive holders: a radio start or stop (wifi_radio.c), a read of the voltages that
 *    needs the SAR ADC, and a retry of a failed readback.
 * Rules for every holder: no re-acquire (it is refused, never waited for), no UART, queue
 * or event-loop wait while holding, and event handlers never take the gate.
 *
 * Built into both ESP modules (software/wifi/raw_u64 and software/u64ctrl). The logic is
 * in radio_entropy.c, the chip side in radio_entropy_esp.c, and the host test
 * (target/pc/linux/radioentropy) replaces the latter.
 */

#ifndef SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_H_
#define SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rpc_calls.h"
#include "rpc_shared.h" // entropy_source_t; the U2+L module's rpc_calls.h does not include it

// Results; the values are those of the esp_err_t they are passed on as (asserted in
// radio_entropy_esp.c)
#define RE_OK            0
#define RE_ERR_BUSY      0x107 // ESP_ERR_TIMEOUT: the gate was not free in time
#define RE_ERR_RECURSION 0x103 // ESP_ERR_INVALID_STATE: the caller holds the gate already

// esp_err_t values the GET_RANDOM answer uses (asserted in radio_entropy_esp.c)
#define RE_ESP_ERR_INVALID_ARG   0x102
#define RE_ESP_ERR_INVALID_STATE 0x103

// How long a caller waits for the gate
#define RE_WAIT_RANDOM_MS     20   // a GET_RANDOM draw
#define RE_WAIT_TRANSITION_MS 5000 // a radio start or stop
#define RE_WAIT_ADC_MS        50   // the voltage read, which gives up and uses its cache
#define RE_WAIT_RETRY_MS      50

// Readback attempts after the first one failed, per transition
#define RE_RETRIES 3

// The source now feeding the RNG, and a count of the radio's stops so far
entropy_source_t entropy_source(void);
uint32_t entropy_epoch(void);

// Once, after wifi_init() and before the first RPC is served. The U64 module starts with
// its radio off, so it enables the ADC source here; a boot that started the radio does not.
void radio_entropy_init(void);

// Around every radio start and stop, by wifi_radio.c only. begin takes the gate
// exclusively: RE_OK, RE_ERR_BUSY or RE_ERR_RECURSION. changing() is called when the
// radio is about to change: the source goes off for the change. changed() gives the state
// the radio is in afterwards: ENTROPY_RADIO when on, else the ADC source, read back. end
// releases the gate.
int radio_entropy_change_begin(void);
void radio_entropy_changing(bool starting);
void radio_entropy_changed(bool radio_on);
void radio_entropy_change_end(void);

// Shared hold of the gate. Returns RE_OK, RE_ERR_BUSY after timeout_ms, or
// RE_ERR_RECURSION. Every RE_OK is matched by one release by the same task.
int radio_entropy_acquire_shared(uint32_t timeout_ms);
void radio_entropy_release_shared(void);

// Exclusive hold around a use of the SAR ADC1 by someone else (the voltage read). Takes
// the gate, and switches the ADC source off if it is on. The end enables the source again
// and reads it back. RE_OK or RE_ERR_BUSY.
int radio_entropy_adc_begin(uint32_t timeout_ms);
void radio_entropy_adc_end(void);

// True while the calling task holds the gate exclusively
bool radio_entropy_held_exclusive(void);

// The whole of a GET_RANDOM answer, up to its transmission: data is the request on the way
// in (size bytes) and the answer on the way out, whose size is returned. fill is
// esp_fill_random(). The gate is held shared for the draw only and is free when this
// returns, so the caller transmits with it released.
int radio_entropy_serve_get_random(void *data, int size, void (*fill)(void *, size_t));

// Reads n voltages with the SAR ADC lent out for the read (radio_entropy_adc_begin), through
// read_unit, which returns 0 or an error. cache holds the last good values, and *valid says
// it does. When the ADC is not free in time the cache is given, never zeros; while there is
// no cache yet, the wait is as long as a radio change's. Returns 0, or an error when there
// is nothing to give: out is then left alone.
int radio_entropy_read_adc(uint16_t *out, uint16_t *cache, bool *valid, int n,
                           int (*read_unit)(uint16_t *));

// Retries a failed readback, at most RE_RETRIES times per transition. Call it about every
// 10 s from a task that may wait.
void radio_entropy_poll(void);

// The chip side, radio_entropy_esp.c only: a task for radio_entropy_poll(), for a module
// that has no loop of its own
void radio_entropy_start_poll_task(void);

typedef struct {
    uint32_t readback_failures; // enables whose readback did not match
    uint32_t retries;           // attempts made by radio_entropy_poll()
    uint32_t recursion;         // acquires refused
    uint32_t busy;              // acquires that timed out
    uint32_t adc_hold_max_ms;   // longest radio_entropy_adc_begin() to _end()
} radio_entropy_stats_t;

void radio_entropy_get_stats(radio_entropy_stats_t *stats);

#endif /* SOFTWARE_WIFI_COMMON_RADIO_ENTROPY_H_ */
