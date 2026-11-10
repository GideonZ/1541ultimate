#ifndef RASTER_WAIT_H
#define RASTER_WAIT_H

#include <stdint.h>

// True when the VIC raster counter, read as $D012 (bits 7-0) and $D011 (bit 7 is
// bit 8 of the line), is at line (lo | hi << 1). Both parts must match: waiting
// only until either one matches ends the wait at once on most lines.
// The two halves have to come from the same line: see raster_read().
static inline bool raster_at_line(uint8_t d012, uint8_t d011, uint8_t lo, uint8_t hi)
{
    return (d012 == lo) && ((d011 & 0x80) == hi);
}

// $D011 while resume() waits with the compare moved to that line: only bit 7 (bit 8
// of the compare) changes. Writing the bare bit 8 also cleared DEN; if the wait then
// crosses line $30, that frame has no bad lines, and the cartridge gateware, which releases the
// CPU on a bad line, releases it a frame late, after a blank frame.
static inline uint8_t d011_while_waiting(uint8_t d011, uint8_t compare_hi)
{
    return (uint8_t)((d011 & 0x7F) | compare_hi);
}


// Bus is the VIC and the 1 ms timer as the code below touches them. c64.cc gives it the real
// registers, the host test (target/pc/linux/rasterwait) a model VIC:
//   uint8_t read(int reg);          read VIC register reg ($D000 + reg)
//   void write(int reg, uint8_t v); write it
//   bool ms_elapsed();              true once per millisecond (the firmware restarts its 1 ms timer)
#define RASTER_REG_D011 17
#define RASTER_REG_D012 18
#define RASTER_REG_IRQ 25
#define RASTER_REG_IRQ_EN 26

// The raster line as ($D012, $D011 bit 7). The line can change between the two reads: reading
// $D012 first and then $D011 turns the step 255 to 256 into $FF with bit 8 set (line 511), and
// $D011 first turns 256 to 255 into $00 with bit 8 clear (line 0), and so on at line 0 and at the
// wrap (311 PAL, 262 NTSC). Read $D011 on both sides of $D012 and start again if bit 7 changed;
// bit 8 cannot change twice in the three reads, so then $D012 belongs to that bit 8.
template <class Bus>
static inline void raster_read(Bus &bus, uint8_t &lo, uint8_t &hi)
{
    for (;;) {
        uint8_t before = bus.read(RASTER_REG_D011);
        lo = bus.read(RASTER_REG_D012);
        uint8_t after = bus.read(RASTER_REG_D011);
        if (!((before ^ after) & 0x80)) {
            hi = after & 0x80;
            return;
        }
    }
}

// Wait until the raster is at line (lo | hi << 1): true, or false after 40 ms without it.
template <class Bus>
static inline bool raster_wait_for_line(Bus &bus, uint8_t lo, uint8_t hi)
{
    int ms = 0;
    for (;;) {
        uint8_t cur_lo, cur_hi;
        raster_read(bus, cur_lo, cur_hi);
        if (raster_at_line(cur_lo, cur_hi, lo, hi)) {
            return true;
        }
        if (bus.ms_elapsed()) {
            if (++ms == 40) {
                return false;
            }
        }
    }
}

// What stop() saw and the program's VIC state, as resume() needs it.
struct RasterResume {
    uint8_t raster;      // $D012 when the C64 was stopped
    uint8_t raster_hi;   // bit 7 = line bit 8 then
    uint8_t vic_irq;     // $D019 then (bit 0: raster flag set)
    uint8_t vic_irq_en;  // $D01A then
    uint8_t vic_d011;    // the program's $D011
    uint8_t vic_d012;    // the program's raster compare
};

// The line to wait for, from the line the C64 was stopped on. Where the stop and the release
// are immediate (g_direct_dma in slot_server_v4.vhd: the Ultimate 64 and the C64 Ultimate) the C64
// goes on at the line the wait ends on, so wait for the stop line. Where the release is on the next bad
// line (slot_master_v4.vhd: the cartridges) wait for the line before the stop line.
static inline int raster_wait_target(uint8_t raster, uint8_t raster_hi, bool release_immediate)
{
    int line = raster | ((raster_hi & 0x80) ? 256 : 0);
    if (release_immediate || !line) {
        return line;
    }
    return line - 1;
}

// The VIC side of C64::resume() leaving the menu: move the compare to the wait line if the program
// had its raster flag set, wait for that line, put the program's compare, $D011 and interrupt
// enable back. c64.cc calls this, then releases the CPU; the host test runs the same function.
template <class Bus>
static inline bool raster_resume(Bus &bus, const RasterResume &s, bool release_immediate)
{
    int target = raster_wait_target(s.raster, s.raster_hi, release_immediate);
    uint8_t lo = (uint8_t)target;
    uint8_t hi = (target & 256) ? 0x80 : 0;
    bool irq = (s.vic_irq & 0x01) != 0; // was the raster flag set when we stopped? set it again
    if (irq) {
        bus.write(RASTER_REG_IRQ_EN, 0x81);
        bus.write(RASTER_REG_D012, lo);
        bus.write(RASTER_REG_D011, d011_while_waiting(s.vic_d011, hi));
    }
    bool found = raster_wait_for_line(bus, lo, hi);
    bus.write(RASTER_REG_D012, s.vic_d012);
    bus.write(RASTER_REG_D011, s.vic_d011);
    if (!irq) {
        bus.write(RASTER_REG_IRQ, 0x81); // clear raster irq
    }
    bus.write(RASTER_REG_IRQ_EN, s.vic_irq_en);
    return found;
}

#endif
