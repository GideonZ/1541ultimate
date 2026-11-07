/*
 * Host model of the raster wait in C64::resume() (io/c64/c64.cc). Leaving the
 * menu, resume() waits until the VIC raster reaches the line before the bad
 * line the C64 was stopped on; the gateware then releases the CPU on the next
 * bad line, which is that same line. The model runs the loop against a VIC
 * whose raster advances one line every 50 register reads (a line is 63 us),
 * for every bad line the C64 can stop on and every line resume() can start on.
 *
 * The register reads straddle line changes as they do on the device, so the
 * model also checks the read order (raster_wait.h).
 *
 * -DRASTER_WAIT_LEGACY builds the condition as it was before the fix; that
 * build must fail (make legacy).
 */
#include <stdio.h>
#include <stdint.h>
#include "raster_wait.h"

#ifdef RASTER_WAIT_LEGACY
// c64.cc before the fix: while ((d012 != lo) && ((d011 & 0x80) != hi)) { ... }
static bool wait_done(uint8_t d012, uint8_t d011, uint8_t lo, uint8_t hi)
{
    return !((d012 != lo) && ((d011 & 0x80) != hi));
}
#else
static bool wait_done(uint8_t d012, uint8_t d011, uint8_t lo, uint8_t hi)
{
    return raster_at_line(d012, d011, lo, hi);
}
#endif

struct Vic {
    int lines, line, reads;
    uint8_t read(int reg)
    {
        if (++reads % 50 == 0) line = (line + 1) % lines;
        if (reg == 18) return (uint8_t)line;
        return (uint8_t)(0x1B | ((line & 0x100) ? 0x80 : 0)); // $D011: bit 7 = raster bit 8
    }
};

int main()
{
    int cases = 0, failures = 0;
    const int systems[2] = { 312, 263 };  // PAL, NTSC
    for (int s = 0; s < 2; s++) {
        for (int stop = 0x30; stop <= 0xF7; stop++) {   // bad lines only occur here
            int target = stop - 1;                      // resume() waits for raster - 1
            uint8_t lo = (uint8_t)target, hi = 0;       // as resume() computes them
            for (int start = 0; start < systems[s]; start++) {
                Vic v = { systems[s], start, 0 };
                int budget = 40 * 1000;                 // 40 ms of reads at about 1 us each
                while (budget-- > 0) {
                    uint8_t d011 = v.read(17);   // the order resume() reads them in
                    uint8_t d012 = v.read(18);
                    if (wait_done(d012, d011, lo, hi)) break;
                }
                cases++;
                if ((v.line != target) || (budget <= 0)) {
                    if (failures < 5)
                        printf("FAIL %s stop line %d, resume starts at %d: wait ended at line %d, want %d\n",
                               s ? "NTSC" : "PAL", stop, start, v.line, target);
                    failures++;
                }
            }
        }
    }
    printf("raster_wait: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
