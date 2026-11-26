#ifndef STUB_DMA_UART_H_
#define STUB_DMA_UART_H_
#include "FreeRTOS.h"
extern "C" {
    #include "cmd_buffer.h"
}
// Only what the command channel calls; GetBuffer has no buffer to give
class DmaUART
{
public:
    int buffers_free = 0;
    int transmitted = 0;
    volatile uint32_t generation = 0; // U64 >= 1 builds of wifi_cmd.cc tag dedicated replies with it
    BaseType_t GetBuffer(command_buf_t **b, TickType_t t);
    void FreeBuffer(command_buf_t *b);
    void SetBaudRate(int b) {}
    void FlowControl(bool on) {}
    void ClearRxBuffer() { generation++; }
    void EnableSlip(bool on) {}
    void SetReceiveCallback(BaseType_t (*cb)(command_buf_context_t *, command_buf_t *, BaseType_t *)) {}
    void EnableIRQ(bool on) {}
    BaseType_t ReceivePacket(command_buf_t **b, TickType_t t) { return pdFALSE; }
    void (*on_transmit)(command_buf_t *) = NULL; // the module answers at once
    bool transmit_fails = false;      // the queue is full: the UART frees the buffer itself
    BaseType_t TransmitPacket(command_buf_t *b, uint16_t *ms = NULL)
    {
        if (transmit_fails) {
            FreeBuffer(b);
            return pdFALSE;
        }
        transmitted++;
        if (on_transmit) {
            on_transmit(b);
        }
        return pdTRUE;
    }
};
#define UART_DATA 0
static inline void ioWrite8(int reg, int v) {}
#endif
