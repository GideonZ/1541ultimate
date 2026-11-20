// Host test of radio_entropy.c, against a model of the chip that counts every call the
// hardware would not allow as a violation:
//   - bootloader_random_enable while the radio runs or while the source is on already;
//   - bootloader_random_disable while it is off;
//   - the Wi-Fi start while the source is on or the radio runs, the Wi-Fi stop while it is off;
//   - any chip call made without the gate held exclusively, or a log line written with it held;
//   - a source other than NONE reported while the radio starts or stops.
// The radio is started and stopped through wifi_radio.c, which is built in too.
//   make -C target/pc/linux/radioentropy test
// Each case runs in a process of its own, because the helper keeps its state in statics.
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "radio_entropy.h"
#include "radio_entropy_port.h"
#include "wifi_radio.h"
#include "wifi_radio_port.h"

static int failures;

#define CHECK(cond, what) do { \
        if (!(cond)) { failures++; printf("    FAIL  %s  (line %d)\n", what, __LINE__); } \
    } while (0)

//----------------------------------
// The model

static pthread_mutex_t lock_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile bool wifi_running;
static volatile bool hw_on;       // the source is enabled
static volatile bool hw_good = true; // the registers read back correctly while it is on
static volatile int violations;
static volatile int readback_failing; // this many readbacks fail
static volatile int wifi_start_fails;
static volatile int wifi_stop_fails;
static volatile int readbacks;
static char trace[256];
static pthread_mutex_t trace_mutex = PTHREAD_MUTEX_INITIALIZER;

static void note(char c)
{
    pthread_mutex_lock(&trace_mutex);
    size_t n = strlen(trace);
    if (n + 1 < sizeof(trace)) {
        trace[n] = c;
        trace[n + 1] = 0;
    }
    pthread_mutex_unlock(&trace_mutex);
}

static void violate(const char *what)
{
    violations++;
    printf("    violation: %s\n", what);
}

// Every chip call comes with the exclusive hold.
static void need_gate(const char *what)
{
    if (!radio_entropy_held_exclusive()) {
        violate(what);
    }
}

void re_port_lock(void) { pthread_mutex_lock(&lock_mutex); }
void re_port_unlock(void) { pthread_mutex_unlock(&lock_mutex); }

uint32_t re_port_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void re_port_delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

uintptr_t re_port_task(void)
{
    return (uintptr_t)pthread_self();
}

int wr_port_wifi_start(void)
{
    need_gate("wifi start without the gate");
    if (entropy_source() != ENTROPY_NONE) violate("a source reported while the radio starts");
    if (wifi_running) violate("wifi start while running");
    if (hw_on) violate("wifi start with the ADC source on");
    note('S');
    if (wifi_start_fails > 0) {
        wifi_start_fails--;
        return 0x101;
    }
    wifi_running = true;
    return 0;
}

int wr_port_wifi_stop(void)
{
    need_gate("wifi stop without the gate");
    if (entropy_source() != ENTROPY_NONE) violate("a source reported while the radio stops");
    if (!wifi_running) violate("wifi stop while not running");
    note('P');
    if (wifi_stop_fails > 0) {
        wifi_stop_fails--;
        return 0x101;
    }
    wifi_running = false;
    return 0;
}

static volatile bool bus_clk_off; // as bootloader_random_enable() leaves it on the S3

void re_port_random_enable(void)
{
    need_gate("enable without the gate");
    if (wifi_running) violate("enable while the radio runs");
    if (hw_on) violate("enable twice");
    note('E');
    hw_on = true;
    bus_clk_off = true;
}

void re_port_adc_ready(void)
{
    need_gate("ADC readied without the gate");
    bus_clk_off = false;
}

void re_port_random_disable(void)
{
    need_gate("disable without the gate");
    if (!hw_on) violate("disable while off");
    note('D');
    hw_on = false;
}

bool re_port_random_readback(void)
{
    need_gate("readback without the gate");
    readbacks++;
    if (readback_failing > 0) {
        readback_failing--;
        return false;
    }
    return hw_on && hw_good;
}

static volatile int log_lines;

void re_port_log(const char *fmt, ...)
{
    if (radio_entropy_held_exclusive()) violate("log line written with the gate held");
    log_lines++;
}

static void reset_trace(void)
{
    pthread_mutex_lock(&trace_mutex);
    trace[0] = 0;
    pthread_mutex_unlock(&trace_mutex);
}

static bool trace_is(const char *expected)
{
    if (strcmp(trace, expected) != 0) {
        printf("    trace \"%s\", expected \"%s\"\n", trace, expected);
        return false;
    }
    return true;
}

//----------------------------------
// The cases

static void boot_without_radio(void)
{
    // The bootloader left the source off. A boot that does not start the radio turns it on.
    radio_entropy_init();
    CHECK(trace_is("E"), "enable once");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC after a passing readback");
    CHECK(violations == 0, "no violations");
    radio_entropy_init();
    CHECK(trace_is("E"), "a second init does nothing");
}

static void boot_with_radio(void)
{
    CHECK(entropy_source() == ENTROPY_NONE, "NONE before anything");
    CHECK(radio_start() == 0, "start");
    CHECK(trace_is("S"), "no disable or enable on a boot that never had the source on");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO");
    radio_entropy_init();
    CHECK(trace_is("S"), "init does nothing with the radio running");
    CHECK(violations == 0, "no violations");
}

static void boot_readback_fails_then_retries(void)
{
    // NONE until a readback passes; never ADC without one
    readback_failing = 2;
    radio_entropy_init();
    CHECK(entropy_source() == ENTROPY_NONE, "NONE after a failed readback");
    radio_entropy_poll();
    CHECK(entropy_source() == ENTROPY_NONE, "still NONE after the first retry fails");
    radio_entropy_poll();
    CHECK(entropy_source() == ENTROPY_ADC, "ADC once a retry reads back");
    CHECK(trace_is("EDEDE"), "each retry disables, then enables");
    radio_entropy_poll();
    CHECK(trace_is("EDEDE"), "no more retries once it passed");
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.readback_failures == 2, "two failures counted");
    CHECK(st.retries == 2, "two retries counted");
    CHECK(violations == 0, "no violations");
}

static void retries_are_bounded(void)
{
    readback_failing = 100;
    radio_entropy_init();
    for (int i = 0; i < 10; i++) {
        radio_entropy_poll();
    }
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.retries == RE_RETRIES, "RE_RETRIES retries, then no more");
    CHECK(entropy_source() == ENTROPY_NONE, "NONE");
    // A transition gives a new budget
    readback_failing = 0;
    CHECK(radio_start() == 0, "start");
    CHECK(radio_stop() == 0, "stop");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC after a transition with a good readback");
    CHECK(violations == 0, "no violations");
}

static void start_from_adc(void)
{
    radio_entropy_init();
    reset_trace();
    uint32_t epoch = entropy_epoch();
    CHECK(radio_start() == 0, "start");
    CHECK(trace_is("DS"), "disable, then start");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO");
    CHECK(entropy_epoch() == epoch, "the epoch counts stops, not starts");
    CHECK(radio_start() == 0, "start again");
    CHECK(trace_is("DS"), "a second start does nothing");
    CHECK(violations == 0, "no violations");
}

static void stop_enables(void)
{
    CHECK(radio_start() == 0, "start");
    reset_trace();
    uint32_t epoch = entropy_epoch();
    CHECK(radio_stop() == 0, "stop");
    CHECK(trace_is("PE"), "stop, then enable");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC");
    CHECK(entropy_epoch() == epoch + 1, "epoch moved");
    CHECK(radio_stop() == 0, "stop again");
    CHECK(trace_is("PE"), "a second stop does nothing");
    CHECK(entropy_epoch() == epoch + 1, "and does not move the epoch");
    CHECK(violations == 0, "no violations");
}

static void stop_readback_fails(void)
{
    CHECK(radio_start() == 0, "start");
    readback_failing = 1;
    CHECK(radio_stop() == 0, "stop");
    CHECK(entropy_source() == ENTROPY_NONE, "NONE: the readback caught it");
    radio_entropy_poll();
    CHECK(entropy_source() == ENTROPY_ADC, "ADC after the retry");
    // and the source then goes away with the next start
    CHECK(radio_start() == 0, "start");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO");
    CHECK(violations == 0, "no violations");
}

static void stop_fails(void)
{
    CHECK(radio_start() == 0, "start");
    reset_trace();
    wifi_stop_fails = 1;
    CHECK(radio_stop() == 0x101, "stop passes the error on");
    CHECK(trace_is("P"), "no enable after a failed stop");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO: the radio is still on");
    CHECK(wifi_running, "and it is");
    CHECK(radio_stop() == 0, "stop works the next time");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC");
    CHECK(violations == 0, "no violations");
}

static void start_fails(void)
{
    radio_entropy_init();
    reset_trace();
    wifi_start_fails = 1;
    CHECK(radio_start() == 0x101, "start passes the error on");
    CHECK(trace_is("DSE"), "the source is put back after a failed start");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC");
    CHECK(!wifi_running, "radio off");
    CHECK(radio_start() == 0, "start works the next time");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO");
    CHECK(violations == 0, "no violations");
}

static void adc_read_with_source_on(void)
{
    radio_entropy_init();
    reset_trace();
    CHECK(radio_entropy_adc_begin(RE_WAIT_ADC_MS) == 0, "begin");
    CHECK(trace_is("D"), "the source off");
    CHECK(entropy_source() == ENTROPY_NONE, "NONE while the ADC is lent out");
    CHECK(radio_entropy_acquire_shared(0) == RE_ERR_RECURSION, "the lender holds the gate");
    radio_entropy_adc_end();
    CHECK(trace_is("DE"), "the source on again");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC, read back");
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.adc_hold_max_ms < 10, "held for less than 10 ms");
    CHECK(radio_entropy_acquire_shared(0) == 0, "and the gate is free");
    radio_entropy_release_shared();
    CHECK(violations == 0, "no violations");
}

static void adc_read_readback_fails(void)
{
    radio_entropy_init();
    CHECK(radio_entropy_adc_begin(RE_WAIT_ADC_MS) == 0, "begin");
    readback_failing = 1;
    radio_entropy_adc_end();
    CHECK(entropy_source() == ENTROPY_NONE, "NONE: the readback caught it");
    radio_entropy_poll();
    CHECK(entropy_source() == ENTROPY_ADC, "ADC after the retry");
    CHECK(violations == 0, "no violations");
}

static void adc_read_with_radio_on(void)
{
    CHECK(radio_start() == 0, "start");
    reset_trace();
    CHECK(radio_entropy_adc_begin(RE_WAIT_ADC_MS) == 0, "begin");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO throughout");
    radio_entropy_adc_end();
    CHECK(trace_is(""), "the radio running, nothing is touched");
    CHECK(entropy_source() == ENTROPY_RADIO, "RADIO");
    CHECK(violations == 0, "no violations");
}

static void adc_read_with_source_off(void)
{
    // A failed readback left no source; reading the voltages does not make one
    readback_failing = 100;
    radio_entropy_init();
    reset_trace();
    CHECK(radio_entropy_adc_begin(RE_WAIT_ADC_MS) == 0, "begin");
    radio_entropy_adc_end();
    CHECK(entropy_source() == ENTROPY_NONE, "NONE");
    CHECK(violations == 0, "no violations");
}

//----------------------------------
// The gate

struct holder {
    volatile int state; // 0 not started, 1 holding, 2 done
    volatile int release;
    int rc;
    uint32_t wait_ms;
    int kind; // 0 shared hold, 1 radio_stop, 2 adc_begin/end, 3 start
};

static void *holder_thread(void *arg)
{
    struct holder *h = arg;
    if (h->kind == 0) {
        h->rc = radio_entropy_acquire_shared(h->wait_ms);
        if (h->rc == 0) {
            h->state = 1;
            while (!h->release) usleep(500);
            radio_entropy_release_shared();
        }
    } else if (h->kind == 1) {
        h->state = 1;
        h->rc = radio_stop();
    } else if (h->kind == 2) {
        h->state = 1;
        h->rc = radio_entropy_adc_begin(h->wait_ms);
        if (h->rc == 0) {
            radio_entropy_adc_end();
        }
    } else if (h->kind == 5) {
        h->rc = radio_entropy_adc_begin(h->wait_ms);
        if (h->rc == 0) {
            h->state = 1;
            while (!h->release) usleep(500);
            radio_entropy_adc_end();
        }
    } else {
        h->state = 1;
        h->rc = radio_start();
    }
    h->state = 2;
    return NULL;
}

static pthread_t spawn(struct holder *h)
{
    pthread_t t;
    h->state = 0;
    h->release = 0;
    pthread_create(&t, NULL, holder_thread, h);
    return t;
}

static void wait_state(struct holder *h, int state)
{
    for (int i = 0; (i < 4000) && (h->state < state); i++) usleep(500);
}

static void shared_holders_do_not_block_each_other(void)
{
    CHECK(radio_start() == 0, "start");
    struct holder a = { .wait_ms = 20 }, b = { .wait_ms = 20 };
    pthread_t ta = spawn(&a);
    wait_state(&a, 1);
    pthread_t tb = spawn(&b);
    wait_state(&b, 1);
    CHECK((a.state == 1) && (b.state == 1), "two holders at once");
    a.release = b.release = 1;
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK((a.rc == 0) && (b.rc == 0), "both got it");
}

static void pending_writer_blocks_new_readers(void)
{
    CHECK(radio_start() == 0, "start");
    struct holder reader = { .wait_ms = 20 };
    pthread_t tr = spawn(&reader);
    wait_state(&reader, 1);

    struct holder writer = { .kind = 1 };
    pthread_t tw = spawn(&writer);
    usleep(20000); // the stop is waiting for the reader
    CHECK(writer.state == 1, "the stop waits for the reader");
    CHECK(wifi_running, "and has not stopped the radio");

    uint32_t t0 = re_port_now_ms();
    CHECK(radio_entropy_acquire_shared(20) == RE_ERR_BUSY, "a new reader is turned away: busy");
    uint32_t waited = re_port_now_ms() - t0;
    CHECK((waited >= 15) && (waited < 200), "after about 20 ms");
    CHECK(radio_entropy_acquire_shared(0) == RE_ERR_BUSY, "at once with a wait of 0");

    reader.release = 1;
    pthread_join(tr, NULL);
    pthread_join(tw, NULL);
    CHECK(writer.rc == 0, "the stop completes");
    CHECK(!wifi_running, "radio off");
    CHECK(radio_entropy_acquire_shared(20) == 0, "readers are served again");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC");
    radio_entropy_release_shared();
    CHECK(violations == 0, "no violations");
}

static void writer_that_gives_up_leaves_nothing_behind(void)
{
    radio_entropy_init();
    struct holder reader = { .wait_ms = 20 };
    pthread_t tr = spawn(&reader);
    wait_state(&reader, 1);
    uint32_t t0 = re_port_now_ms();
    CHECK(radio_entropy_adc_begin(30) == RE_ERR_BUSY, "the voltage read gives up");
    uint32_t waited = re_port_now_ms() - t0;
    CHECK((waited >= 25) && (waited < 200), "after about 30 ms");
    reader.release = 1;
    pthread_join(tr, NULL);
    CHECK(radio_entropy_acquire_shared(0) == 0, "no pending writer left behind");
    radio_entropy_release_shared();
    CHECK(entropy_source() == ENTROPY_ADC, "the source was not touched");
    CHECK(violations == 0, "no violations");
}

static void recursion_is_refused(void)
{
    CHECK(radio_start() == 0, "start");
    CHECK(radio_entropy_acquire_shared(20) == 0, "shared");
    // With a writer pending, a waiting second acquire would never return
    struct holder writer = { .kind = 1 };
    pthread_t tw = spawn(&writer);
    usleep(20000);
    uint32_t t0 = re_port_now_ms();
    CHECK(radio_entropy_acquire_shared(5000) == RE_ERR_RECURSION, "shared again, writer pending: refused");
    CHECK(re_port_now_ms() - t0 < 100, "at once, not after the wait");
    CHECK(radio_stop() == RE_ERR_RECURSION, "a reader cannot become a writer");
    CHECK(radio_start() == RE_ERR_RECURSION, "nor start the radio");
    CHECK(radio_entropy_adc_begin(5000) == RE_ERR_RECURSION, "nor lend out the ADC");
    radio_entropy_release_shared();
    pthread_join(tw, NULL);
    CHECK(writer.rc == 0, "the stop completes");
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.recursion >= 4, "counted");
    CHECK(violations == 0, "no violations");
}

// A stub of a busy reader: shared holds of 2 ms, one after another, as fast as it can
static volatile int stop_engine;
static volatile int engine_steps, engine_busy;

static void *engine(void *arg)
{
    while (!stop_engine) {
        if (radio_entropy_acquire_shared(RE_WAIT_RANDOM_MS) == 0) {
            usleep(2000);
            radio_entropy_release_shared();
            engine_steps++;
        } else {
            engine_busy++;
        }
    }
    return NULL;
}

static void back_to_back_readers_do_not_starve_a_transition(void)
{
    CHECK(radio_start() == 0, "start");
    pthread_t te;
    pthread_create(&te, NULL, engine, NULL);
    while (engine_steps < 5) usleep(1000);
    uint32_t t0 = re_port_now_ms();
    CHECK(radio_stop() == 0, "stop");
    uint32_t took = re_port_now_ms() - t0;
    CHECK(took < 300, "completes within a few steps, not starved");
    CHECK(!wifi_running, "radio off");
    stop_engine = 1;
    pthread_join(te, NULL);
    CHECK(violations == 0, "no violations");
}

static void gate_is_free_after_a_failed_transition(void)
{
    CHECK(radio_start() == 0, "start");
    wifi_stop_fails = 1;
    CHECK(radio_stop() != 0, "stop fails");
    CHECK(radio_entropy_acquire_shared(0) == 0, "the gate is free");
    radio_entropy_release_shared();
    CHECK(radio_entropy_adc_begin(20) == 0, "and for the writer");
    radio_entropy_adc_end();
}

//----------------------------------
// The answer to GET_RANDOM

static volatile int fills;
static volatile int fill_gate_ok;

// esp_fill_random(): called with the shared hold, and with nothing else held
static void fake_fill(void *buf, size_t len)
{
    fills++;
    if ((radio_entropy_acquire_shared(0) == RE_ERR_RECURSION) && !radio_entropy_held_exclusive()) {
        fill_gate_ok++;
    }
    memset(buf, 0xA5, len);
}

static union {
    uint32_t align;
    uint8_t bytes[12 + 512 + 16];
} msg;

// Asks for len bytes; returns the size of the answer
static int ask(uint16_t len, int size_in)
{
    memset(msg.bytes, 0xEE, sizeof(msg.bytes));
    rpc_get_random_req *req = (rpc_get_random_req *)msg.bytes;
    req->hdr.command = CMD_GET_RANDOM;
    req->length = len;
    return radio_entropy_serve_get_random(msg.bytes, size_in, fake_fill);
}

static rpc_get_random_resp *reply(void)
{
    return (rpc_get_random_resp *)msg.bytes;
}

static bool gate_is_free(void)
{
    if (radio_entropy_acquire_shared(0) != 0) return false;
    radio_entropy_release_shared();
    if (radio_entropy_adc_begin(0) != 0) return false;
    radio_entropy_adc_end();
    return true;
}

static void get_random_answers(void)
{
    int n;
    // No source yet
    n = ask(16, 6);
    CHECK((n == 12) && (reply()->esp_err == RE_ESP_ERR_INVALID_STATE) && (reply()->length == 0), "NONE: refused, no bytes");
    CHECK(reply()->source == ENTROPY_NONE, "NONE: source says none");
    CHECK(fills == 0, "nothing drawn");
    CHECK(gate_is_free(), "the gate is free after a refusal (no hold leaks)");

    radio_entropy_init();
    n = ask(16, 6);
    CHECK((n == 12 + 16) && (reply()->esp_err == 0) && (reply()->length == 16), "ADC: 16 bytes");
    CHECK((reply()->source == ENTROPY_ADC) && (reply()->data[0] == 0xA5) && (reply()->data[15] == 0xA5), "ADC: from the draw");
    CHECK(msg.bytes[12 + 16] == 0xEE, "no byte past the length is written");
    CHECK((fills == 1) && (fill_gate_ok == 1), "drawn once, with the shared hold only");
    CHECK(gate_is_free(), "the gate is free when the answer goes out");

    n = ask(512, 6);
    CHECK((n == 12 + 512) && (reply()->esp_err == 0), "512 bytes");
    n = ask(0, 6);
    CHECK((n == 12) && (reply()->esp_err == RE_ESP_ERR_INVALID_ARG), "0 bytes: invalid");
    n = ask(513, 6);
    CHECK((n == 12) && (reply()->esp_err == RE_ESP_ERR_INVALID_ARG) && (reply()->length == 0), "513 bytes: invalid");
    n = ask(16, 5);
    CHECK((n == 12) && (reply()->esp_err == RE_ESP_ERR_INVALID_ARG), "a request too short to hold the length");
    CHECK(fills == 2, "refusals draw nothing");
    CHECK(gate_is_free(), "the gate is free after each");
    CHECK(msg.bytes[12] == 0xEE, "a refusal sends no data");

    CHECK(radio_start() == 0, "start");
    n = ask(8, 6);
    CHECK((n == 12 + 8) && (reply()->source == ENTROPY_RADIO), "RADIO: served");
    CHECK(radio_stop() == 0, "stop");
    n = ask(8, 6);
    CHECK((n == 12 + 8) && (reply()->source == ENTROPY_ADC), "ADC again after the stop");
    CHECK(violations == 0, "no violations");
}

void serve_from_thread(int *size, uint32_t *ms);

static void get_random_busy(void)
{
    radio_entropy_init();
    // A voltage read holds the source
    CHECK(radio_entropy_adc_begin(20) == 0, "the ADC is lent out");
    // This task holds the gate exclusively, so serve from another
    int size = 0;
    uint32_t ms = 0;
    serve_from_thread(&size, &ms);
    CHECK(size == 12, "busy: no data");
    CHECK(reply()->esp_err == RPC_ERR_ENTROPY_BUSY, "busy: the busy error");
    CHECK((reply()->source == ENTROPY_NONE) && (reply()->length == 0), "busy: no source, no bytes");
    CHECK((ms >= 15) && (ms < 200), "after about 20 ms");
    CHECK(fills == 0, "nothing drawn");
    radio_entropy_adc_end();
    CHECK(gate_is_free(), "free again");
    CHECK(ask(4, 6) == 12 + 4, "served once the ADC is back");
    CHECK(violations == 0, "no violations");
}

static void *serve_thread(void *arg)
{
    int *r = arg;
    uint32_t t0 = re_port_now_ms();
    r[0] = ask(16, 6);
    r[1] = (int)(re_port_now_ms() - t0);
    return NULL;
}

void serve_from_thread(int *size, uint32_t *ms)
{
    int r[2];
    pthread_t t;
    pthread_create(&t, NULL, serve_thread, r);
    pthread_join(t, NULL);
    *size = r[0];
    *ms = (uint32_t)r[1];
}

//----------------------------------
// The voltage read

static volatile int unit_reads;
static volatile int unit_fails;
static volatile uint16_t unit_base;

static int fake_unit(uint16_t *out)
{
    unit_reads++;
    if (!radio_entropy_held_exclusive()) violate("voltages read without the exclusive hold");
    if (bus_clk_off) violate("voltages read with the SAR ADC bus clock off");
    if (hw_on) violate("voltages read with the ADC source on");
    if (unit_fails > 0) {
        unit_fails--;
        return 0x101;
    }
    for (int i = 0; i < 7; i++) out[i] = unit_base + i;
    return 0;
}

// Radio off (the source on), then on: the source's enable left the ADC's bus clock off,
// and a voltage read with the radio on must not read with it off
static void adc_read_after_the_radio_came_on(void)
{
    uint16_t out[7], cache[7];
    bool valid = false;
    radio_entropy_init();
    CHECK(entropy_source() == ENTROPY_ADC, "radio off: the ADC source");
    CHECK(radio_start() == 0, "start");
    CHECK(radio_entropy_read_adc(out, cache, &valid, 7, fake_unit) == 0, "read with the radio on");
    CHECK(violations == 0, "the clock was on for the read");
}

static void adc_read_cache(void)
{
    uint16_t cache[7] = { 0 };
    bool valid = false;
    uint16_t out[7];
    radio_entropy_init();

    unit_base = 1000;
    memset(out, 0xEE, sizeof(out));
    CHECK(radio_entropy_read_adc(out, cache, &valid, 7, fake_unit) == 0, "first read");
    CHECK(valid && (out[0] == 1000) && (out[6] == 1006) && (cache[3] == 1003), "values and cache");
    CHECK(entropy_source() == ENTROPY_ADC, "the source is back");
    CHECK(gate_is_free(), "gate free");

    // The ADC is busy: a reader holds the gate
    struct holder reader = { .wait_ms = 20 };
    pthread_t tr = spawn(&reader);
    wait_state(&reader, 1);
    unit_base = 2000;
    int before = unit_reads;
    memset(out, 0xEE, sizeof(out));
    uint32_t t0 = re_port_now_ms();
    CHECK(radio_entropy_read_adc(out, cache, &valid, 7, fake_unit) == 0, "busy with a cache: succeeds");
    uint32_t took = re_port_now_ms() - t0;
    CHECK((out[0] == 1000) && (out[6] == 1006), "with the last values, not zeros");
    CHECK((took >= 40) && (took < 300), "after about 50 ms");
    CHECK(unit_reads == before, "the unit was not read");
    reader.release = 1;
    pthread_join(tr, NULL);

    // A failing read gives the cache and the error
    unit_fails = 1;
    memset(out, 0xEE, sizeof(out));
    CHECK(radio_entropy_read_adc(out, cache, &valid, 7, fake_unit) == 0x101, "a failed read is reported");
    CHECK((out[0] == 1000) && (cache[0] == 1000), "and does not touch the cache");
    CHECK(entropy_source() == ENTROPY_ADC, "the source is back after a failed read");
    CHECK(gate_is_free(), "gate free");
    CHECK(violations == 0, "no violations");
}

static struct holder *release_target;

static void *release_later(void *arg)
{
    usleep(100000);
    release_target->release = 1;
    return NULL;
}

static void adc_read_without_a_cache(void)
{
    uint16_t cache[7] = { 0 };
    bool valid = false;
    uint16_t out[7];
    radio_entropy_init();

    // The boot read fails: no values, so an error, never zeros
    unit_fails = 1;
    memset(out, 0xEE, sizeof(out));
    CHECK(radio_entropy_read_adc(out, cache, &valid, 7, fake_unit) == 0x101, "the failed first read is an error");
    CHECK(!valid, "and there is no cache");
    CHECK(out[0] == 0xEEEE, "out is left alone");

    // And the next read, when it cannot get the ADC, is an error too
    struct holder reader = { .wait_ms = 20 };
    pthread_t tr = spawn(&reader);
    wait_state(&reader, 1);
    memset(out, 0xEE, sizeof(out));
    int rc = radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
    CHECK(rc == RE_ERR_BUSY, "no cache and no ADC: an error, after the long wait");
    CHECK(out[0] == 0xEEEE, "out is left alone, not zeros");
    reader.release = 1;
    pthread_join(tr, NULL);

    // The long wait lets a short hold through
    struct holder blip = { .wait_ms = 20 };
    pthread_t tb = spawn(&blip);
    wait_state(&blip, 1);
    unit_base = 3000;
    pthread_t rel;
    release_target = &blip;
    pthread_create(&rel, NULL, release_later, NULL);
    rc = radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
    CHECK((rc == 0) && valid && (out[0] == 3000), "no cache: waits for the ADC rather than give up at once");
    pthread_join(rel, NULL);
    pthread_join(tb, NULL);
    CHECK(violations == 0, "no violations");
}

static void retry_budget_survives_voltage_reads(void)
{
    uint16_t cache[7] = { 0 };
    bool valid = false;
    uint16_t out[7];
    readback_failing = 100;
    radio_entropy_init();
    for (int i = 0; i < 6; i++) radio_entropy_poll();
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.retries == RE_RETRIES, "the budget is spent");
    int lines = log_lines;
    for (int i = 0; i < 5; i++) {
        radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
        radio_entropy_poll();
    }
    radio_entropy_get_stats(&st);
    CHECK(st.retries == RE_RETRIES, "voltage reads do not give new retries");
    CHECK(log_lines == lines, "and the failing readbacks are not logged again");
    CHECK(entropy_source() == ENTROPY_NONE, "NONE");
}

static void retries_per_transition(void)
{
    // The budget belongs to the transition: poll and voltage reads draw on it, nothing renews it
    uint16_t cache[7] = { 0 };
    bool valid = false;
    uint16_t out[7];
    unit_base = 500;
    radio_entropy_init();
    CHECK(entropy_source() == ENTROPY_ADC, "healthy");
    // A healthy source fails once on a voltage read: that is the first failure, retries follow
    readback_failing = 100;
    radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
    CHECK(entropy_source() == ENTROPY_NONE, "NONE after the failed re-enable");
    for (int i = 0; i < 10; i++) {
        radio_entropy_poll();
        radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
    }
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.retries == RE_RETRIES, "no more than RE_RETRIES retries, polls and reads together");
    reset_trace();
    for (int i = 0; i < 5; i++) {
        radio_entropy_read_adc(out, cache, &valid, 7, fake_unit);
    }
    CHECK(trace_is(""), "once spent, a voltage read does not touch the source at all");
    CHECK(entropy_source() == ENTROPY_NONE, "NONE until the next transition");
    CHECK(valid && (out[0] == 500), "and the voltages still read");
    // The next transition gives a new budget
    readback_failing = 0;
    CHECK(radio_start() == 0, "start");
    CHECK(radio_stop() == 0, "stop");
    CHECK(entropy_source() == ENTROPY_ADC, "ADC after the next transition");
    CHECK(violations == 0, "no violations");
}

static void spent_poll_does_not_wait_for_the_gate(void)
{
    readback_failing = 100;
    radio_entropy_init();
    for (int i = 0; i < 4; i++) radio_entropy_poll();
    struct holder reader = { .wait_ms = 20 };
    pthread_t tr = spawn(&reader);
    wait_state(&reader, 1);
    uint32_t t0 = re_port_now_ms();
    radio_entropy_poll();
    CHECK(re_port_now_ms() - t0 < 5, "a poll with nothing to do returns at once, with the gate busy");
    reader.release = 1;
    pthread_join(tr, NULL);
    radio_entropy_stats_t st;
    radio_entropy_get_stats(&st);
    CHECK(st.busy == 0, "and did not queue for the gate");
}

static void adc_end_by_a_bystander_changes_nothing(void)
{
    radio_entropy_init();
    struct holder lender = { .kind = 5, .wait_ms = 50 };
    pthread_t tl = spawn(&lender);
    wait_state(&lender, 1);
    CHECK(entropy_source() == ENTROPY_NONE, "lent out");
    int lines = log_lines;
    radio_entropy_adc_end();
    CHECK(log_lines == lines + 1, "an end without a begin is logged");
    CHECK(radio_entropy_acquire_shared(0) == RE_ERR_BUSY, "and the lender still holds the gate");
    CHECK(entropy_source() == ENTROPY_NONE, "and the source is untouched");
    lender.release = 1;
    pthread_join(tl, NULL);
    CHECK(entropy_source() == ENTROPY_ADC, "the lender's end puts the source back");
    CHECK(violations == 0, "no violations");
}

static void logs_are_written_outside_the_gate(void)
{
    readback_failing = 1;
    radio_entropy_init();
    CHECK(log_lines == 1, "the mismatch was logged");
    CHECK(violations == 0, "and not with the gate held");
}

//----------------------------------
// A long random walk over everything, with failures, against the model's rules

static unsigned rng_state = 12345;
static unsigned rnd(unsigned n)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 8) % n;
}

static void random_walk(void)
{
    int adc_without_source = 0;
    for (int i = 0; i < 20000; i++) {
        switch (rnd(8)) {
            case 0: radio_start(); break;
            case 1: radio_stop(); break;
            case 2: radio_entropy_init(); break;
            case 3: radio_entropy_poll(); break;
            case 4:
                if (radio_entropy_adc_begin(5) == 0) {
                    radio_entropy_adc_end();
                }
                break;
            case 5: wifi_start_fails = rnd(3) == 0; break;
            case 6: wifi_stop_fails = rnd(3) == 0; break;
            case 7: readback_failing = rnd(3) == 0 ? 1 + rnd(3) : 0; break;
        }
        // The source is only ADC when the hardware really is on and was read back; RADIO only
        // while the radio runs
        entropy_source_t s = entropy_source();
        if ((s == ENTROPY_ADC) && !(hw_on && !wifi_running)) adc_without_source++;
        if ((s == ENTROPY_RADIO) && !wifi_running) adc_without_source++;
        if (s == ENTROPY_NONE && wifi_running) { /* a start that failed to report cannot happen */ adc_without_source++; }
    }
    CHECK(adc_without_source == 0, "the source always matches the chip");
    CHECK(violations == 0, "no violations in 20000 steps");
}

//----------------------------------

struct testcase {
    const char *name;
    void (*fn)(void);
};

#define CASE(f) { #f, f }

static const struct testcase cases[] = {
    CASE(boot_without_radio),
    CASE(boot_with_radio),
    CASE(boot_readback_fails_then_retries),
    CASE(retries_are_bounded),
    CASE(start_from_adc),
    CASE(stop_enables),
    CASE(stop_readback_fails),
    CASE(stop_fails),
    CASE(start_fails),
    CASE(adc_read_with_source_on),
    CASE(adc_read_readback_fails),
    CASE(adc_read_with_radio_on),
    CASE(adc_read_with_source_off),
    CASE(shared_holders_do_not_block_each_other),
    CASE(pending_writer_blocks_new_readers),
    CASE(writer_that_gives_up_leaves_nothing_behind),
    CASE(recursion_is_refused),
    CASE(back_to_back_readers_do_not_starve_a_transition),
    CASE(gate_is_free_after_a_failed_transition),
    CASE(get_random_answers),
    CASE(get_random_busy),
    CASE(adc_read_after_the_radio_came_on),
    CASE(adc_read_cache),
    CASE(adc_read_without_a_cache),
    CASE(retry_budget_survives_voltage_reads),
    CASE(logs_are_written_outside_the_gate),
    CASE(retries_per_transition),
    CASE(spent_poll_does_not_wait_for_the_gate),
    CASE(adc_end_by_a_bystander_changes_nothing),
    CASE(random_walk),
};

int main(void)
{
    int bad = 0;
    int n = sizeof(cases) / sizeof(cases[0]);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 0; i < n; i++) {
        printf("%s\n", cases[i].name);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            alarm(30); // a hang is a failure
            cases[i].fn();
            fflush(stdout);
            _exit(failures ? 1 : 0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("  FAILED (status 0x%x)\n", status);
            bad++;
        } else {
            printf("  ok\n");
        }
    }
    printf("\n%d cases, %d failed\n", n, bad);
    return bad ? 1 : 0;
}
