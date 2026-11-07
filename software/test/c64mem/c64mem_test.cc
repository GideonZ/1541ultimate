// Host test of the C64's whole-RAM copies (u64/u64_machine.cc: clear_ram, get_all_memory) and of
// the interrupt-safe window poke (u64/c64_window.h) used by U64Config::unlock_irq.
//
// The copies move all 64K through the C64 memory window, about 68 ms on the U64E. They must do it
// with the scheduler suspended (other tasks kept off the window and its mode register, interrupts
// served), not inside a critical section (interrupts off for the whole copy). The stubs record the
// bytes of C64 memory that move inside each critical section and each suspended stretch.
//
// With interrupts on during a copy, an interrupt handler can find C64_DMA_MEMONLY set by the task.
// c64_isr_poke_io must still reach the I/O register, leave the task's RAM alone and put the mode
// back, without reading I/O space (a read can acknowledge a CIA interrupt); that part runs against
// a window model with a posted write, as the hardware has.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "c64mem_sim.h"
#include "u64_machine.h"

// ---- simulated hardware and RTOS ---------------------------------------------------------------
SimMode sim_memonly;
uint8_t sim_serve;
uint8_t sim_ram[65536], sim_basic[8192], sim_kernal[8192], sim_charrom[4096];
uint8_t _raminit_bin_start[65536];   // the RAM image clear_ram writes (const where the driver declares it)

// u64_machine.cc defines it (c64_window.h). The weak one here only lets the test link against a
// u64_machine.cc that has no such flag (the base, for the RED run); the real definition wins.
__attribute__((weak)) volatile uint8_t c64_ram_copy_active;
static bool machine_stopped;
static U64Machine *machine;

struct Section { int depth; std::vector<uint8_t> ram, buf; };
struct Stats {
    Section crit, susp;
    long crit_max_bytes, susp_max_bytes;  // most C64-memory bytes moved in one critical section / one suspended stretch
    long crit_sections, susp_sections;
    long memonly_writes_outside;          // C64_DMA_MEMONLY changed with the scheduler running
    long stops, resumes, stops_inside, resumes_inside;
    int  flag_at_suspend, flag_at_resume; // c64_ram_copy_active when the scheduler is suspended / resumed
    std::string flag_seq;                 // its value at the end of each critical section inside the suspended stretch
} st;
static uint8_t *watched;                  // the caller's buffer, for get_all_memory

static long diff(const uint8_t *a, const std::vector<uint8_t> &b)
{
    long n = 0;
    for (size_t i = 0; i < b.size(); i++) n += (a[i] != b[i]);
    return n;
}
static void open_section(Section &s)
{
    if (s.depth++) return;
    s.ram.assign(sim_ram, sim_ram + 65536);
    if (watched) s.buf.assign(watched, watched + 65536); else s.buf.clear();
}
static long close_section(Section &s)
{
    if (--s.depth) return -1;
    return diff(sim_ram, s.ram) + (watched ? diff(watched, s.buf) : 0);
}
void sim_enter(void) { open_section(st.crit); }
void sim_exit(void)
{
    long b = close_section(st.crit);
    if (b < 0) return;
    st.crit_sections++;
    if (st.susp.depth) st.flag_seq += c64_ram_copy_active ? '1' : '0';
    if (b > st.crit_max_bytes) st.crit_max_bytes = b;
}
void sim_suspend(void) { if (!st.susp.depth) st.flag_at_suspend = c64_ram_copy_active; open_section(st.susp); }
long sim_resume(void)
{
    if (st.susp.depth == 1) st.flag_at_resume = c64_ram_copy_active;
    long b = close_section(st.susp);
    if (b < 0) return 0;
    st.susp_sections++;
    if (b > st.susp_max_bytes) st.susp_max_bytes = b;
    return 0;
}

void C64::stop(bool) { st.stops++; if (st.crit.depth || st.susp.depth) st.stops_inside++; machine_stopped = true; }
void C64::resume(void) { st.resumes++; if (st.crit.depth || st.susp.depth) st.resumes_inside++; machine_stopped = false; }
bool C64::is_stopped(void) { return machine_stopped; }
U64Machine *C64::make_u64(void) { return new U64Machine(); }
void C64::get_all_memory_of(U64Machine *m, uint8_t *pb) { m->get_all_memory(pb); }

// ---- window model for c64_window.h -----------------------------------------------------------------
// MEMONLY=1: the window reaches RAM everywhere. MEMONLY=0 (bus): $D000-$DFFF is I/O. A window write
// is posted: it lands, in the mode current at that moment, on the next window access, not when the
// mode register changes. That is why a write must be followed by a read before the mode changes.
static uint8_t io[65536];
static struct { bool pending; uint16_t addr; uint8_t val; } wbuf;
static int io_reads;                      // reads of I/O space: they can have side effects (CIA ICR, cart registers)
static int win_reads, win_writes;         // all window accesses
static void land(void)
{
    if (!wbuf.pending) return;
    bool to_io = !sim_memonly.v && wbuf.addr >= 0xD000 && wbuf.addr < 0xE000;
    (to_io ? io : sim_ram)[wbuf.addr] = wbuf.val;
    wbuf.pending = false;
}
static uint8_t win_read(uint16_t a)
{
    land();
    win_reads++;
    bool from_io = !sim_memonly.v && a >= 0xD000 && a < 0xE000;
    io_reads += from_io;
    return (from_io ? io : sim_ram)[a];
}
static void win_write(uint16_t a, uint8_t v) { land(); win_writes++; wbuf.pending = true; wbuf.addr = a; wbuf.val = v; }
#undef C64_PEEK                          // the stub c64.h's direct versions, used by u64_machine.cc
#undef C64_POKE
#define C64_PEEK(a)    win_read(a)
#define C64_POKE(a, v) win_write(a, v)
#include "u64.h"
#include "c64_window.h"

// ---- tests ------------------------------------------------------------------------------------
static int failures, passes;
static void check(bool ok, const char *name, const std::string &d)
{
    printf("%s %s - %s\n", ok ? "PASS" : "FAIL", name, d.c_str());
    if (ok) passes++; else failures++;
}

static std::string stats(void)
{
    char b[300];
    snprintf(b, sizeof b, "%ld critical sections (most bytes in one: %ld), %ld suspended stretches (most bytes: %ld); "
             "%ld stops, %ld resumes (%ld/%ld inside); copy flag %d at suspend, %d at resume, '%s' at the critical "
             "sections inside", st.crit_sections, st.crit_max_bytes, st.susp_sections, st.susp_max_bytes, st.stops,
             st.resumes, st.stops_inside, st.resumes_inside, st.flag_at_suspend, st.flag_at_resume, st.flag_seq.c_str());
    return b;
}

static void reset(bool stopped, bool frozen)
{
    st = Stats();
    machine_stopped = stopped;
    machine->isFrozen = frozen;
    sim_memonly.v = 0;
    watched = 0;
}

// the whole copy ran with the scheduler suspended, none of it inside a critical section, the copy
// flag went up and down with the mode inside the suspended stretch (two two-store critical sections),
// and nothing was left open or set
static bool suspended_copy(void)
{
    return st.crit_max_bytes == 0 && st.susp_sections == 1 && st.susp_max_bytes >= 65536 - 4096 &&
           st.crit.depth == 0 && st.susp.depth == 0 && st.stops_inside == 0 && st.resumes_inside == 0 &&
           sim_memonly.v == 0 && st.flag_at_suspend == 0 && st.flag_at_resume == 0 && st.flag_seq == "10" &&
           c64_ram_copy_active == 0;
}

int main()
{
    machine = C64::make_u64();
    uint8_t *init = _raminit_bin_start;
    for (int i = 0; i < 65536; i++) init[i] = (uint8_t)((i * 7 + 3) ^ (i >> 8));
    std::vector<uint8_t> pb(65536);

    // Clear Memory on a running machine: stopped, cleared, resumed
    reset(false, false);
    for (int i = 0; i < 65536; i++) sim_ram[i] = (uint8_t)~init[i];
    machine->clear_ram();
    check(memcmp(sim_ram, init, 65536) == 0 && !machine_stopped && st.stops == 1 && st.resumes == 1,
          "clear_ram_writes_all_64k_and_resumes", stats());
    check(suspended_copy(), "clear_ram_copies_with_the_scheduler_suspended_not_interrupts_off", stats());

    // saving memory from a running machine
    reset(false, false);
    for (int i = 0; i < 65536; i++) { sim_ram[i] = (uint8_t)(i ^ 0x5A ^ (i >> 8)); pb[i] = (uint8_t)~sim_ram[i]; }
    watched = pb.data();
    C64::get_all_memory_of(machine, pb.data());
    check(memcmp(pb.data(), sim_ram, 65536) == 0 && !machine_stopped && st.stops == 1 && st.resumes == 1,
          "get_all_memory_reads_all_64k_and_resumes", stats());
    check(suspended_copy(), "get_all_memory_copies_with_the_scheduler_suspended_not_interrupts_off", stats());

    // a machine another task holds stopped stays stopped
    reset(true, false);
    for (int i = 0; i < 65536; i++) pb[i] = (uint8_t)~sim_ram[i];
    watched = pb.data();
    C64::get_all_memory_of(machine, pb.data());
    check(memcmp(pb.data(), sim_ram, 65536) == 0 && machine_stopped && st.stops == 0 && st.resumes == 0 && suspended_copy(),
          "get_all_memory_leaves_a_stopped_machine_stopped", stats());

    // frozen (freezer menu): no stop or resume, and 1K-4K come from / go to the backups
    reset(true, true);
    for (int i = 0; i < 65536; i++) sim_ram[i] = (uint8_t)~init[i];
    machine->clear_ram();
    check(memcmp(sim_ram, init, 65536) == 0 && memcmp(machine->screen_backup, init + 1024, 1024) == 0 &&
          memcmp(machine->ram_backup, init + 2048, 2048) == 0 && st.stops == 0 && st.resumes == 0 && suspended_copy(),
          "clear_ram_frozen_fills_the_backups", stats());
    reset(true, true);
    for (int i = 0; i < 65536; i++) { sim_ram[i] = (uint8_t)(i * 5); pb[i] = (uint8_t)~sim_ram[i]; }
    memset(machine->screen_backup, 0x11, 1024);
    memset(machine->ram_backup, 0x22, 2048);
    watched = pb.data();
    C64::get_all_memory_of(machine, pb.data());
    bool frozen_ok = memcmp(pb.data(), sim_ram, 1024) == 0 && memcmp(pb.data() + 4096, sim_ram + 4096, 65536 - 4096) == 0;
    for (int i = 1024; i < 2048; i++) frozen_ok = frozen_ok && pb[i] == 0x11;
    for (int i = 2048; i < 4096; i++) frozen_ok = frozen_ok && pb[i] == 0x22;
    check(frozen_ok && st.stops == 0 && st.resumes == 0 && suspended_copy(), "get_all_memory_frozen_uses_the_backups", stats());

    // the unlock interrupt arrives while a task copies RAM in MEMONLY mode, right after the task's
    // window write to RAM $D038 (still posted)
    memset(io, 0x5F, sizeof io);
    memset(&wbuf, 0, sizeof wbuf);
    io_reads = 0;
    sim_ram[0xD038] = 0x00;
    sim_memonly.v = 1;
    c64_ram_copy_active = 1;
    win_write(0xD038, 0x77);                // the task's write, posted
    c64_isr_poke_io(0xD038, 0x00);          // the interrupt handler
    bool mode_back = sim_memonly.v == 1;
    bool isr_landed = !wbuf.pending;
    (void)win_read(0x1000);                 // the task carries on
    char d[200];
    int isr_io_reads = io_reads;
    snprintf(d, sizeof d, "I/O $D038 = %02x (want 00), RAM $D038 = %02x (want 77, the task's), mode after the handler %d "
             "(want 1), handler's write landed before return: %s, I/O reads %d (want 0)", io[0xD038], sim_ram[0xD038],
             mode_back, isr_landed ? "yes" : "no", isr_io_reads);
    check(io[0xD038] == 0x00 && sim_ram[0xD038] == 0x77 && mode_back && isr_landed && isr_io_reads == 0,
          "isr_poke_during_a_copy_reaches_io_and_leaves_the_tasks_ram_and_mode", d);
    sim_memonly.v = 0;
    c64_ram_copy_active = 0;

    // outside a copy it is exactly the old handler's single write: no window read, no mode access
    memset(io, 0x5F, sizeof io);
    memset(&wbuf, 0, sizeof wbuf);
    io_reads = win_reads = win_writes = 0;
    sim_memonly.writes = 0;
    sim_ram[0xD038] = 0x33;
    c64_isr_poke_io(0xD038, 0x00);
    int r_isr = win_reads, w_isr = win_writes;
    long m_isr = sim_memonly.writes;
    (void)win_read(0x1000);                 // the write lands on the next access
    snprintf(d, sizeof d, "window reads %d (want 0), writes %d (want 1), mode writes %ld (want 0), I/O $D038 = %02x",
             r_isr, w_isr, m_isr, io[0xD038]);
    check(r_isr == 0 && w_isr == 1 && m_isr == 0 && io[0xD038] == 0x00 && sim_ram[0xD038] == 0x33,
          "isr_poke_outside_a_copy_is_the_old_single_write", d);

    printf("c64mem: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
