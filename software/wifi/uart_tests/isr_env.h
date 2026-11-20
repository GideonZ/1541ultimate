/*
 * isr_env.h
 *
 * What the modules' UART interrupt handler (my_uart_intr_handler() in my_uart.c, cut out as it is) sees on the
 * build host: the HAL and register calls of a UART whose RX FIFO the cases fill (isr_stubs.c) and the real
 * buffer queues (software/io/uart/cmd_buffer.c over cmdbuf_queues.c). The handler's own types (my_uart_obj_t,
 * uart_context_t) are cut out of the module's my_uart.c by the Makefile, after this file.
 */

#ifndef ISR_ENV_H_
#define ISR_ENV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "freertos/queue.h"
#include "cmd_buffer.h"

typedef BaseType_t portBASE_TYPE;
typedef int uart_port_t;
typedef int portMUX_TYPE;
typedef struct { int unused; } uart_hal_context_t;
typedef struct { void *task; QueueHandle_t queue; } dispatcher_t; // rpc_dispatch.h

enum { UART_NUM_0, UART_NUM_1, UART_NUM_2, UART_NUM_MAX };
#define UART_INTR_RXFIFO_FULL  0x001
#define UART_INTR_TXFIFO_EMPTY 0x002
#define UART_INTR_RXFIFO_OVF   0x010
#define UART_INTR_RXFIFO_TOUT  0x100

#define UART_ISR_ATTR
#define UART_DEBUG_IRQ 0
#define portYIELD_FROM_ISR() ((void)0)
#define taskYIELD() ((void)0)
#define UART_ENTER_CRITICAL_ISR(mux) ((void)(mux))
#define UART_EXIT_CRITICAL_ISR(mux)  ((void)(mux))
#define UART_ENTER_CRITICAL(mux)     ((void)(mux))
#define UART_EXIT_CRITICAL(mux)      ((void)(mux))

// Register addresses: the RX FIFO, the TX FIFO and the UART0 console (writes there are counted)
#define UART_FIFO_REG(n)     (0x3FF40000u + (uint32_t)(n))
#define UART_FIFO_AHB_REG(n) (0x60000000u + (uint32_t)(n))
uint32_t isr_read_reg(uint32_t addr);
void isr_write_reg(uint32_t addr, uint32_t value);
#define READ_PERI_REG(addr)        isr_read_reg(addr)
#define WRITE_PERI_REG(addr, val)  isr_write_reg((addr), (val))

uint32_t uart_hal_get_intsts_mask(uart_hal_context_t *hal);
uint32_t uart_hal_get_intraw_mask(uart_hal_context_t *hal);
void uart_hal_disable_intr_mask(uart_hal_context_t *hal, uint32_t mask);
void uart_hal_ena_intr_mask(uart_hal_context_t *hal, uint32_t mask);
void uart_hal_clr_intsts_mask(uart_hal_context_t *hal, uint32_t mask);
uint16_t uart_hal_get_txfifo_len(uart_hal_context_t *hal);
uint32_t uart_hal_get_rxfifo_len(uart_hal_context_t *hal);
void uart_hal_rxfifo_rst(uart_hal_context_t *hal);
#define UART_DEBUG_TX 0

#endif /* ISR_ENV_H_ */
