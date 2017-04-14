#ifndef IOMAP_H
#define IOMAP_H

#include <stdint.h>

#ifndef IOBASE
#define IOBASE 0x4000000
#endif

#define ITU_BASE           (IOBASE + 0x00000)
#define DRIVE_A_BASE       (IOBASE + 0x20000)
#define DRIVE_B_BASE       (IOBASE + 0x24000)
#define IEC_BASE           (IOBASE + 0x28000)
#define LED_BASE		   (IOBASE + 0x2C000)
#define C64_CARTREGS_BASE  (IOBASE + 0x40000)
#define SID_BASE           (IOBASE + 0x42000)
#define CMD_IF_BASE        (IOBASE + 0x44000)
#define COPPER_BASE        (IOBASE + 0x46000)
#define SAMPLER_BASE       (IOBASE + 0x48000)
#define C64_MEMORY_BASE    (IOBASE + 0x50000)
#define SDCARD_BASE        (IOBASE + 0x60000)
#define RTC_BASE           (IOBASE + 0x60100)
#define FLASH_BASE         (IOBASE + 0x60200)
#define TRACE_BASE         (IOBASE + 0x60300)
#define RTC_TIMER_BASE     (IOBASE + 0x60400)
#define GCR_CODER_BASE     (IOBASE + 0x60500)
#define ICAP_BASE          (IOBASE + 0x60600)
#define AUDIO_SEL_BASE     (IOBASE + 0x60700)
#define RMII_BASE          (IOBASE + 0x60800)
#define USB_BASE           (IOBASE + 0x80000)
#define C2N_PLAY_BASE      (IOBASE + 0xA0000)
#define C2N_RECORD_BASE    (IOBASE + 0xC0000)
#define OVERLAY_BASE       (IOBASE + 0xE0000)

#ifndef RUNS_ON_PC
#define ioWrite8(x, y)  (*(volatile uint8_t *)(x)) = y
#define ioRead8(x)      (*(volatile uint8_t *)(x))

//#define ioWrite16(x, y) (*(volatile uint16_t *)(x)) = y
//#define ioWrite32(x, y) (*(volatile uint32_t *)(x)) = y
//#define ioRead16(x)     (*(volatile uint16_t *)(x))
//#define ioRead32(x)     (*(volatile uint32_t *)(x))

#else

void ioWrite8(uint32_t addr, uint8_t value);
void ioWrite16(uint32_t addr, uint16_t value);
void ioWrite32(uint32_t addr, uint32_t value);

uint8_t  ioRead8(uint32_t addr);
uint16_t ioRead16(uint32_t addr);
uint32_t ioRead32(uint32_t addr);
#endif

#endif
