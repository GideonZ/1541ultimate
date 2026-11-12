// Host test of the C64's whole-RAM copies (u64/u64_machine.cc: clear_ram, get_all_memory).
//
// The copies move all 64K through the C64 memory window, about 68 ms on the U64E, with the C64
// stopped. They must do it with the scheduler suspended (other tasks kept off the window and its
// mode register, interrupts served), not inside a critical section (interrupts off for the whole
// copy). The stubs record the bytes of C64 memory that move inside each critical section and each
// suspended stretch, and whether the C64 was stopped while the scheduler was suspended.
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

static bool machine_stopped;
static U64Machine *machine;

struct Section { int depth; std::vector<uint8_t> ram, buf; };
struct Stats {
    Section crit, susp;
    long crit_max_bytes, susp_max_bytes;  // most C64-memory bytes moved in one critical section / one suspended stretch
    long crit_sections, susp_sections;
    long stops, resumes, stops_inside, resumes_inside;
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
    if (b > st.crit_max_bytes) st.crit_max_bytes = b;
}
void sim_suspend(void) { open_section(st.susp); }
long sim_resume(void)
{
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
             "%ld stops, %ld resumes (%ld/%ld inside)", st.crit_sections, st.crit_max_bytes, st.susp_sections,
             st.susp_max_bytes, st.stops, st.resumes, st.stops_inside, st.resumes_inside);
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

// the whole copy ran with the scheduler suspended, none of it inside a critical section, the C64 was
// stopped and resumed outside the suspended stretch, and nothing was left open or set
static bool suspended_copy(void)
{
    return st.crit_max_bytes == 0 && st.susp_sections == 1 && st.susp_max_bytes >= 65536 - 4096 &&
           st.crit.depth == 0 && st.susp.depth == 0 && st.stops_inside == 0 && st.resumes_inside == 0 &&
           sim_memonly.v == 0;
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

    printf("c64mem: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
