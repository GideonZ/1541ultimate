#ifndef IOMAP_H
#define IOMAP_H

#include <stdint.h>

#define ITU_BASE           0x4000000
#define DRIVE_A_BASE       0x4020000
#define DRIVE_B_BASE       0x4024000
#define IEC_BASE           0x4028000
#define C64_CARTREGS_BASE  0x4040000
#define SID_BASE           0x4042000
#define CMD_IF_BASE        0x4044000
#define COPPER_BASE        0x4046000
#define SAMPLER_BASE       0x4048000
#define C64_MEMORY_BASE    0x4050000
#define SDCARD_BASE        0x4060000
#define RTC_BASE           0x4060100
#define FLASH_BASE         0x4060200
#define TRACE_BASE         0x4060300
#define RTC_TIMER_BASE     0x4060400
#define GCR_CODER_BASE     0x4060500
#define ICAP_BASE          0x4060600
#define AUDIO_SEL_BASE     0x4060700
#define USB_BASE           0x4080000
#define C2N_PLAY_BASE      0x40A0000
#define C2N_RECORD_BASE    0x40C0000
#define OVERLAY_BASE       0x40E0000

#ifndef RUNS_ON_PC
#define ioWrite8(x, y)  (*(volatile uint8_t *)(x)) = y
#define ioWrite16(x, y) (*(volatile uint16_t *)(x)) = y
#define ioWrite32(x, y) (*(volatile uint32_t *)(x)) = y

#define ioRead8(x)  (*(volatile uint8_t *)(x))
#define ioRead16(x) (*(volatile uint16_t *)(x))
#define ioRead32(x) (*(volatile uint32_t *)(x))
#else

void ioWrite8(uint32_t addr, uint8_t value);
void ioWrite16(uint32_t addr, uint16_t value);
void ioWrite32(uint32_t addr, uint32_t value);

uint8_t  ioRead8(uint32_t addr);
uint16_t ioRead16(uint32_t addr);
uint32_t ioRead32(uint32_t addr);
#endif

#endif
