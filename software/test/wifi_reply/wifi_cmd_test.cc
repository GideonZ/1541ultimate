/*
 * Host test of wifi_cmd.cc with stubs for the UART and the RTOS: what a request
 * answers when the UART has no free TX buffer. The RPCs answer 0 for success, so
 * they must answer -1; wifi_detect answers a success flag, so pdFALSE. And how a
 * request ends whose reply comes in time, as the wait times out, late, or never.
 * And wifi_call, the request that can be cancelled: how each wait ends, with a fake
 * clock, and what it leaves behind for the next request.
 */
#include <stdio.h>
#include <string.h>
#include "wifi_cmd.h"
#include "wifi_random.h"

Esp32 :: Esp32() : uart(NULL) {}
Esp32 esp32;
static DmaUART uart;
static command_buf_t txbuf, rxbuf;
static int failures;
static int queued;                  // frames the ISR passed to the receive queue
static command_buf_t *notify_value; // the notification value of the waiting task
static bool notify_pending;
static TickType_t first_wait;       // the ticks of the first wait of a request
static int waits;
enum reply_time { NEVER, ON_TIME, AT_TIMEOUT, AFTER_RELEASE };
static reply_time reply_at;
static bool timed_out;
static bool module_ready;
static TickType_t buffer_wait;      // the ticks the last GetBuffer was given
static void (*on_get_buffer)(void); // runs as GetBuffer hands the buffer out
static int suspended;               // depth of vTaskSuspendAll; every function must return it to 0
static int critical;                // depth of the critical section; the same
// The fake clock: it moves only when a wait ends without a notification, or by wait_elapse
static TickType_t now = 1000;
static TickType_t wait_elapse;      // the ticks a wait that ends with a notification took
static int blocking_waits;          // the waits with a timeout, and the ticks each was given
static TickType_t wait_ticks[8];
static void (*on_wait)(int n);      // runs at the start of the n-th wait with a timeout
static void (*after_wait)(void);    // runs when a wait that took a notification is about to return
// What the module answers: a GET_RANDOM answer, or one of the power mode (the default)
static bool random_answer;
static int random_err;
static uint8_t random_source = 1;
static uint16_t random_length;
static int random_command = -1;    // when not -1, the command the answer carries instead

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

BaseType_t DmaUART::GetBuffer(command_buf_t **b, TickType_t t)
{
    buffer_wait = t;
    if (!buffers_free) {
        return pdFALSE;
    }
    buffers_free--;
    *b = &txbuf;
    if (on_get_buffer) {
        on_get_buffer();
    }
    return pdTRUE;
}

void DmaUART::FreeBuffer(command_buf_t *b)
{
    buffers_free++;
}

extern "C" BaseType_t cmd_buffer_received_isr(command_buf_context_t *c, command_buf_t *b, BaseType_t *w) { queued++; return pdFALSE; }

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

// The module answers the request with this header
static void answer(rpc_header_t hdr)
{
    if (random_answer) {
        rpc_get_random_resp *rnd = (rpc_get_random_resp *)rxbuf.data;
        rnd->hdr = hdr;
        if (random_command >= 0) {
            rnd->hdr.command = (uint8_t)random_command;
        }
        rnd->esp_err = random_err;
        rnd->source = random_source;
        rnd->length = random_length;
        for (int i = 0; i < random_length; i++) {
            rnd->data[i] = (uint8_t)(0xA0 + i);
        }
        rxbuf.size = offsetof(rpc_get_random_resp, data) + random_length;
        BaseType_t w;
        wifi_rx_isr(NULL, &rxbuf, &w);
        return;
    }
    rpc_get_power_mode_resp *resp = (rpc_get_power_mode_resp *)rxbuf.data;
    resp->hdr = hdr;
    resp->esp_err = 0;
    resp->mode = 3;
    resp->last_state = 1;
    BaseType_t w;
    wifi_rx_isr(NULL, &rxbuf, &w);
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, int a, BaseType_t *w)
{
    notify_value = (command_buf_t *)v;
    notify_pending = true;
    return pdTRUE;
}

// As in FreeRTOS: without overwrite, a notification that is pending stays. The owner check and the
// notification are one step: only with the scheduler suspended can the waiter not leave in between.
BaseType_t xTaskNotify(TaskHandle_t t, uint32_t v, int a)
{
    CHECK(suspended > 0);
    if (notify_pending && (a == eSetValueWithoutOverwrite)) {
        return pdFAIL;
    }
    notify_value = (command_buf_t *)v;
    notify_pending = true;
    return pdTRUE;
}

TickType_t xTaskGetTickCount(void)
{
    return now;
}

void vTaskSuspendAll(void)
{
    suspended++;
}

BaseType_t xTaskResumeAll(void)
{
    suspended--;
    return pdFALSE;
}

// The module answers the request just sent: during the wait (ON_TIME), after the
// wait timed out but before the slot is released (AT_TIMEOUT), so only a drain
// after the release can take it, or during that drain (AFTER_RELEASE). As in
// FreeRTOS, the value is written whether or not a notification was pending.
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t t)
{
    if (!waits++) {
        first_wait = t;
    }
    if (t) {
        if (blocking_waits < 8) {
            wait_ticks[blocking_waits] = t;
        }
        if (on_wait) {
            on_wait(blocking_waits);
        }
        blocking_waits++;
    }
    if ((t && reply_at == ON_TIME) || (!t && reply_at == AFTER_RELEASE)) {
        reply_at = NEVER;
        answer(*(rpc_header_t *)txbuf.data);
    }
    *v = (uint32_t)notify_value;
    if (t && reply_at == AT_TIMEOUT) {
        reply_at = NEVER;
        timed_out = true;
        return pdFALSE;
    }
    if (!notify_pending) {
        now += t;
        return pdFALSE;
    }
    notify_pending = false;
    now += wait_elapse;
    wait_elapse = 0;
    if (after_wait) {
        after_wait();
    }
    return pdTRUE;
}

bool wifi_module_ready(void)
{
    return module_ready;
}

static void (*safe_hook)(void);     // runs once, at the next critical section

extern "C" void leave_safe_section(void)
{
    CHECK(critical > 0);
    critical--;
}

extern "C" void enter_safe_section(void)
{
    critical++;
    if (safe_hook) {
        void (*hook)(void) = safe_hook;
        safe_hook = NULL;
        hook();
    }
    if (timed_out) {
        timed_out = false;
        answer(*(rpc_header_t *)txbuf.data);
    }
}

// Claims every reply slot and answers them all, so they are free again
static bool all_slots_free(void)
{
    rpc_header_t hdr[WIFI_REPLY_SLOTS];
    int n = 0;
    while (n < WIFI_REPLY_SLOTS && wifi_reply_register(&hdr[n])) {
        n++;
    }
    for (int i = 0; i < n; i++) {
        answer(hdr[i]);
    }
    notify_pending = false;
    return n == WIFI_REPLY_SLOTS;
}


static void test_no_tx_buffer(void)
{
    uint8_t mode, last;
    uart.buffers_free = 0;
    uart.transmitted = 0;
    CHECK(wifi_get_power_mode(&mode, &last) == -1);
    CHECK(wifi_set_power_mode(1) == -1);
    CHECK(wifi_set_wake_on_wifi(1) == -1);
    CHECK(uart.transmitted == 0);

    uint16_t major, minor;
    char str[32];
    CHECK(wifi_detect(&major, &minor, str, sizeof(str)) == pdFALSE);
    CHECK(uart.transmitted == 0);
}

// A starved buffer must not use up reply slots either
static void test_no_tx_buffer_keeps_slots(void)
{
    uart.buffers_free = 0;
    for (int i = 0; i < WIFI_REPLY_SLOTS + 1; i++) {
        uint8_t mode, last;
        CHECK(wifi_get_power_mode(&mode, &last) == -1);
    }
    CHECK(all_slots_free());
}

// With a buffer, a request that gets no reply is sent, is -1 too and releases its slot
static void test_no_reply(void)
{
    uint8_t mode, last;
    for (int i = 0; i < 2 * WIFI_REPLY_SLOTS; i++) {
        uart.buffers_free = 1;
        uart.transmitted = 0;
        waits = 0;
        CHECK(wifi_get_power_mode(&mode, &last) == -1);
        CHECK(uart.transmitted == 1);
        CHECK(first_wait == pdMS_TO_TICKS(WIFI_REPLY_TIMEOUT_MS));
    }
    CHECK(all_slots_free());
}

// The reply arrives in time or as the wait times out: the caller gets it, not -1,
// and frees it
static void test_reply(reply_time when)
{
    uint8_t mode = 0, last = 0;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    waits = 0;
    reply_at = when;
    CHECK(wifi_get_power_mode(&mode, &last) == 0);
    CHECK(mode == 3 && last == 1);
    CHECK(uart.transmitted == 1);
    CHECK(first_wait == pdMS_TO_TICKS(WIFI_REPLY_TIMEOUT_MS));
    CHECK(uart.buffers_free == 1);  // the reply buffer (the stub keeps the TX buffer)
    CHECK(queued == 0);
    CHECK(!notify_pending);
    CHECK(all_slots_free());
}

// The reply arrives after the slot is released: it is not for the caller any more
static void test_reply_after_release(void)
{
    uint8_t mode, last;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    reply_at = AFTER_RELEASE;
    CHECK(wifi_get_power_mode(&mode, &last) == -1);
    CHECK(uart.transmitted == 1);
    CHECK(queued == 1);
    CHECK(!notify_pending);
    CHECK(all_slots_free());
}

// wifi_get_random: the request, and each way it ends
static void random_setup(uint16_t len)
{
    random_answer = true;
    random_err = 0;
    random_source = 1;
    random_length = len;
    module_ready = true;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    waits = 0;
    buffer_wait = 0;
    reply_at = NEVER;
    memset(txbuf.data, 0, 16);
}

static void test_random(void)
{
    uint8_t out[16];

    // No module application: nothing is sent and nothing waits
    random_setup(8);
    module_ready = false;
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_NO_MODULE);
    CHECK(uart.transmitted == 0 && waits == 0);

    // No TX buffer within 20 ms: busy, and no slot is used up
    for (int i = 0; i < WIFI_REPLY_SLOTS + 1; i++) {
        random_setup(8);
        uart.buffers_free = 0;
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(buffer_wait == 4 && uart.transmitted == 0);
    }
    CHECK(all_slots_free());
    random_answer = false; // all_slots_free answers in the module's other format

    // No answer: the request went out, was waited for 200 ms, and its slot is free again
    for (int i = 0; i < 2 * WIFI_REPLY_SLOTS; i++) {
        random_setup(8);
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(uart.transmitted == 1);
        CHECK(first_wait == pdMS_TO_TICKS(200));
        CHECK(out[0] == 0);
    }
    CHECK(all_slots_free());

    // An answer in time, or as the wait times out: the bytes, once, and the buffer freed
    reply_time when[2] = { ON_TIME, AT_TIMEOUT };
    for (int i = 0; i < 2; i++) {
        random_setup(8);
        reply_at = when[i];
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == 0);
        CHECK(out[0] == 0xA0 && out[7] == 0xA7 && out[8] == 0);
        rpc_get_random_req *req = (rpc_get_random_req *)txbuf.data;
        CHECK(req->hdr.command == CMD_GET_RANDOM && req->length == 8);
        CHECK(uart.transmitted == 1 && uart.buffers_free == 1 && queued == 0 && !notify_pending);
        random_answer = false;
        CHECK(all_slots_free());
    }

    // The module refuses, is busy, answers short or without a source: no byte is handed out
    int errs[4] = { 0x105, RPC_ERR_ENTROPY_BUSY, 0, 0 };
    int want[4] = { WIFI_RANDOM_REFUSED, WIFI_RANDOM_BUSY, WIFI_RANDOM_INVALID, WIFI_RANDOM_REFUSED };
    for (int i = 0; i < 4; i++) {
        random_setup(8);
        reply_at = ON_TIME;
        random_err = errs[i];
        random_length = (i == 2) ? 4 : 8; // asked for 8
        random_source = (i == 3) ? ENTROPY_NONE : 1;
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == want[i]);
        CHECK(out[0] == 0);
        CHECK(uart.buffers_free == 1);
    }

    // An answer to another command, with this request's sequence: not ours, no byte is handed out
    random_setup(8);
    reply_at = ON_TIME;
    random_command = CMD_WIFI_GETMAC;
    memset(out, 0, sizeof(out));
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_FOREIGN);
    CHECK(out[0] == 0 && uart.buffers_free == 1);
    random_command = -1;

    // Every reply slot taken: busy, nothing is sent, and the TX buffer is given back
    {
        rpc_header_t held[WIFI_REPLY_SLOTS];
        random_setup(8);
        for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
            CHECK(wifi_reply_register(&held[i]));
        }
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(uart.transmitted == 0 && uart.buffers_free == 1 && out[0] == 0);
        random_answer = false;
        for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
            answer(held[i]);
        }
        notify_pending = false;
        CHECK(all_slots_free());
    }

    // An answer after the slot was released is not the caller's any more
    random_setup(8);
    reply_at = AFTER_RELEASE;
    memset(out, 0, sizeof(out));
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
    CHECK(out[0] == 0 && queued == 1 && !notify_pending);
    random_answer = false;
    CHECK(all_slots_free());
}

// wifi_call: a request that can be cancelled. The tests are in the task of the wait, so that "the
// canceller" is the code run from a wait, as another task is while the waiter blocks.
static bool cancel_flag;
static int cancel_polls;
static int takes;
static int mode_taken;

static TaskHandle_t me(void)
{
    return xTaskGetCurrentTaskHandle();
}

static bool cancel_pred(void *context)
{
    cancel_polls++;
    return cancel_flag;
}

static void (*fill_hook)(void);

static int fill_power(uint8_t *request, void *context)
{
    if (fill_hook) {
        fill_hook();
    }
    return sizeof(rpc_header_t);
}

static int take_power(const uint8_t *reply, int size, uint16_t sequence, void *context)
{
    takes++;
    CHECK(suspended > 0); // a reply is read and freed with no reset of the pool in between
    const rpc_get_power_mode_resp *r = (const rpc_get_power_mode_resp *)reply;
    if ((size < (int)sizeof(*r)) || (r->hdr.sequence != sequence) || (r->hdr.command != CMD_GET_POWER_MODE)) {
        return WIFI_CALL_FOREIGN;
    }
    mode_taken = r->mode;
    return 0;
}

static void call_setup(void)
{
    random_answer = false;
    module_ready = true;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    uart.transmit_fails = false;
    uart.on_transmit = NULL;
    queued = 0;
    waits = 0;
    blocking_waits = 0;
    reply_at = NEVER;
    notify_pending = false;
    wait_elapse = 0;
    on_wait = NULL;
    after_wait = NULL;
    safe_hook = NULL;
    on_get_buffer = NULL;
    fill_hook = NULL;
    cancel_flag = false;
    cancel_polls = 0;
    takes = 0;
    mode_taken = 0;
    now = 1000;
}

static wifi_call_t power_call(uint32_t timeout_ms, bool with_cancel)
{
    wifi_call_t call;
    memset(&call, 0, sizeof(call));
    call.name = "Power";
    call.command = CMD_GET_POWER_MODE;
    call.timeout_ms = timeout_ms;
    call.fill = fill_power;
    call.take = take_power;
    call.cancel = with_cancel ? cancel_pred : NULL;
    return call;
}

static void answer_now(command_buf_t *tx)
{
    answer(*(rpc_header_t *)tx->data);
}

// What every end of a call leaves: no slot, no notification, no waiter that a later wake could reach
static void check_clean(void)
{
    CHECK(suspended == 0);
    CHECK(critical == 0);
    CHECK(!notify_pending);
    wifi_wake(me());
    CHECK(!notify_pending);
    CHECK(all_slots_free());
}

static void cancel_in_wait(int n)
{
    cancel_flag = true;
    wifi_wake(me());
}

static void wake_in_wait(int n)
{
    if (n == 0) {
        wait_elapse = 100;
        wifi_wake(me());
    }
}

static void reply_then_cancel_in_wait(int n)
{
    answer(*(rpc_header_t *)txbuf.data);
    cancel_flag = true;
    wifi_wake(me()); // refused: the reply is pending
}

static void cancel_then_reply_in_wait(int n)
{
    cancel_flag = true;
    wifi_wake(me());
    answer(*(rpc_header_t *)txbuf.data); // overwrites the wake
}

static void reply_in_wait(int n)
{
    answer(*(rpc_header_t *)txbuf.data);
}

static void wake_after_reply(void)
{
    wifi_wake(me());
}

// A wake that lands as the call ends: after its last check, before it stops being the waiter
static void wake_at_leave_in_wait(int n)
{
    safe_hook = wake_after_reply;
}

static void foreign_wake_in_wait(int n)
{
    wifi_wake((TaskHandle_t)&n);
}

static int nested_result;
static void call_in_wait(int n)
{
    wifi_call_t inner = power_call(5000, true);
    uint32_t sent = uart.transmitted;
    nested_result = wifi_call(&inner);
    CHECK(uart.transmitted == sent);
}

// A cancel that was raised before the wait: the request went out, and nothing blocks
static void test_call_cancel_before_wait(void)
{
    call_setup();
    cancel_flag = true;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == WIFI_CALL_CANCELLED);
    CHECK(uart.transmitted == 1);
    CHECK(blocking_waits == 0);
    CHECK(now == 1000);
    CHECK(takes == 0);
    check_clean();
}

// A cancel during the wait ends it at once: one wait, of the whole timeout, and none of it used up
static void test_call_cancel_during_wait(void)
{
    call_setup();
    on_wait = cancel_in_wait;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == WIFI_CALL_CANCELLED);
    CHECK(uart.transmitted == 1);
    CHECK(blocking_waits == 1);
    CHECK(wait_ticks[0] == pdMS_TO_TICKS(5000));
    CHECK(now == 1000);
    CHECK(takes == 0);
    CHECK(uart.buffers_free == 0);
    check_clean();
}

// A wake that cancels nothing is no reason to stop: the rest of the time is waited
static void test_call_spurious_wake(void)
{
    call_setup();
    on_wait = wake_in_wait;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 2);
    CHECK(wait_ticks[0] == pdMS_TO_TICKS(5000));
    CHECK(wait_ticks[1] == pdMS_TO_TICKS(5000) - 100);
    CHECK(now == 1000 + pdMS_TO_TICKS(5000));
    check_clean();
}

// A wake aimed at another task, or when nothing waits, does nothing
static void test_call_wake_of_others(void)
{
    call_setup();
    wifi_wake(me());
    CHECK(!notify_pending);
    on_wait = foreign_wake_in_wait;
    wifi_call_t call = power_call(300, true);
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 1);
    CHECK(now == 1300);
    check_clean();
}

// The reply and the cancel arrive together, in either order: the reply wins, once, and its buffer is freed
static void test_call_reply_races_cancel(void)
{
    void (*orders[2])(int) = { reply_then_cancel_in_wait, cancel_then_reply_in_wait };
    for (int i = 0; i < 2; i++) {
        call_setup();
        on_wait = orders[i];
        wifi_call_t call = power_call(5000, true);
        CHECK(wifi_call(&call) == 0);
        CHECK(mode_taken == 3);
        CHECK(takes == 1);
        CHECK(uart.buffers_free == 1);
        CHECK(queued == 0);
        check_clean();
    }
    // The reply was there before the cancel was looked at: it is still the caller's
    call_setup();
    cancel_flag = true;
    uart.on_transmit = answer_now;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == 0);
    CHECK(mode_taken == 3 && takes == 1);
    CHECK(uart.buffers_free == 1);
    CHECK(blocking_waits == 0);
    check_clean();
}

// A wake that comes after the reply was taken must not be left for the next request, which would read
// it as a reply
static void test_call_no_stale_wake(void)
{
    call_setup();
    on_wait = reply_in_wait;
    after_wait = wake_after_reply;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == 0);
    CHECK(takes == 1);
    CHECK(!notify_pending);
    after_wait = NULL;
    on_wait = NULL;

    uint8_t mode = 0, last = 0;
    uart.buffers_free = 2;
    reply_at = ON_TIME;
    CHECK(wifi_get_power_mode(&mode, &last) == 0);
    CHECK(mode == 3 && last == 1);
    check_clean();

    // And after a timeout, where no reply is there to tell it from
    call_setup();
    on_wait = wake_at_leave_in_wait;
    call = power_call(100, true);
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(takes == 0);
    check_clean();
}

// Without a cancel the wait is one block of the whole timeout, as for every other request
static void test_call_no_cancel(void)
{
    call_setup();
    wifi_call_t call = power_call(5000, false);
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 1);
    CHECK(wait_ticks[0] == pdMS_TO_TICKS(5000));
    CHECK(now == 1000 + pdMS_TO_TICKS(5000));
    CHECK(uart.transmitted == 1);
    check_clean();

    call_setup();
    on_wait = reply_in_wait;
    call = power_call(5000, false);
    CHECK(wifi_call(&call) == 0);
    CHECK(mode_taken == 3 && takes == 1 && uart.buffers_free == 1);
    check_clean();

    // The reply arrives as the wait times out: still the caller's
    call_setup();
    reply_at = AT_TIMEOUT;
    call = power_call(5000, false);
    CHECK(wifi_call(&call) == 0);
    CHECK(takes == 1 && uart.buffers_free == 1);
    check_clean();
}

// "Not sent" is not "sent, no answer": a caller may send again after the first only
static void test_call_unsent(void)
{
    wifi_call_t call = power_call(5000, true);

    call_setup();
    uart.buffers_free = 0;
    CHECK(wifi_call(&call) == WIFI_CALL_UNSENT);
    CHECK(uart.transmitted == 0 && blocking_waits == 0);
    check_clean();

    call_setup();
    rpc_header_t held[WIFI_REPLY_SLOTS];
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        CHECK(wifi_reply_register(&held[i]));
    }
    CHECK(wifi_call(&call) == WIFI_CALL_UNSENT);
    CHECK(uart.transmitted == 0 && uart.buffers_free == 1 && blocking_waits == 0);
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        answer(held[i]);
    }
    notify_pending = false;
    check_clean();

    call_setup();
    uart.transmit_fails = true;
    CHECK(wifi_call(&call) == WIFI_CALL_UNSENT);
    CHECK(uart.transmitted == 0 && uart.buffers_free == 1 && blocking_waits == 0);
    check_clean();

    // Sent, and nothing came back
    call_setup();
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(uart.transmitted == 1 && blocking_waits == 1);
    check_clean();

    // A second request while one waits: nothing is sent for it
    call_setup();
    uart.buffers_free = 2;
    on_wait = call_in_wait;
    nested_result = -1;
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(nested_result == WIFI_CALL_UNSENT);
    check_clean();
}

// A deadline before the end of the timeout ends the wait there, in one block
static void test_call_deadline(void)
{
    call_setup();
    wifi_call_t call = power_call(1000, true);
    call.use_deadline = true;
    call.deadline = now + 50;
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 1);
    CHECK(wait_ticks[0] == 50);
    CHECK(now == 1050);
    check_clean();

    // Past already: not a tick of waiting; and a deadline after the timeout changes nothing
    call_setup();
    call = power_call(1000, true);
    call.use_deadline = true;
    call.deadline = now - 1;
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 0);
    check_clean();

    call_setup();
    call = power_call(1000, false);
    call.use_deadline = true;
    call.deadline = now + 5000;
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 1 && wait_ticks[0] == 1000);
    check_clean();

    // The tick counter wraps inside the wait
    call_setup();
    now = 0xFFFFFFF0u;
    call = power_call(1000, true);
    call.use_deadline = true;
    call.deadline = now + 0x20;
    CHECK(wifi_call(&call) == WIFI_CALL_BUSY);
    CHECK(blocking_waits == 1 && wait_ticks[0] == 0x20);
    check_clean();
}

// The pool is reset while the request is made: the reset gave the buffer back to the pool, so the request
// does not use it, nor frees it, and nothing is sent
static void pool_reset_now(void)
{
    uart.generation++;
    uart.buffers_free = 1;
}

static void test_call_reset_while_making_request(void)
{
    wifi_call_t call = power_call(5000, true);

    call_setup();
    on_get_buffer = pool_reset_now;
    CHECK(wifi_call(&call) == WIFI_CALL_UNSENT);
    CHECK(uart.transmitted == 0 && blocking_waits == 0);
    CHECK(uart.buffers_free == 1); // not freed a second time
    check_clean();

    call_setup();
    fill_hook = pool_reset_now;
    uart.buffers_free = 1;
    CHECK(wifi_call(&call) == WIFI_CALL_UNSENT);
    CHECK(uart.transmitted == 0 && blocking_waits == 0);
    CHECK(uart.buffers_free == 1);
    check_clean();

    // A reset before the call is not one for it
    call_setup();
    pool_reset_now();
    uart.buffers_free = 1;
    on_wait = reply_in_wait;
    CHECK(wifi_call(&call) == 0);
    check_clean();
}

// A restart of the module during the wait ends it at once, with a result of its own: no answer will come
static void restart_in_wait(int n)
{
    wifi_command_init(); // which resets the buffer pool, and counts it
}

static void test_call_module_restart(void)
{
    call_setup();
    on_wait = restart_in_wait;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == WIFI_CALL_RESTARTED);
    CHECK(blocking_waits == 1);
    CHECK(wait_ticks[0] == pdMS_TO_TICKS(5000));
    CHECK(now == 1000);
    CHECK(takes == 0);
    check_clean();

    // Also for a call without a cancel
    call_setup();
    on_wait = restart_in_wait;
    call = power_call(5000, false);
    CHECK(wifi_call(&call) == WIFI_CALL_RESTARTED);
    CHECK(blocking_waits == 1 && now == 1000);
    check_clean();

    // A restart before the call: it starts after, and is not a restart for it
    call_setup();
    wifi_command_init();
    on_wait = reply_in_wait;
    call = power_call(5000, true);
    CHECK(wifi_call(&call) == 0);
    CHECK(takes == 1);
    check_clean();
}

// A reply that was delivered before a reset of the buffer pool is not read, and not freed: the reset took it back
static void reply_then_reset_in_wait(int n)
{
    answer(*(rpc_header_t *)txbuf.data);
    uart.generation++;
}

static void test_call_reply_before_reset(void)
{
    call_setup();
    on_wait = reply_then_reset_in_wait;
    wifi_call_t call = power_call(5000, true);
    CHECK(wifi_call(&call) == WIFI_CALL_RESTARTED);
    CHECK(takes == 0);
    CHECK(uart.buffers_free == 0);
    check_clean();
}

int main(void)
{
    esp32.uart = &uart;
    test_no_tx_buffer();
    test_no_tx_buffer_keeps_slots();
    test_no_reply();
    test_reply(ON_TIME);
    test_reply(AT_TIMEOUT);
    test_reply_after_release();
    test_random();
    test_call_cancel_before_wait();
    test_call_cancel_during_wait();
    test_call_spurious_wake();
    test_call_wake_of_others();
    test_call_reply_races_cancel();
    test_call_no_stale_wake();
    test_call_no_cancel();
    test_call_unsent();
    test_call_deadline();
    test_call_reply_before_reset();
    test_call_module_restart();
    test_call_reset_while_making_request();
    printf(failures ? "wifi_cmd: %d FAILED\n" : "wifi_cmd: all passed\n", failures);
    return failures ? 1 : 0;
}
