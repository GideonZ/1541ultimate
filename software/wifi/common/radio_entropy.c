/*
 * radio_entropy.c
 *
 * See radio_entropy.h. Free of ESP-IDF and FreeRTOS, so that the host test tests what
 * ships: everything the chip and the RTOS do goes through radio_entropy_port.h.
 */

#include <string.h>
#include "radio_entropy.h"
#include "radio_entropy_port.h"
#include "wifi_radio.h"

#define MAX_READERS 8

// The gate. All of it is changed under re_port_lock() only, and a waiter polls, so that no
// state needs a wake up, and a waiter that gives up leaves nothing behind.
static struct {
    int readers;
    bool writer_active;
    int writers_pending;
    uintptr_t writer_task;
    uintptr_t reader_task[MAX_READERS]; // 0 for a free slot
} gate;

// Changed under the exclusive hold, read by anyone
static volatile entropy_source_t source = ENTROPY_NONE;

// Changed under the exclusive hold only
static bool hw_enabled;       // bootloader_random_enable() was called, and no disable since
static bool adc_off;          // radio_entropy_adc_begin() switched the source off
static uint32_t adc_begin_ms;
static bool retry_pending;
static int retries_left;

static radio_entropy_stats_t stats;

// A line for the log, set under the exclusive hold and written after it is released: a
// holder does not touch the UART
static const char *pending_log;

entropy_source_t entropy_source(void)
{
    return source;
}

uint32_t entropy_epoch(void)
{
    return radio_epoch();
}

static bool expired(uint32_t deadline)
{
    return (int32_t)(re_port_now_ms() - deadline) >= 0;
}

// Under re_port_lock()
static bool holds(uintptr_t me)
{
    if (gate.writer_active && (gate.writer_task == me)) {
        return true;
    }
    for (int i = 0; i < MAX_READERS; i++) {
        if (gate.reader_task[i] == me) {
            return true;
        }
    }
    return false;
}

int radio_entropy_acquire_shared(uint32_t timeout_ms)
{
    uintptr_t me = re_port_task();
    uint32_t deadline = re_port_now_ms() + timeout_ms;
    for (;;) {
        re_port_lock();
        if (holds(me)) {
            stats.recursion++;
            re_port_unlock();
            return RE_ERR_RECURSION;
        }
        // A pending writer keeps new readers out, so that back to back readers
        // cannot starve a radio change
        if (!gate.writer_active && (gate.writers_pending == 0)) {
            for (int i = 0; i < MAX_READERS; i++) {
                if (gate.reader_task[i] == 0) {
                    gate.reader_task[i] = me;
                    gate.readers++;
                    re_port_unlock();
                    return RE_OK;
                }
            }
        }
        re_port_unlock();
        if (expired(deadline)) {
            re_port_lock();
            stats.busy++;
            re_port_unlock();
            return RE_ERR_BUSY;
        }
        re_port_delay_ms(1);
    }
}

void radio_entropy_release_shared(void)
{
    uintptr_t me = re_port_task();
    re_port_lock();
    for (int i = 0; i < MAX_READERS; i++) {
        if (gate.reader_task[i] == me) {
            gate.reader_task[i] = 0;
            gate.readers--;
            break;
        }
    }
    re_port_unlock();
}

static int acquire_exclusive(uint32_t timeout_ms)
{
    uintptr_t me = re_port_task();
    uint32_t deadline = re_port_now_ms() + timeout_ms;

    re_port_lock();
    if (holds(me)) {
        stats.recursion++;
        re_port_unlock();
        return RE_ERR_RECURSION;
    }
    gate.writers_pending++;
    re_port_unlock();

    for (;;) {
        re_port_lock();
        if (!gate.writer_active && (gate.readers == 0)) {
            gate.writer_active = true;
            gate.writer_task = me;
            gate.writers_pending--;
            re_port_unlock();
            return RE_OK;
        }
        re_port_unlock();
        if (expired(deadline)) {
            re_port_lock();
            gate.writers_pending--;
            stats.busy++;
            re_port_unlock();
            return RE_ERR_BUSY;
        }
        re_port_delay_ms(1);
    }
}

static void release_exclusive(void)
{
    re_port_lock();
    gate.writer_active = false;
    gate.writer_task = 0;
    const char *line = pending_log;
    pending_log = NULL;
    re_port_unlock();
    if (line) {
        re_port_log("%s", line);
    }
}

bool radio_entropy_held_exclusive(void)
{
    re_port_lock();
    bool mine = gate.writer_active && (gate.writer_task == re_port_task());
    re_port_unlock();
    return mine;
}

//----------------------------------
// The source. Everything below runs under the exclusive hold.

// Switches the ADC source off if it is on
static void disable_source(void)
{
    if (hw_enabled) {
        re_port_random_disable();
        hw_enabled = false;
    }
}

// Only a source that reads back correctly is ENTROPY_ADC. The first attempt of a transition
// starts the retry budget; nothing else gives it back.
static void enable_source(bool first)
{
    if (first) {
        retries_left = RE_RETRIES;
    }
    re_port_random_enable();
    hw_enabled = true;
    if (re_port_random_readback()) {
        source = ENTROPY_ADC;
        retry_pending = false;
    } else {
        source = ENTROPY_NONE;
        stats.readback_failures++;
        retry_pending = true;
        // Once the retries are spent there is nothing more to say
        if (first || (retries_left > 0)) {
            pending_log = "radio_entropy: ADC source readback mismatch";
        }
    }
}

void radio_entropy_init(void)
{
    if (acquire_exclusive(RE_WAIT_TRANSITION_MS) != RE_OK) {
        return;
    }
    if (!radio_is_on() && !hw_enabled) {
        enable_source(true);
    }
    release_exclusive();
}

int radio_entropy_change_begin(void)
{
    return acquire_exclusive(RE_WAIT_TRANSITION_MS);
}

void radio_entropy_changing(bool starting)
{
    source = ENTROPY_NONE;
    if (starting) {
        // The radio and the ADC source do not run together
        disable_source();
    }
}

void radio_entropy_changed(bool radio_on)
{
    if (radio_on) {
        source = ENTROPY_RADIO;
    } else {
        enable_source(true);
    }
}

void radio_entropy_change_end(void)
{
    release_exclusive();
}

int radio_entropy_adc_begin(uint32_t timeout_ms)
{
    int rc = acquire_exclusive(timeout_ms);
    if (rc != RE_OK) {
        return rc;
    }
    adc_begin_ms = re_port_now_ms();
    // With the radio running the RNG does not depend on the ADC, and the ADC is free as it is
    adc_off = hw_enabled;
    if (adc_off) {
        source = ENTROPY_NONE;
        disable_source();
    }
    // Also with the radio on: an enable since the unit was created leaves the clock off
    re_port_adc_ready();
    return RE_OK;
}

void radio_entropy_adc_end(void)
{
    if (!radio_entropy_held_exclusive()) {
        re_port_log("radio_entropy: adc_end without adc_begin");
        return;
    }
    if (adc_off) {
        adc_off = false;
        // The retries of a transition are spent once, by whoever needs them: a source that
        // went bad does not get a new budget from a voltage read
        bool failing = retry_pending;
        if (!failing || (retries_left > 0)) {
            if (failing) {
                retries_left--;
                stats.retries++;
            }
            enable_source(false);
        }
    }
    uint32_t held = re_port_now_ms() - adc_begin_ms;
    if (held > stats.adc_hold_max_ms) {
        stats.adc_hold_max_ms = held;
    }
    release_exclusive();
}

void radio_entropy_poll(void)
{
    // A look without the gate; the decision is made again under it
    if (!retry_pending || (retries_left <= 0) || radio_is_on()) {
        return;
    }
    if (acquire_exclusive(RE_WAIT_RETRY_MS) != RE_OK) {
        return;
    }
    if (retry_pending && (retries_left > 0) && !radio_is_on()) {
        retries_left--;
        stats.retries++;
        source = ENTROPY_NONE;
        disable_source();
        enable_source(false);
    }
    release_exclusive();
}

void radio_entropy_get_stats(radio_entropy_stats_t *out)
{
    re_port_lock();
    *out = stats;
    re_port_unlock();
}

//----------------------------------
// The answer to GET_RANDOM, and the voltage read. Here so that the host test runs them.

int radio_entropy_serve_get_random(void *data, int size, void (*fill)(void *, size_t))
{
    rpc_get_random_req *req = (rpc_get_random_req *)data;
    rpc_get_random_resp *resp = (rpc_get_random_resp *)data;

    // The response overlays the request, so take what we need from it first
    uint16_t length = (size >= (int)sizeof(rpc_get_random_req)) ? req->length : 0;

    // Clear the fixed part, padding included, so no stale buffer bytes go out
    memset(&resp->esp_err, 0, sizeof(rpc_get_random_resp) - sizeof(rpc_header_t));
    if ((length == 0) || (length > RANDOM_MAX_BYTES)) {
        resp->esp_err = RE_ESP_ERR_INVALID_ARG;
    } else if (radio_entropy_acquire_shared(RE_WAIT_RANDOM_MS) != RE_OK) {
        // A radio change or a read of the voltages holds the source. Nothing is drawn.
        resp->esp_err = RPC_ERR_ENTROPY_BUSY;
    } else {
        uint32_t epoch = entropy_epoch();
        entropy_source_t src = entropy_source();
        resp->source = src;
        if (src == ENTROPY_NONE) {
            resp->esp_err = RE_ESP_ERR_INVALID_STATE;
        } else {
            fill(resp->data, length);
            // The gate keeps the radio from changing while it is held; this is a second guard
            if ((entropy_source() == ENTROPY_NONE) || (entropy_epoch() != epoch)) {
                resp->source = ENTROPY_NONE;
                resp->esp_err = RE_ESP_ERR_INVALID_STATE;
            } else {
                resp->esp_err = 0;
                resp->length = length;
            }
        }
        radio_entropy_release_shared();
    }
    return sizeof(rpc_get_random_resp) + resp->length;
}

int radio_entropy_read_adc(uint16_t *out, uint16_t *cache, bool *valid, int n,
                           int (*read_unit)(uint16_t *))
{
    uint16_t fresh[16];
    if (n > 16) {
        return RE_ESP_ERR_INVALID_ARG;
    }
    re_port_lock();
    bool have = *valid;
    re_port_unlock();

    if (radio_entropy_adc_begin(have ? RE_WAIT_ADC_MS : RE_WAIT_TRANSITION_MS) != RE_OK) {
        if (!have) {
            return RE_ERR_BUSY;
        }
        re_port_lock();
        memcpy(out, cache, n * sizeof(uint16_t));
        re_port_unlock();
        return 0;
    }
    int rc = read_unit(fresh);
    radio_entropy_adc_end();

    re_port_lock();
    if (rc == 0) {
        memcpy(cache, fresh, n * sizeof(uint16_t));
        *valid = true;
    }
    have = *valid;
    if (have) {
        memcpy(out, cache, n * sizeof(uint16_t));
    }
    re_port_unlock();
    return rc;
}
