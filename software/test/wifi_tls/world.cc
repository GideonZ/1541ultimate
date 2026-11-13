/*
 * world.cc: see world.h.
 */
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
int world_isr_calls;

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
    world_trace.clear();
    queue.clear();
    world_suspended = 0;
    world_transmits = 0;
    world_isr_calls = 0;
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

// The receive interrupt, as wifi_rx_isr handles a frame on the dedicated thread.
static void isr_deliver(void)
{
    command_buf_t *rx = &rx_bufs[rx_next++ % RX_BUFFERS];
    int kind = calm ? 0 : (int)world_rand(10);
    make_reply(rx, last_tx, last_tx_size, kind);
    uint32_t generation = uart.generation;
    if (!calm && (world_rand(10) == 0)) {
        generation--; // tagged before a reset that has since happened
    }
    world_isr_calls++;
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

void world_reset_maker(world_maker_t maker)
{
    make_reply = maker;
}

void world_deliver_now(void)
{
    isr_deliver();
}

TickType_t xTaskGetTickCount(void)
{
    if (!calm && (world_rand(5) == 0)) {
        now += world_rand(11); // preempted for a while
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
    if (q != (QueueHandle_t)&queue) {
        trace(" wrong queue\n");
        return pdFALSE;
    }
    if (queue.empty() && (ticks > 0)) {
        if (calm) {
            if (!calm_delivered) {
                calm_delivered = true;
                now += 1;
                isr_deliver();
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
                isr_deliver();
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
    memcpy(last_tx, buf->data, CMD_BUF_SIZE);
    last_tx_size = buf->size;
    // Late replies to earlier requests, or even this one, before the wait.
    if (!calm && (world_rand(100) < 15)) {
        int burst = 1 + world_rand(2);
        for (int i = 0; i < burst; i++) {
            isr_deliver();
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
