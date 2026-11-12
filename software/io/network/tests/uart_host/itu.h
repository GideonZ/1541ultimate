// Stand-in for the I/O of itu.h that dma_uart.cc uses. The console bytes the ISR writes
// (ioWrite8(UART_DATA, ...)) are collected by the test.
#ifndef UART_HOST_ITU_H
#define UART_HOST_ITU_H
#include <stdint.h>
#define CLOCK_FREQ       50000000
#define UART_DATA        ((volatile uint8_t *)0x1000)
#define ITU_IRQ_HIGH_EN  ((volatile uint8_t *)0x2000)
#ifdef __cplusplus
// dma_uart.h and dma_uart.cc print the volatile register fields (printf("%b", uart->status)), which
// the proxies of dma_uart_regs.h cannot pass through a variadic call. The test's console is not
// printf(), so the prints are taken by a template that discards them.
#include <stdio.h>
template <typename... Args> inline int host_printf(const char *, const Args &...) { return 0; }
#define printf(...) host_printf(__VA_ARGS__)
#endif
#ifdef __cplusplus
extern "C" {
#endif
void ioWrite8(volatile uint8_t *reg, uint8_t value);
uint8_t ioRead8(volatile uint8_t *reg);
uint16_t getMsTimer(void);
typedef uint8_t (*irq_function_t)(void *context);
void install_high_irq(int irq, irq_function_t func, void *context);
#ifdef __cplusplus
}
#endif
#endif
