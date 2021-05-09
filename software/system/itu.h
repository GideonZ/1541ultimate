#ifndef ITU_H
#define ITU_H

#include "integer.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "iomap.h"

// Definitions for the ITU (Interrupt, Timer, Uart)
#define ITU_IRQ_GLOBAL    (ITU_BASE + 0x00)
#define ITU_IRQ_ENABLE    (ITU_BASE + 0x01)
#define ITU_IRQ_DISABLE   (ITU_BASE + 0x02)
#define ITU_IRQ_EDGE      (ITU_BASE + 0x03)
#define ITU_IRQ_CLEAR     (ITU_BASE + 0x04)
#define ITU_IRQ_ACTIVE    (ITU_BASE + 0x05)
#define ITU_TIMER         (ITU_BASE + 0x06)
#define ITU_IRQ_TIMER_EN  (ITU_BASE + 0x07)
#define ITU_IRQ_TIMER_HI  (ITU_BASE + 0x08)
#define ITU_IRQ_TIMER_LO  (ITU_BASE + 0x09)
#define ITU_BUTTON_REG    (ITU_BASE + 0x0A)
#define ITU_FPGA_VERSION  (ITU_BASE + 0x0B)
#define ITU_MS_TIMER_LO   (ITU_BASE + 0x23)
#define ITU_MS_TIMER_HI   (ITU_BASE + 0x22)
#define __ITU_MS_TIMER    (ITU_BASE + 0x22)
#define ITU_USB_BUSY	  (ITU_BASE + 0x24)
#define ITU_SD_BUSY	      (ITU_BASE + 0x25)
#define ITU_MISC_IO	      (ITU_BASE + 0x26)
#define ITU_IRQ_HIGH_EN   (ITU_BASE + 0x27)
#define ITU_IRQ_HIGH_ACT  (ITU_BASE + 0x28)

#define ITU_INTERRUPT_TIMER  0x01
#define ITU_INTERRUPT_UART   0x02
#define ITU_INTERRUPT_USB    0x04
#define ITU_INTERRUPT_TAPE   0x08
#define ITU_INTERRUPT_CMDIF  0x10
#define ITU_INTERRUPT_RMIIRX 0x20
#define ITU_INTERRUPT_RMIITX 0x40
#define ITU_INTERRUPT_RESET  0x80
#define ITU_IRQHIGH_ACIA     0x01

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
#define CAPAB_UNUSED		0x00400000
#define CAPAB_USB_HOST2     0x00800000
#define CAPAB_ETH_RMII		0x01000000
#define CAPAB_ULTIMATE2PLUS 0x02000000
#define CAPAB_ULTIMATE64    0x04000000
#define CAPAB_ACIA          0x08000000
#define CAPAB_FPGA_TYPE     0x30000000
#define CAPAB_BOOT_FPGA     0x40000000
#define CAPAB_SIMULATION    0x80000000

#define FPGA_TYPE_SHIFT     28

#ifdef OS
# include "portmacro.h"
# define ENTER_SAFE_SECTION portDISABLE_INTERRUPTS(); // ioWrite8(ITU_IRQ_GLOBAL,0);
# define LEAVE_SAFE_SECTION portENABLE_INTERRUPTS();  // ioWrite8(ITU_IRQ_GLOBAL,1);
#else
# define ENTER_SAFE_SECTION
# define LEAVE_SAFE_SECTION
#endif

#define ITU_BUTTON0 0x20
#define ITU_BUTTON1 0x40
#define ITU_BUTTON2 0x80
#define ITU_BUTTONS 0xE0

// Timer functions
void wait_ms(int);
void wait_10us(uint8_t mult); // max=126
void outbyte(int c);
uint8_t getButtons(void);
void itu_clear_irqs(void);

#define UART_DATA  (ITU_BASE + 0x10)
#define UART_GET   (ITU_BASE + 0x11)
#define UART_FLAGS (ITU_BASE + 0x12)
#define UART_ICTRL (ITU_BASE + 0x13)

#define UART_Overflow    0x01
#define UART_TxFifoFull  0x10
#define UART_RxFifoFull  0x20
#define UART_TxReady     0x40
#define UART_RxDataAv    0x80

// Uart Functions
uint16_t uart_read_buffer(const void *buf, uint16_t count);
uint16_t uart_write_buffer(const void *buf, uint16_t count);
uint16_t uart_write_hex(uint8_t b);
BOOL uart_data_available(void);
int uart_get_byte(int delay);
uint16_t getMsTimer();
uint32_t getFpgaCapabilities();
uint8_t  getFpgaVersion();

extern void (*custom_outbyte)(int c);

#ifdef __cplusplus
}
#endif

#endif
