#ifndef IOMAPTESTER_H
#define IOMAPTESTER_H

#include <stdint.h>

#define IO_BASE 0xA0000000
#define FLASH_BASE         (IO_BASE + 0x300)
#define UART_BASE          (IO_BASE + 0x400)

#define UART_DATA  (UART_BASE + 0x0)
#define UART_GET   (UART_BASE + 0x1)
#define UART_FLAGS (UART_BASE + 0x2)
#define UART_ICTRL (UART_BASE + 0x3)

#define UART_Overflow    0x01
#define UART_TxFifoFull  0x10
#define UART_RxFifoFull  0x20
#define UART_TxReady     0x40
#define UART_RxDataAv    0x80

#define ioWrite8(x, y)  (*(volatile uint8_t *)(x)) = y
#define ioRead8(x)      (*(volatile uint8_t *)(x))

#endif
