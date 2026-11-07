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

#endif
