/*
 * Host test of the real DmaUART::ClearRxBuffer (dma_uart.cc): the reset of the buffer pool, which the
 * calls to the WiFi module count in DmaUART::generation to tell that the module restarted. The rest of
 * dma_uart.cc is compiled but not run; the queues, the buffer pool (cmd_buffer.c), the interrupt
 * hookup and the scheduler are stubs that only record what happens, and the registers are a struct.
 */
#include <stdio.h>
#include "dma_uart.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int suspended;
static int ever_suspended;
static int queue_resets;
static int queue_resets_unsuspended;
static int pool_resets;
static int pool_resets_unsuspended;
static uint32_t generation_at_pool_reset;
static uint32_t generation_at_resume;
static DmaUART *the_uart;

extern "C" {
void vTaskSuspendAll(void) { suspended++; ever_suspended++; }
BaseType_t xTaskResumeAll(void)
{
    suspended--;
    generation_at_resume = the_uart->generation;
    return pdFALSE;
}
QueueHandle_t xQueueCreate(int length, int item_size) { static int q; return &q; }
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t newQueue)
{
    queue_resets++;
    queue_resets_unsuspended += (suspended == 0);
    return pdTRUE;
}
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w) { return pdFALSE; }
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *w) { return pdFALSE; }
unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return 0; }

void cmd_buffer_reset(command_buf_context_t *context)
{
    pool_resets++;
    pool_resets_unsuspended += (suspended == 0);
    generation_at_pool_reset = the_uart->generation;
}
BaseType_t cmd_buffer_free(command_buf_context_t *c, command_buf_t *b) { return pdTRUE; }
BaseType_t cmd_buffer_get(command_buf_context_t *c, command_buf_t **b, TickType_t t) { return pdFALSE; }
BaseType_t cmd_buffer_transmit(command_buf_context_t *c, command_buf_t *b) { return pdFALSE; }
BaseType_t cmd_buffer_received(command_buf_context_t *c, command_buf_t **b, TickType_t t) { return pdFALSE; }
BaseType_t cmd_buffer_get_tx_isr(command_buf_context_t *c, command_buf_t **b, BaseType_t *w) { return pdFALSE; }
BaseType_t cmd_buffer_free_isr(command_buf_context_t *c, command_buf_t *b, BaseType_t *w) { return pdFALSE; }
BaseType_t cmd_buffer_get_free_isr(command_buf_context_t *c, command_buf_t **b, BaseType_t *w) { return pdFALSE; }
BaseType_t cmd_buffer_received_isr(command_buf_context_t *c, command_buf_t *b, BaseType_t *w) { return pdFALSE; }

void install_high_irq(int irqNr, uint8_t (*func)(void *), void *context) {}
uint16_t getMsTimer() { return 0; }
void ioWrite8(uint32_t addr, uint8_t value) {}
uint8_t ioRead8(uint32_t addr) { return 0; }
}

static dma_uart_t regs;
static command_buf_context_t pool;

int main(void)
{
    DmaUART uart(&regs, 3, NULL, &pool);
    the_uart = &uart;
    CHECK(uart.generation == 0);

    regs.flowctrl = 0;
    uart.ClearRxBuffer();
    // Everything is reset with the scheduler suspended, the scheduler runs again afterwards, and the
    // reset is counted before it does, and after the pool has been refilled
    CHECK(suspended == 0);
    CHECK(ever_suspended == 1);
    CHECK(queue_resets == 1 && queue_resets_unsuspended == 0);
    CHECK(pool_resets == 1 && pool_resets_unsuspended == 0);
    CHECK(generation_at_pool_reset == 0);
    CHECK(generation_at_resume == 1);
    CHECK(uart.generation == 1);
    CHECK(regs.flowctrl & DMAUART_RESET);

    uart.ClearRxBuffer();
    CHECK(suspended == 0);
    CHECK(uart.generation == 2);
    CHECK(generation_at_pool_reset == 1 && generation_at_resume == 2);

    printf(failures ? "dma_uart: %d FAILED\n" : "dma_uart: all passed\n", failures);
    return failures ? 1 : 0;
}
