/*
 * Host test of the raster wait in C64::resume() (io/c64/c64.cc). Leaving the menu,
 * resume() waits until the VIC raster reaches the line before the bad line the C64
 * was stopped on; the gateware then releases the CPU on the next bad line, which is
 * that same line. The test runs raster_wait_for_line() from raster_wait.h, the
 * function c64.cc calls, against a VIC whose raster advances one line every 50
 * register reads (a line is 63 us), for every bad line the C64 can stop on and
 * every line resume() can start on. The register reads straddle line changes as
 * they do on the device, so it also checks the read order.
 *
 * The Makefile builds the old code by editing a copy of raster_wait.h
 * (make legacy, legacy-d011, legacy-order); each of those must fail. `make test`
 * also checks that c64.cc calls raster_wait_for_line() and holds no loop of its own.
 */
#include <stdio.h>
#include <stdint.h>
#include "raster_wait.h"

// The Bus raster_wait_for_line() takes: a VIC whose raster advances between reads.
struct Vic {
    int lines, line, reads, since_ms;
    uint8_t d011()
    {
        step();
        return (uint8_t)(0x1B | ((line & 0x100) ? 0x80 : 0)); // bit 7 = raster bit 8
    }
    uint8_t d012()
    {
        step();
        return (uint8_t)line;
    }
    bool ms_elapsed()   // about 1 us per register read
    {
        if (++since_ms < 1000) return false;
        since_ms = 0;
        return true;
    }
    void step() { if (++reads % 50 == 0) line = (line + 1) % lines; }
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
                Vic v = { systems[s], start, 0, 0 };
                bool found = raster_wait_for_line(v, lo, hi);   // the production wait
                cases++;
                if (!found || (v.line != target)) {
                    if (failures < 5)
                        printf("FAIL %s stop line %d, resume starts at %d: wait ended at line %d, want %d\n",
                               s ? "NTSC" : "PAL", stop, start, v.line, target);
                    failures++;
                }
            }
        }
    }
    // Bad lines: the frame has them only if DEN ($D011 bit 4) is set during line $30.
    // resume() writes $D011 for the wait, waits, then restores the program's $D011;
    // the gateware releases the CPU on the next bad line, which must be the stop line.
    const uint8_t prog_d011 = 0x1B;   // DEN on, 25 rows, YSCROLL 3: bad lines where (line & 7) == 3
    int den_cases = 0, den_failures = 0;
    for (int s = 0; s < 2; s++) {
        for (int stop = 0x30; stop <= 0xF7; stop++) {
            if ((stop & 7) != (prog_d011 & 7)) continue;          // the C64 stops on bad lines only
            int target = stop - 1;
            for (int start = 0; start < systems[s]; start++) {
                uint8_t during = d011_while_waiting(prog_d011, 0);
                bool den_at_30 = true;                             // for the frame the release happens in
                int line = start;
                if (line == 0x30) den_at_30 = (during & 0x10) != 0;
                while (line != target) {                           // the wait (the condition is tested above)
                    line = (line + 1) % systems[s];
                    if (line == 0) den_at_30 = true;               // a new frame
                    if (line == 0x30) den_at_30 = (during & 0x10) != 0;
                }
                // restore the program's $D011, then release on the next bad line
                int released = -1;
                for (int l = line + 1, n = 0; n < 2 * systems[s]; l++, n++) {
                    int ll = l % systems[s];
                    if (ll == 0) den_at_30 = true;
                    if (ll == 0x30) den_at_30 = (prog_d011 & 0x10) != 0;
                    if (ll >= 0x30 && ll <= 0xF7 && (ll & 7) == (prog_d011 & 7) && den_at_30) { released = ll; n = (l - line); break; }
                }
                den_cases++;
                if (released != stop) {
                    if (den_failures < 3)
                        printf("FAIL %s stop line %d, resume starts at %d: released on line %d after a frame without bad lines\n",
                               s ? "NTSC" : "PAL", stop, start, released);
                    den_failures++;
                }
            }
        }
    }
    printf("raster_wait: DEN kept while waiting: %d cases, %d failures\n", den_cases, den_failures);
    failures += den_failures;
    printf("raster_wait: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
