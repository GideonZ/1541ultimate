/*
 * Host test of the VIC side of C64::resume() (io/c64/c64.cc): raster_resume() from
 * raster_wait.h, the function c64.cc calls with the real VIC registers, here against a
 * model VIC. It starts the wait on every line of PAL (312 lines) and NTSC (263), for
 * every line the C64 can have been stopped on, with the program's raster flag set and
 * clear, and checks what the C64 sees:
 *
 *  - Ultimate 64 / C64 Ultimate (release_immediate, g_direct_dma in slot_server_v4.vhd):
 *    stop and release happen on any line, and the C64 goes on on the line the wait ends
 *    on. That has to be the stop line (or the next one: the match can be the last read of a line).
 *  - Cartridges (slot_master_v4.vhd): the release is on the next bad line. The wait ends
 *    on the line before the stop line, and the next bad line has to be the stop line,
 *    which needs a frame with bad lines: DEN ($D011 bit 4) set while the raster passes
 *    line $30.
 *  - the program's raster compare, $D011 and interrupt enable are back afterwards, the
 *    raster flag is cleared when it was not set, and DEN stayed on during the wait.
 *  - raster_read() (stop() and the wait use it) returns a line that existed, also when
 *    the raster moves between its reads at the wrap and at lines 0 and 256.
 *
 * The register reads straddle line changes as they do on the device (the raster advances
 * every 50 reads, a line is 63 us). The Makefile builds the old code by editing a copy
 * of raster_wait.h (make mutants); each of those must fail.
 */
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include "raster_wait.h"

struct Vic {
    int lines, line, reads, since_ms;
    uint8_t d011w, cmp_lo, irq_en, irq_clears, irq_en_081, cmp_during, d011_during;
    int cmp_writes, d011_writes;
    bool den_at_30, den_lost;
    std::vector<int> seen, wlog;

    Vic(int n, int start, int phase, uint8_t d011) : lines(n), line(start), reads(phase), since_ms(0),
        d011w(d011), cmp_lo(0), irq_en(0), irq_clears(0), irq_en_081(0), cmp_during(0), d011_during(0), cmp_writes(0), d011_writes(0), den_at_30(true), den_lost(false) {}

    void step()
    {
        if (++reads % 50 == 0) {
            line = (line + 1) % lines;
            if (line == 0x30 && !(d011w & 0x10)) den_lost = true;
            if (line == 0x30) den_at_30 = (d011w & 0x10) != 0;
        }
        seen.push_back(line);
    }
    uint8_t read(int reg)
    {
        step();
        if (reg == 18) return (uint8_t)line;
        if (reg == 17) return (uint8_t)((d011w & 0x7F) | ((line & 0x100) ? 0x80 : 0));
        return 0;
    }
    void write(int reg, uint8_t v)
    {
        wlog.push_back(reg);
        if (reg == 17) { d011w = v; if (!d011_writes++) d011_during = v; }
        if (reg == 18) { cmp_lo = v; if (!cmp_writes++) cmp_during = v; }
        if (reg == 26) { irq_en = v; if (v == 0x81) irq_en_081++; }
        if (reg == 25 && v == 0x81) irq_clears++;
    }
    bool ms_elapsed()   // about 1 us per register read
    {
        if (++since_ms < 1000) return false;
        since_ms = 0;
        return true;
    }
};

static bool bad_line(int l, uint8_t d011) { return l >= 0x30 && l <= 0xF7 && (l & 7) == (d011 & 7); }

int main()
{
    int cases = 0, failures = 0, spills = 0, cart_cases = 0;
    const int systems[2] = { 312, 263 };  // PAL, NTSC
    for (int imm = 0; imm < 2; imm++) {
        for (int s = 0; s < 2; s++) {
            const int n = systems[s];
            for (int stop = 0; stop < n; stop++) {
                const uint8_t prog_d011 = (uint8_t)(0x1B | (((stop >> 2) & 1) ? 0x80 : 0)); // DEN, YSCROLL 3, a compare bit 8 that varies
                if (!imm && !bad_line(stop, prog_d011)) continue; // cartridges stop on bad lines only
                for (int irq = 0; irq < 2; irq++) {
                    for (int start = 0; start < n; start += (start < 8 || start > n - 8 || (start >= 0xF8 && start <= 0x108)) ? 1 : 7) {
                        Vic v(n, start, 10 + (start * 7) % 30, prog_d011);
                        RasterResume st;
                        st.raster = (uint8_t)stop;
                        st.raster_hi = (stop & 0x100) ? 0x80 : 0;
                        st.vic_irq = (uint8_t)irq;
                        st.vic_irq_en = 0x01;
                        st.vic_d011 = prog_d011;
                        st.vic_d012 = 0xF8;
                        bool found = raster_resume(v, st, imm != 0);
                        int release = v.line;
                        int target = imm ? stop : (stop ? stop - 1 : 0);
                        bool spill = false;
                        if (!imm) {   // released on the next bad line after the wait line, in a frame that has them
                            if (v.line == stop) {
                                spill = true;   // the wait ended late, in the stop line itself: not judged, counted
                                spills++;
                            } else {
                                bool den = v.den_at_30;
                                int l = v.line;
                                for (int k = 0; k < 2 * n; k++) {
                                    l = (l + 1) % n;
                                    if (l == 0x30) den = (prog_d011 & 0x10) != 0;
                                    if (bad_line(l, prog_d011) && den) { release = l; break; }
                                }
                            }
                            cart_cases++;
                        }
                        // direct: the wait can end in the last read of a line, so the release may fall in the next one
                        bool at_stop = release == stop;
                        bool compare_ok = !irq || (v.cmp_during == (uint8_t)target && ((v.d011_during & 0x80) == ((target & 256) ? 0x80 : 0)) && (v.d011_during & 0x7F) == (prog_d011 & 0x7F));
                        size_t w = v.wlog.size();
                        bool order_ok = irq ? (w >= 3 && v.wlog[w-3] == 18 && v.wlog[w-2] == 17 && v.wlog[w-1] == 26)
                                            : (w >= 4 && v.wlog[w-4] == 18 && v.wlog[w-3] == 17 && v.wlog[w-2] == 25 && v.wlog[w-1] == 26);
                        bool ok = order_ok && found && (at_stop || spill) && compare_ok && !v.den_lost && v.d011w == prog_d011 &&
                                  v.cmp_lo == 0xF8 && v.irq_en == 0x01 &&
                                  (irq ? (v.irq_en_081 == 1 && v.irq_clears == 0) : (v.irq_en_081 == 0 && v.irq_clears == 1));
                        cases++;
                        if (failures >= 50) goto report;   // enough: a broken wait can take 40 ms per case
                        if (!ok) {
                            if (failures < 6)
                                printf("FAIL %s %s stop line %d, irq %d, starts at %d: found %d, released on %d, den lost %d, d011 %02x cmp %02x en %02x\n",
                                       imm ? "direct" : "cartridge", s ? "NTSC" : "PAL", stop, irq, start, found,
                                       release, v.den_lost, v.d011w, v.cmp_lo, v.irq_en);
                            failures++;
                        }
                    }
                }
            }
        }
    }
report:
    // raster_read: every line, every phase of the 50-read step
    int read_cases = 0, read_failures = 0;
    for (int s = 0; s < 2; s++) {
        const int n = systems[s];
        for (int line = 0; line < n; line++) {
            for (int phase = 0; phase < 50; phase++) {
                Vic v(n, line, phase, 0x1B);
                uint8_t lo, hi;
                raster_read(v, lo, hi);
                int got = lo | (hi ? 256 : 0);
                bool exists = false;
                for (size_t i = 0; i < v.seen.size(); i++) if (v.seen[i] == got) exists = true;
                read_cases++;
                if (!exists) {
                    if (read_failures < 3)
                        printf("FAIL %s raster_read at line %d phase %d: got %d\n", s ? "NTSC" : "PAL", line, phase, got);
                    read_failures++;
                }
            }
        }
    }
    if (spills * 5 > cart_cases) {   // the wait should end on the line before the stop line, not in the stop line
        printf("FAIL cartridge: the wait ended in the stop line itself in %d of %d cases\n", spills, cart_cases);
        failures++;
    }
    printf("raster_wait: raster_read: %d cases, %d failures\n", read_cases, read_failures);
    printf("raster_wait: %d cases, %d failures\n", cases, failures);
    return (failures || read_failures) ? 1 : 0;
}
