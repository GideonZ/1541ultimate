#include "../../io/uart/dma_uart.h"
#include <stdio.h>
unsigned uart_critical_depth;
static unsigned checks, failures, losses, frees, resets, callbacks;
static bool in_isr, have_receive = true, accept_receive = true;
static dma_uart_t registers = {};
static command_buf_t receive_buffer = {};
static void expect(bool value, const char *label) { ++checks; if (!value) { ++failures; printf("FAIL %s\n", label); } }
static void loss() { expect(in_isr || uart_critical_depth, "loss callback serialized"); ++losses; }
QueueHandle_t xQueueCreate(unsigned, unsigned) { return &registers; }
BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*) { return pdTRUE; }
BaseType_t xQueueReceiveFromISR(QueueHandle_t, void *out, BaseType_t*) {
    registers.status = 0; // Emulate hardware acknowledgement to end the ISR loop.
    if (!have_receive) return pdFALSE;
    *static_cast<command_buf_t**>(out) = &receive_buffer; return pdTRUE;
}
BaseType_t xQueueGenericReset(QueueHandle_t, BaseType_t) { return pdTRUE; }
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
unsigned uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
extern "C" {
BaseType_t cmd_buffer_received_isr(command_buf_context_t*, command_buf_t*, BaseType_t*) { ++callbacks; return accept_receive; }
BaseType_t cmd_buffer_free_isr(command_buf_context_t*, command_buf_t*, BaseType_t*) { ++frees; return pdTRUE; }
BaseType_t cmd_buffer_get_free_isr(command_buf_context_t*, command_buf_t**, BaseType_t*) { return pdFALSE; }
BaseType_t cmd_buffer_get_tx_isr(command_buf_context_t*, command_buf_t**, BaseType_t*) { return pdFALSE; }
void cmd_buffer_reset(command_buf_context_t*) { ++resets; }
BaseType_t cmd_buffer_get(command_buf_context_t*, command_buf_t**, TickType_t) { return pdFALSE; }
BaseType_t cmd_buffer_received(command_buf_context_t*, command_buf_t**, TickType_t) { return pdFALSE; }
BaseType_t cmd_buffer_free(command_buf_context_t*, command_buf_t*) { return pdTRUE; }
BaseType_t cmd_buffer_transmit(command_buf_context_t*, command_buf_t*) { return pdTRUE; }
}
static void irq(DmaUART& uart, uint8_t status) {
    registers.status = status; in_isr = true; DmaUART::DmaUartInterrupt(&uart); in_isr = false;
}
int main()
{
    command_buf_context_t packets = {};
    DmaUART uart(&registers,1,NULL,&packets);
    irq(uart,8); expect(losses == 0, "no observer preserves default path");
    uart.SetLossCallback(loss);
    irq(uart,8); expect(losses == 1 && frees == 0, "overflow without packet reported without freeing buffers");
    irq(uart,1); expect(losses == 1 && callbacks == 1 && frees == 0, "accepted receive preserves ownership");
    have_receive = false; irq(uart,1);
    expect(losses == 2 && frees == 0, "missing DMA buffer signals loss without invented free");
    uart.ClearRxBuffer(); expect(losses == 3 && resets == 1, "receive reset signals loss");
    uart.SoftReset(); expect(losses == 4, "soft reset signals loss");
    uart.ResetReceiveCallback(); expect(losses == 5, "receiver handoff signals loss");
    uart.SetLossCallback(NULL); expect(losses == 6, "observer replacement retires previous lifetime");
    uart.SoftReset(); expect(losses == 6, "unregistered observer not called");
    expect(uart_critical_depth == 0, "critical sections balanced");
    printf("UART loss: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
