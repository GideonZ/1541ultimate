#ifndef ITU_H
#define ITU_H
#include "integer.h"
#include "iomap.h"

// Definitions for the ITU (Interrupt, Timer, Uart)
#define ITU_IRQ_GLOBAL    *((volatile BYTE *)(ITU_BASE + 0x00))
#define ITU_IRQ_ENABLE    *((volatile BYTE *)(ITU_BASE + 0x01))
#define ITU_IRQ_DISABLE   *((volatile BYTE *)(ITU_BASE + 0x02))
#define ITU_IRQ_EDGE      *((volatile BYTE *)(ITU_BASE + 0x03))
#define ITU_IRQ_CLEAR     *((volatile BYTE *)(ITU_BASE + 0x04))
#define ITU_IRQ_ACTIVE    *((volatile BYTE *)(ITU_BASE + 0x05))
#define ITU_TIMER         *((volatile BYTE *)(ITU_BASE + 0x06))
#define ITU_IRQ_TIMER_EN  *((volatile BYTE *)(ITU_BASE + 0x07))
#define ITU_IRQ_TIMER_LO  *((volatile BYTE *)(ITU_BASE + 0x09))
#define ITU_IRQ_TIMER_HI  *((volatile BYTE *)(ITU_BASE + 0x08))
#define ITU_FPGA_VERSION  *((volatile BYTE *)(ITU_BASE + 0x0B))
#define ITU_CAPABILITIES  *((volatile DWORD*)(ITU_BASE + 0x0C))
#define CAPABILITIES      *((volatile DWORD*)(ITU_BASE + 0x0C))
#define ITU_MS_TIMER_LO   *((volatile BYTE *)(ITU_BASE + 0x23))
#define ITU_MS_TIMER_HI   *((volatile BYTE *)(ITU_BASE + 0x22))
#define ITU_MS_TIMER      *((volatile WORD *)(ITU_BASE + 0x22))

#define CAPAB_UART          0x00000001
#define CAPAB_DRIVE_1541_1  0x00000002
#define CAPAB_DRIVE_1541_2  0x00000004
#define CAPAB_DRIVE_SOUND   0x00000008
#define CAPAB_HARDWARE_GCR  0x00000010
#define CAPAB_HARDWARE_IEC  0x00000020
#define CAPAB_IEC_PROG_TIM  0x00000040
#define CAPAB_C2N_STREAMER  0x00000080
#define CAPAB_C2N_RECORDER  0x00000100
#define CAPAB_CARTRIDGE     0x00000200
#define CAPAB_RAM_EXPANSION 0x00000400
#define CAPAB_USB_HOST      0x00000800
#define CAPAB_RTC_CHIP      0x00001000
#define CAPAB_RTC_TIMER     0x00002000
#define CAPAB_SPI_FLASH     0x00004000
#define CAPAB_ICAP          0x00008000
#define CAPAB_EXTENDED_REU  0x00010000
#define CAPAB_STEREO_SID    0x00020000
#define CAPAB_COMMAND_INTF  0x00040000
#define CAPAB_COPPER        0x00080000
#define CAPAB_OVERLAY       0x00100000
#define CAPAB_SAMPLER       0x00200000
#define CAPAB_ANALYZER      0x00400000
#define CAPAB_USB_HOST2     0x00800000
#define CAPAB_FPGA_TYPE     0x30000000
#define CAPAB_BOOT_FPGA     0x40000000
#define CAPAB_SIMULATION    0x80000000

#define FPGA_TYPE_SHIFT     28

#define ENTER_SAFE_SECTION ITU_IRQ_GLOBAL=0;
#define LEAVE_SAFE_SECTION ITU_IRQ_GLOBAL=1;

#define ITU_C64_IRQ 0x10
#define ITU_BUTTON0 0x20
#define ITU_BUTTON1 0x40
#define ITU_BUTTON2 0x80
#define ITU_BUTTONS 0xE0

// Timer functions
void wait_ms(int);
void outbyte(int c);

#define UART_DATA  *((volatile BYTE *)(ITU_BASE + 0x10))
#define UART_GET   *((volatile BYTE *)(ITU_BASE + 0x11))
#define UART_FLAGS *((volatile BYTE *)(ITU_BASE + 0x12))
#define UART_ICTRL *((volatile BYTE *)(ITU_BASE + 0x13))

#define UART_Overflow    0x01
#define UART_TxFifoFull  0x10
#define UART_RxFifoFull  0x20
#define UART_TxReady     0x40
#define UART_RxDataAv    0x80

// Uart Functions
SHORT uart_read_buffer(const void *buf, USHORT count);
SHORT uart_write_buffer(const void *buf, USHORT count);
SHORT uart_write_hex(BYTE b);
BOOL uart_data_available(void);
int uart_get_byte(int delay);


#endif
