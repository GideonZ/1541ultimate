// world.cc: see world.h.
#include <deque>
#include <stdarg.h>
#include <string.h>
#include "world.h"

Esp32 esp32;
uint16_t sequence_nr;
const char *no_wifi_buf = "No buffer for WiFi command.\n";
QueueHandle_t dedicated_replies;
volatile bool dedicated_waiting;

bool world_ready;
bool world_no_buffer;
bool world_no_transmit;
std::string world_trace;
int world_suspended;
int world_transmits;
uint64_t world_wait_span;
TickType_t world_transmit_ticks;
int world_late_after_poll;

#define QUEUE_DEPTH 4 // as wifi_cmd.cc creates it
#define RX_BUFFERS  16

static DmaUART uart;
static std::deque<dedicated_reply_t> queue;
static uint32_t rng;
static TickType_t now;
static command_buf_t tx_buf;
static command_buf_t rx_bufs[RX_BUFFERS];
static int rx_next;
static uint8_t last_tx[CMD_BUF_SIZE];
static int last_tx_size;
static world_maker_t make_reply;
static bool calm;
static bool calm_delivered;
static const world_step_t *script;
static int script_left;
static TickType_t script_waited; // of the current step's delay, in earlier waits
static TickType_t preempt_next;  // added at the next tick read
static bool start_pending;       // a request went out; the next tick read starts its deadline
static TickType_t start_tick;

static void trace(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    world_trace += line;
}

uint32_t world_rand(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? (rng % n) : 0;
}

size_t world_queued(void)
{
    return queue.size();
}

void world_reset(uint32_t seed, world_maker_t maker, bool quiet)
{
    rng = (seed * 2654435761u) ^ 0x9E3779B9u;
    if (!rng) {
        rng = 1;
    }
    make_reply = maker;
    calm = quiet;
    calm_delivered = false;
    script = NULL;
    script_left = 0;
    script_waited = 0;
    preempt_next = 0;
    start_pending = false;
    start_tick = 0;
    world_wait_span = 0;
    world_transmit_ticks = 0;
    world_late_after_poll = 0;
    world_trace.clear();
    queue.clear();
    world_suspended = 0;
    world_transmits = 0;
    rx_next = 0;
    last_tx_size = -1;
    // Now and then start just short of the tick counter wrapping.
    now = (world_rand(4) == 0) ? (0xFFFFFFFFu - world_rand(400)) : world_rand(100000);
    sequence_nr = (uint16_t)world_rand(65536);
    uart.generation = world_rand(1000);
    esp32.uart = &uart;
    world_ready = calm || (world_rand(100) >= 3);
    world_no_buffer = false;
    world_no_transmit = false;
    dedicated_replies = (!calm && (world_rand(100) < 2)) ? NULL : (QueueHandle_t)&queue;
    dedicated_waiting = !calm && (world_rand(100) < 5); // another caller holds the channel
    memset(tx_buf.data, 0xAB, sizeof(tx_buf.data));
    tx_buf.bufnr = 3;
    for (int i = 0; i < RX_BUFFERS; i++) {
        rx_bufs[i].bufnr = 0x40 | i;
    }
}

void world_script(const world_step_t *steps, int count)
{
    script = steps;
    script_left = count;
    script_waited = 0;
}

// The receive interrupt, as wifi_rx_isr handles a frame on the dedicated thread.
static void isr_deliver(int kind, bool stale)
{
    command_buf_t *rx = &rx_bufs[rx_next++ % RX_BUFFERS];
    make_reply(rx, last_tx, last_tx_size, kind);
    uint32_t generation = uart.generation;
    if (stale) {
        generation--; // tagged before a reset that has since happened
    }
    if (!dedicated_waiting) {
        trace("isr drop b%x (not waiting)\n", rx->bufnr);
    } else if (queue.size() >= QUEUE_DEPTH) {
        trace("isr drop b%x (full)\n", rx->bufnr);
    } else {
        dedicated_reply_t reply = { rx, generation };
        queue.push_back(reply);
        trace("isr queue b%x g%u k%d\n", rx->bufnr, generation, kind);
    }
}

static void isr_deliver_random(void)
{
    int kind = calm ? 0 : (int)world_rand(10);
    isr_deliver(kind, !calm && (world_rand(10) == 0));
}

// One wait of a scripted world: the next step, if it falls within the wait.
static void scripted_wait(TickType_t ticks)
{
    if (!script_left || (script->delay - script_waited > ticks)) {
        if (script_left) {
            script_waited += ticks;
        }
        now += ticks;
        return;
    }
    TickType_t delay = script->delay - script_waited;
    now += delay;
    for (int i = 0; i < script->count; i++) {
        isr_deliver(script->kind[i], script->stale[i]);
    }
    preempt_next += script->preempt;
    script++;
    script_left--;
    script_waited = 0;
    if (queue.empty()) {
        now += ticks - delay; // nothing came through; wait it out
    }
}

TickType_t xTaskGetTickCount(void)
{
    if (!calm && (world_rand(5) == 0)) {
        now += world_rand(11); // preempted for a while
    }
    now += preempt_next;
    preempt_next = 0;
    if (start_pending) {
        start_pending = false;
        start_tick = now;
    }
    trace("tick %u\n", now);
    return now;
}

void vTaskSuspendAll(void)
{
    world_suspended++;
    trace("suspend\n");
}

BaseType_t xTaskResumeAll(void)
{
    world_suspended--;
    trace("resume\n");
    return pdFALSE;
}

void vTaskDelay(TickType_t ticks)
{
    now += ticks;
    trace("delay %u\n", ticks);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    trace("receive %u", ticks);
    uint64_t reach = (uint64_t)(TickType_t)(now - start_tick) + ticks; // no wrap: a huge wait must show
    if ((ticks > 0) && (reach > world_wait_span)) {
        world_wait_span = reach;
    }
    if (q != (QueueHandle_t)&queue) {
        trace(" wrong queue\n");
        return pdFALSE;
    }
    if (queue.empty() && (ticks > 0)) {
        if (script) {
            scripted_wait(ticks);
        } else if (calm) {
            if (!calm_delivered) {
                calm_delivered = true;
                now += 1;
                isr_deliver_random();
            } else {
                now += ticks;
            }
        } else if (world_rand(10) < 6) {
            TickType_t delay = world_rand(ticks);
            now += delay;
            if (world_rand(10) == 0) {
                uart.generation++;
                trace(" reset");
            }
            int burst = 1 + world_rand((world_rand(4) == 0) ? 6 : 3); // more than the queue holds, now and then
            for (int i = 0; i < burst; i++) {
                isr_deliver_random();
            }
            if (queue.empty()) {
                now += ticks - delay; // nothing came through; wait it out
            }
        } else {
            now += ticks;
        }
    }
    if (queue.empty()) {
        trace(" -> none\n");
        if ((ticks == 0) && (world_late_after_poll > 0)) {
            world_late_after_poll--;
            isr_deliver(0, false);
        }
        return pdFALSE;
    }
    *(dedicated_reply_t *)item = queue.front();
    queue.pop_front();
    trace(" -> b%x\n", ((dedicated_reply_t *)item)->buf->bufnr);
    return pdTRUE;
}

BaseType_t DmaUART::GetBuffer(command_buf_t **buf, TickType_t ticks)
{
    trace("getbuffer %u", ticks);
    if (world_no_buffer || (!calm && (world_rand(100) < 5))) {
        now += ticks;
        trace(" -> none\n");
        return pdFALSE;
    }
    tx_buf.size = 0;
    tx_buf.dropped = 0;
    *buf = &tx_buf;
    trace(" -> b%x\n", tx_buf.bufnr);
    return pdTRUE;
}

BaseType_t DmaUART::TransmitPacket(command_buf_t *buf, uint16_t *ms)
{
    trace("transmit %d:", buf->size);
    for (int i = 0; (i < buf->size) && (i < CMD_BUF_SIZE); i++) {
        trace(" %02x", buf->data[i]);
    }
    if (world_no_transmit || (!calm && (world_rand(100) < 5))) {
        trace(" -> failed\n");
        return pdFALSE; // the real one frees the buffer itself
    }
    trace("\n");
    world_transmits++;
    now += world_transmit_ticks;
    start_pending = true;
    memcpy(last_tx, buf->data, CMD_BUF_SIZE);
    last_tx_size = buf->size;
    // Late replies to earlier requests, or even this one, before the wait.
    if (!calm && (world_rand(100) < 15)) {
        int burst = 1 + world_rand(2);
        for (int i = 0; i < burst; i++) {
            isr_deliver_random();
        }
    }
    return pdTRUE;
}

BaseType_t DmaUART::FreeBuffer(command_buf_t *buf)
{
    trace("free b%x%s\n", buf->bufnr, world_suspended ? "" : " (not suspended)");
    return pdTRUE;
}

void world_enter_safe(void)
{
    trace("enter\n");
}

void world_leave_safe(void)
{
    trace("leave\n");
}

int world_printf(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    world_trace += "print: ";
    world_trace += line;
    return n;
}

bool wifi_module_ready(void)
{
    return world_ready;
}
