// The calls isr_test.cpp makes into isr_stubs.c
#ifndef ISR_TEST_API_H_
#define ISR_TEST_API_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int console_writes;          // writes of the handler to the UART0 console
void isr_reset(void);               // fresh buffers, FIFOs, interrupt state
void isr_interrupt(const uint8_t *bytes, int n, uint32_t extra); // one interrupt with these n bytes in the RX FIFO
void isr_overflow(void);            // the RX FIFO overflows: its interrupt status (enabled or not, as the module has it)
int isr_received(int *size, int *dropped, int keep); // the next frame the dispatcher would get; given back unless keep
void isr_tx_interrupt(void);        // one TX interrupt: the UART has sent what it had, there is room for one small frame
void isr_txm(int on);               // model: a 128 byte TX FIFO and an edge-like TXFIFO_EMPTY (isr_stubs.c)
void isr_service_tx(void);          // the TX interrupt, serviced once if it is asserted
void isr_uart_drain(int n);         // the UART sends n bytes of its FIFO
int isr_tx_fifo_used(void);
int isr_tx_total(void);             // bytes written to the TX FIFO since isr_reset()
int isr_reply(int size);            // a reply of this size from the dispatcher (a task: it kicks the interrupt)
int isr_tx_settle(int max_rounds);  // drain + service until every queued frame is out; returns the rounds (max_rounds: never)
int isr_tx_pending(void);           // frames not yet sent whole
int isr_event(void);                // a frame from the dispatcher (my_uart_get_buffer, then sent): 1 if it got a buffer
#ifdef __cplusplus
}
#endif
#endif
