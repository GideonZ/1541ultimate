#ifndef ESP32_DIAGNOSTIC_PLATFORM_H
#define ESP32_DIAGNOSTIC_PLATFORM_H
#include <stdint.h>

// C64 Ultimate RV32I / FPGA 122 register map. No generic board probing.
struct ControllerProbeHardware {
    alignas(4) uint8_t request[4];
    // uart_dma's SLIP decoder is bounded at 1536 bytes; reserve a full 64 KiB
    // for its 16-bit length register, keeping DMA separate from code and stack.
    alignas(4) uint8_t response[65536];
    uint8_t read(unsigned offset) { return *(volatile uint8_t *)(0x10060900u + offset); }
    void write(unsigned offset, uint8_t value) { *(volatile uint8_t *)(0x10060900u + offset) = value; }
    uint16_t milliseconds() {
        // Separate byte registers do not latch. Re-read low after a rollover.
        const uint8_t hi = *(volatile uint8_t *)0x10000022u;
        uint8_t lo = *(volatile uint8_t *)0x10000023u;
        const uint8_t next = *(volatile uint8_t *)0x10000022u;
        if (next != hi) lo = *(volatile uint8_t *)0x10000023u;
        return uint16_t(next) << 8 | lo;
    }
    uint8_t *tx() { return request; }
    const uint8_t *rx() { __asm__ volatile ("fence" ::: "memory"); return response; }
    void address(unsigned offset, const void *p) {
        uintptr_t value = reinterpret_cast<uintptr_t>(p);
        for (unsigned i = 0; i < 4; ++i) write(offset + i, uint8_t(value >> (i * 8)));
    }
    void receive() { address(12, response); }
    void submit(unsigned size) {
        __asm__ volatile ("fence" ::: "memory");
        address(4, request); write(8, uint8_t(size)); write(9, uint8_t(size >> 8)); write(10, 1);
    }
};

inline void diagnostic_line(unsigned row, const char *text)
{
    volatile uint8_t *regs = (volatile uint8_t *)0x10140000u;
    const unsigned columns = regs[4], rows = regs[5];
    if (columns < 32 || columns > 80 || rows < 8 || rows > 50 || row >= rows) return;
    volatile uint8_t *chars = (volatile uint8_t *)0x10141000u;
    volatile uint8_t *colors = (volatile uint8_t *)0x10142000u;
    regs[10] = 0; regs[11] = 0; regs[13] = 0xc0;
    for (unsigned i = 0; i < columns; ++i) {
        const uint8_t value = *text ? uint8_t(*text++) & 127 : 32;
        chars[row * columns + i] = value;
        colors[row * columns + i] = 0x1f;
    }
}
#endif
