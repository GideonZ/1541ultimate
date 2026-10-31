/*
 * isr_test.cc
 *
 * The receive interrupt's dispatch of a frame (wifi_rx_isr()) and the UART's buffer pool
 * reset (DmaUART::ClearRxBuffer()), both cut out of their files as they are, against
 * recording stubs.
 */

#include <stdio.h>
#include "isr_env.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s:%d: %s (log \"%s\")\n", __func__, __LINE__, what, rlog); failures++; } } while (0)

static char rlog[64];
static void note(char c) { size_t n = strlen(rlog); if (n + 1 < sizeof(rlog)) { rlog[n] = c; rlog[n + 1] = 0; } }

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

static DmaUART uart;
static dma_uart_t regs;
static command_buf_context_t packets;
static int queue_tag, task_tag;
Esp32Stub esp32 = { &uart };
wifi_reply_slot_t replySlots[WIFI_REPLY_SLOTS];
QueueHandle_t dedicated_replies = &queue_tag;
volatile bool dedicated_waiting;

static dedicated_reply_t queued;
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w) { note('q'); queued = *(const dedicated_reply_t *)item; return pdTRUE; }
BaseType_t notify_from_isr(TaskHandle_t t, eNotifyAction a, BaseType_t *w) { note('n'); return pdTRUE; }
BaseType_t cmd_buffer_received_isr(command_buf_context_t *context, command_buf_t *b, BaseType_t *w) { note('e'); return pdTRUE; }
void ioWrite8(int reg, uint8_t value) {}
void vTaskSuspendAll(void) { note('{'); }
BaseType_t xTaskResumeAll(void) { note('}'); return pdFALSE; }
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t new_queue) { note('r'); return pdTRUE; }
static uint32_t gen_before;
void cmd_buffer_reset(command_buf_context_t *context) { note('c'); if (uart.generation != gen_before) note('!'); }

static BaseType_t frame(uint8_t thread, uint16_t sequence = 0)
{
    static command_buf_t buf;
    memset(&buf, 0, sizeof(buf));
    ((rpc_header_t *)buf.data)->thread = thread;
    ((rpc_header_t *)buf.data)->sequence = sequence;
    rlog[0] = 0;
    BaseType_t w = pdFALSE;
    return wifi_rx_isr(&packets, &buf, &w);
}

static void dedicated_channel(void)
{
    dedicated_waiting = false;
    CHECK(frame(WIFI_DEDICATED_THREAD) == pdFALSE, "nobody waits: the buffer goes back");
    CHECK(rlog[0] == 0, "and is not queued or handed on");
    dedicated_waiting = true;
    uart.generation = 7;
    CHECK(frame(WIFI_DEDICATED_THREAD) == pdTRUE, "a waiting caller");
    CHECK((strcmp(rlog, "q") == 0) && (queued.generation == 7), "gets it, with the pool's generation");
    dedicated_waiting = false;
}

static void other_threads(void)
{
    replySlots[3].task = &task_tag;
    replySlots[3].sequence = 77;
    frame(3, 76);
    CHECK(strcmp(rlog, "e") == 0, "a reply with another sequence number is not the slot's");
    frame(3, 77);
    CHECK(strcmp(rlog, "n") == 0, "a reply goes to its waiting task");
    CHECK(replySlots[3].task == NULL, "once");
    frame(3, 77);
    CHECK(strcmp(rlog, "e") == 0, "without one, to the event queue");
    frame(0xFF);
    CHECK(strcmp(rlog, "e") == 0, "events too");
}

static void pool_reset(void)
{
    uart.uart = &regs;
    uart.packets = &packets;
    uart.generation = 41;
    gen_before = 41;
    rlog[0] = 0;
    uart.ClearRxBuffer();
    CHECK(uart.generation == 42, "a reset counts as one generation");
    CHECK((rlog[0] == '{') && (rlog[strlen(rlog) - 1] == '}'), "with the scheduler suspended");
    CHECK((strchr(rlog, 'r') != NULL) && (strchr(rlog, 'c') != NULL) && (strchr(rlog, '!') == NULL), "after both resets");
}

int main(void)
{
    dedicated_channel();
    other_threads();
    pool_reset();
    if (failures) {
        printf("getrandompaths isr: %d check(s) failed\n", failures);
        return 1;
    }
    printf("getrandompaths isr: all checks passed\n");
    return 0;
}
