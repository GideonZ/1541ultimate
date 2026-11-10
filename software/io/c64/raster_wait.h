#ifndef RASTER_WAIT_H
#define RASTER_WAIT_H

#include <stdint.h>

// True when the VIC raster counter, read as $D012 (bits 7-0) and $D011 (bit 7 is
// bit 8 of the line), is at line (lo | hi << 1). Both parts must match: waiting
// only until either one matches ends the wait at once on most lines.
// Read $D011 before $D012: read the other way round, the step from line 311 to
// line 0 (PAL) reads as $037 = line 55, and a wait for it would end at line 0.
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


// The wait in C64::resume(): returns true when the raster is at line (lo | hi << 1), false after
// 40 ms without it. c64.cc and the host test (target/pc/linux/rasterwait) both run this function;
// they differ only in the Bus they give it:
//   uint8_t d011();      read $D011
//   uint8_t d012();      read $D012
//   bool ms_elapsed();   true once per millisecond (the firmware restarts its 1 ms timer when it is)
template <class Bus>
static inline bool raster_wait_for_line(Bus &bus, uint8_t lo, uint8_t hi)
{
    int ms = 0;
    for (;;) {
        uint8_t d011 = bus.d011(); // before $D012, see raster_at_line
        if (raster_at_line(bus.d012(), d011, lo, hi)) {
            return true;
        }
        if (bus.ms_elapsed()) {
            if (++ms == 40) {
                return false;
            }
        }
    }
}

#endif
