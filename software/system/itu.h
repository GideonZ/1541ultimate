#ifndef ITU_H
#define ITU_H
#include "integer.h"

// Definitions for the ITU (Interrupt, Timer, Uart)
#define ITU_IRQ_GLOBAL    *((volatile BYTE *)0x4000000)
#define ITU_IRQ_ENABLE    *((volatile BYTE *)0x4000001)
#define ITU_IRQ_DISABLE   *((volatile BYTE *)0x4000002)
#define ITU_IRQ_EDGE      *((volatile BYTE *)0x4000003)
#define ITU_IRQ_CLEAR     *((volatile BYTE *)0x4000004)
#define ITU_IRQ_ACTIVE    *((volatile BYTE *)0x4000005)
#define ITU_TIMER         *((volatile BYTE *)0x4000006)
#define ITU_IRQ_TIMER_EN  *((volatile BYTE *)0x4000007)
#define ITU_IRQ_TIMER_LO  *((volatile BYTE *)0x4000008)
#define ITU_IRQ_TIMER_HI  *((volatile BYTE *)0x4000009)
#define ITU_FPGA_VERSION  *((volatile BYTE *)0x400000F)

#define ENTER_SAFE_SECTION ITU_IRQ_GLOBAL=0;
#define LEAVE_SAFE_SECTION ITU_IRQ_GLOBAL=1;

#define ITU_C64_IRQ 0x10
#define ITU_BUTTON0 0x20
#define ITU_BUTTON1 0x40
#define ITU_BUTTON2 0x80
#define ITU_BUTTONS 0xE0

// Timer functions
void wait_ms(int);


#define UART_DATA  *((volatile BYTE *)0x4000010)
#define UART_GET   *((volatile BYTE *)0x4000011)
#define UART_FLAGS *((volatile BYTE *)0x4000012)
#define UART_ICTRL *((volatile BYTE *)0x4000013)

#define UART_Overflow    0x01
#define UART_TxFifoFull  0x10
#define UART_RxFifoFull  0x20
#define UART_TxReady     0x40
#define UART_RxDataAv    0x80

// Uart Functions
SHORT uart_read_buffer(const void *buf, USHORT count);
SHORT uart_write_buffer(const void *buf, USHORT count);
BOOL uart_data_available(void);
int uart_get_byte(int delay);


#endif
