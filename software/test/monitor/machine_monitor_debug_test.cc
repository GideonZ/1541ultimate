// Host tests for Assembly Debug mode (DBG-MODE-001 ... DBG-TEST-001).
//
// These tests exercise the modal state, key routing, header flag composition,
// fixed CPU footer layout, breakpoint table, and patch cleanup paths without
// needing a real 6510 to run against. A stub DebugSession wired through a
// derived backend lets us verify Over/Trace/Out/Go dispatch and that cleanup
// is invoked on monitor close and debug-off.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine_monitor.h"
#include "monitor_breakpoints.h"
#include "monitor_debug.h"
#include "monitor_debug_session.h"
#include "monitor_debug_brk_session.h"
#include "monitor_debug_u64.h"
#include "monitor_file_io.h"
#include "monitor_debug_predictor.h"
#include "machine_monitor_test_support.h"

// Host stubs for monitor_io. The Debug-mode tests do not exercise file IO
// or the live `G` handoff, but MachineMonitor still pulls these symbols in.

static int g_swap_interface_type_calls = 0;

int swap_interface_type(UserInterface *ui)
{
    (void)ui;
    g_swap_interface_type_calls++;
    return MENU_HIDE;
}
namespace monitor_io {
bool pick_file(UserInterface *, const char *, char *, int, char *, int, bool) {
    return false;
}
const char *load_into_memory(const char *, const char *, MemoryBackend *,
                             uint16_t, bool, uint32_t, bool, uint32_t,
                             uint16_t *, uint32_t *) {
    return "NOT SUPPORTED";
}
const char *save_from_memory(UserInterface *, const char *, const char *,
                             MemoryBackend *, uint16_t, uint16_t) {
    return "NOT SUPPORTED";
}
void jump_to(uint16_t) { }
void resume_to_context(const DebugContext &) { }
}

namespace {

struct StubDebugSession : public DebugSession
{
    int over_calls;
    int over_at_calls;
    int over_breakpoint_calls;
    int over_at_breakpoint_calls;
    int trace_calls;
    int trace_at_calls;
    int out_calls;
    int out_breakpoint_calls;
    int go_calls;
    int run_to_calls;
    int cleanup_calls;
    int cleanup_to_context_calls;
    int snapshot_calls;
    int claim_calls;
    int refresh_calls;
    int release_calls;
    uint16_t last_start_pc;
    uint16_t last_run_to_target;
    DebugContext cleanup_target_ctx;
    DebugContext next_ctx;
    bool predict_bytes_valid;
    uint8_t predict_bytes[3];
    Result over_result;
    Result trace_result;
    Result out_result;
    Result go_result;
    Result run_to_result;
    Result snapshot_result;
    bool claim_allowed;

    StubDebugSession()
        : over_calls(0), over_at_calls(0),
          over_breakpoint_calls(0), over_at_breakpoint_calls(0),
          trace_calls(0), trace_at_calls(0),
          out_calls(0), out_breakpoint_calls(0),
          go_calls(0), run_to_calls(0), cleanup_calls(0),
          cleanup_to_context_calls(0),
          snapshot_calls(0), claim_calls(0),
          refresh_calls(0), release_calls(0),
          last_start_pc(0), last_run_to_target(0),
          predict_bytes_valid(false),
          over_result(DBG_OK), trace_result(DBG_OK),
          out_result(DBG_OK), go_result(DBG_OK), run_to_result(DBG_OK),
          snapshot_result(DBG_OK), claim_allowed(true)
    {
        debug_context_reset(&cleanup_target_ctx);
        debug_context_reset(&next_ctx);
        next_ctx.valid = true;
        next_ctx.pc = 0xC003;
        next_ctx.sp = 0xF7;
        next_ctx.a = 0x01;
        next_ctx.x = 0x00;
        next_ctx.y = 0xFF;
        next_ctx.sr = 0x24;
        next_ctx.irq_valid = true;
        next_ctx.irq_vec = 0xC123;
        next_ctx.nmi_valid = true;
        next_ctx.nmi_vec = 0xEA31;
        predict_bytes[0] = 0;
        predict_bytes[1] = 0;
        predict_bytes[2] = 0;
    }

    virtual Result snapshot(DebugContext *ctx) {
        snapshot_calls++;
        if (snapshot_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return snapshot_result;
    }
    virtual Result over(const DebugContext &, const DebugPredictResult &, DebugContext *ctx) {
        over_calls++;
        if (over_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return over_result;
    }
    virtual Result over(const DebugContext &from, const DebugPredictResult &pred,
                        const MonitorBreakpoints *, DebugContext *ctx) {
        over_breakpoint_calls++;
        return over(from, pred, ctx);
    }
    virtual Result over_at(uint16_t start_pc, const DebugPredictResult &, DebugContext *ctx) {
        over_at_calls++;
        last_start_pc = start_pc;
        if (over_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return over_result;
    }
    virtual Result over_at(uint16_t start_pc, const DebugPredictResult &pred,
                           const MonitorBreakpoints *, DebugContext *ctx) {
        over_at_breakpoint_calls++;
        return over_at(start_pc, pred, ctx);
    }
    virtual Result trace(const DebugContext &, const DebugPredictResult &, DebugContext *ctx) {
        trace_calls++;
        if (trace_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return trace_result;
    }
    virtual Result trace_at(uint16_t start_pc, const DebugPredictResult &, DebugContext *ctx) {
        trace_at_calls++;
        last_start_pc = start_pc;
        if (trace_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return trace_result;
    }
    virtual Result step_out(const DebugContext &, DebugContext *ctx) {
        out_calls++;
        if (out_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return out_result;
    }
    virtual Result step_out(const DebugContext &from,
                            const MonitorBreakpoints *, DebugContext *ctx) {
        out_breakpoint_calls++;
        return step_out(from, ctx);
    }
    virtual Result go(const DebugContext &, const MonitorBreakpoints *, uint16_t) {
        go_calls++;
        // Model the real session's semantic: Go invalidates the previous
        // capture. If a stop happens during this Go, a real session would
        // refresh `next_ctx`; the stub simulates "no stop happened" by
        // marking snapshot unavailable until the next over/trace/out call
        // explicitly captures something.
        if (snapshot_result == DBG_OK) {
            snapshot_result = DBG_NOT_SUPPORTED;
        }
        return go_result;
    }
    virtual Result run_to(const DebugContext &, uint16_t target_pc,
                          const MonitorBreakpoints *, uint16_t start_pc,
                          DebugContext *ctx) {
        run_to_calls++;
        last_run_to_target = target_pc;
        last_start_pc = start_pc;
        if (run_to_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return run_to_result;
    }
    virtual void cleanup(void) {
        cleanup_calls++;
    }
    virtual void cleanup_to_context(const DebugContext *ctx) {
        cleanup_calls++;
        cleanup_to_context_calls++;
        if (ctx) {
            cleanup_target_ctx = *ctx;
        } else {
            debug_context_reset(&cleanup_target_ctx);
        }
    }
    virtual bool read_step_bytes(uint16_t, uint8_t *dst, uint8_t len) {
        if (!predict_bytes_valid || !dst || len == 0) {
            return false;
        }
        for (uint8_t i = 0; i < len && i < 3; i++) {
            dst[i] = predict_bytes[i];
        }
        return true;
    }
    virtual bool claim_debug_ownership(bool) {
        claim_calls++;
        return claim_allowed;
    }
    virtual void refresh_debug_ownership(void) {
        refresh_calls++;
    }
    virtual void release_debug_ownership(void) {
        release_calls++;
    }
};

// Singleton stub session shared with FakeDebugBackend so tests can both
// install canned outputs ahead of time and assert call counts afterwards.
struct DebugBackendController
{
    StubDebugSession session;
    bool refuse_session;
    int session_creations;

    DebugBackendController() : refuse_session(false), session_creations(0) {}
};

struct FakeDebugBackend : public FakeMemoryBackend
{
    DebugBackendController *ctrl;
    FakeDebugBackend(DebugBackendController *c) : ctrl(c) {}

    virtual DebugSession *create_debug_session(void) {
        if (!ctrl || ctrl->refuse_session) {
            return NULL;
        }
        ctrl->session_creations++;
        // Return a fresh instance so MachineMonitor's `delete debug_session`
        // is safe; copy the controller's canned state into it.
        StubDebugSession *s = new StubDebugSession();
        *s = ctrl->session;
        // Reset counters owned by this instance (we keep the controller's
        // session as the truth for asserting after destruction would lose
        // them, so we forward call counts on cleanup via an out param).
        return s;
    }
};

// Like FakeDebugBackend but exposes the session so tests can introspect.
// MachineMonitor owns the lifetime, so we keep a raw pointer via the
// controller and update its `last_session` whenever a new session is created.
struct TrackingDebugBackend : public FakeMemoryBackend
{
    StubDebugSession *last_session;
    int session_creations;
    bool refuse_session;
    int reset_calls;
    int nmi_pulses;
    bool allow_reset;
    DebugContext canned_snapshot;
    bool canned_snapshot_set;
    DebugSession::Result snapshot_result;
    bool session_claim_allowed;

    TrackingDebugBackend() : last_session(NULL), session_creations(0),
                             refuse_session(false), reset_calls(0),
                             allow_reset(true), canned_snapshot_set(false),
                             snapshot_result(DebugSession::DBG_OK),
                             session_claim_allowed(true)
    {
        debug_context_reset(&canned_snapshot);
    }

    virtual DebugSession *create_debug_session(void) {
        if (refuse_session) {
            return NULL;
        }
        session_creations++;
        last_session = new StubDebugSession();
        if (canned_snapshot_set) {
            last_session->next_ctx = canned_snapshot;
        }
        last_session->snapshot_result = snapshot_result;
        last_session->claim_allowed = session_claim_allowed;
        return last_session;
    }

    virtual bool supports_reset(void) const { return allow_reset; }

    virtual bool reset_machine(void) {
        if (!allow_reset) {
            return false;
        }
        reset_calls++;
        return true;
    }
};

struct SourceLabelDebugBackend : public TrackingDebugBackend
{
    virtual const char *source_name(uint16_t address) const {
        switch (address) {
            case 0x1000: return "RAM";
            case 0x1001: return "BASIC";
            case 0x1002: return "KERNAL";
            case 0x1003: return "CHAR";
            case 0x1004: return "IO";
            default: return "RAM";
        }
    }
};

// Direct render-target regression harness for the BRK debug engine.
//
// FakeFreezeMachine implements the BrkDebugSession hardware hooks over a flat
// 64K RAM array plus a `frozen` flag that mirrors C64::isFrozen. It models the
// freeze-mode invariant the production fix enforces: a debug step temporarily
// unfreezes the machine to run the live CPU, then MUST re-freeze before control
// returns so the monitor keeps rendering into the firmware/menu screen instead
// of the live C64 screen RAM. The reserved sentinel byte is set on the first
// delay() call to simulate the BRK trampoline firing.
namespace {

static const uint16_t FAKE_SENTINEL_ADDR = 0x03F7; // mirrors SENTINEL_ADDR
static const uint16_t FAKE_SCREEN_BASE   = 0x0400; // C64 screen RAM aperture
static const uint16_t FAKE_STORE_Y       = 0x03F0;
static const uint16_t FAKE_STORE_X       = 0x03F1;
static const uint16_t FAKE_STORE_A       = 0x03F2;
static const uint16_t FAKE_STORE_SR      = 0x03F3;
static const uint16_t FAKE_STORE_PCLO    = 0x03F4;
static const uint16_t FAKE_STORE_PCHI    = 0x03F5;
static const uint16_t FAKE_STORE_SP      = 0x03F6;
static const uint16_t FAKE_SPIN_LO       = 0x0378;
static const uint16_t FAKE_SPIN_HI       = 0x0379;
static const uint16_t FAKE_RESUME_TRAMP  = 0x033C;
static const uint16_t FAKE_NMI_VECTOR_HI = 0x0319; // mirrors NMI_VECTOR_HI
static const uint16_t FAKE_NMI_TRAMP     = 0x03A0; // mirrors NMI_TRAMPOLINE_ADDR

class FakeFreezeMachine : public BrkDebugSession
{
public:
    enum { MAX_RECORDED_BRK_PATCHES = 32 };

    uint8_t ram[65536];
    bool frozen;
    bool accessible_when_unfrozen;
    bool break_on_unfrozen_unfreeze_attempt;
    int unfreeze_attempts;
    int unfreeze_calls;
    int refreeze_calls;
    int reset_calls;
    int nmi_pulses;
    int brk_patch_writes;
    uint16_t last_brk_patch_addr;
    uint16_t brk_patch_addrs[MAX_RECORDED_BRK_PATCHES];
    // When false, delay_ms() does NOT raise the sentinel, so wait_for_sentinel
    // runs to its full timeout. Lets a test force the DBG_TIMEOUT path and then
    // re-arm to prove the run-window state (depth, freeze bracketing) recovered.
    bool sentinel_armed;
    // Test hook: model the freeze-mode "gap free-run" race. When set, the live
    // CPU is treated as having reached the already-installed step BRK while the
    // controlled NMI launch is still being set up, so the sentinel goes high
    // mid-setup (as the NMI vector high byte is written). A correct launch must
    // clear that stale sentinel before releasing the CPU.
    bool stale_sentinel_during_nmi_setup;
    bool nmi_from_spin_times_out;
    bool capture_override_armed;
    uint16_t capture_override_pc;
    uint8_t capture_override_sp;
    uint8_t capture_override_a;
    uint8_t capture_override_x;
    uint8_t capture_override_y;
    uint8_t capture_override_sr;

    FakeFreezeMachine(bool start_frozen)
        : frozen(start_frozen), accessible_when_unfrozen(false),
          break_on_unfrozen_unfreeze_attempt(false), unfreeze_attempts(0),
          unfreeze_calls(0), refreeze_calls(0), reset_calls(0), nmi_pulses(0),
          brk_patch_writes(0), last_brk_patch_addr(0),
          sentinel_armed(true), stale_sentinel_during_nmi_setup(false),
          nmi_from_spin_times_out(false),
          capture_override_armed(false),
          capture_override_pc(0), capture_override_sp(0),
          capture_override_a(0), capture_override_x(0), capture_override_y(0),
          capture_override_sr(0)
    {
        memset(ram, 0, sizeof(ram));
        memset(brk_patch_addrs, 0, sizeof(brk_patch_addrs));
    }

    // Leaf cleanup while the fake hooks are still live (the abstract base
    // destructor intentionally does not call cleanup()).
    virtual ~FakeFreezeMachine() { cleanup(); }

    // Stamp a recognizable BASIC-like pattern into the C64 screen RAM so a test
    // can prove monitor redraws never reach it.
    void stamp_screen_sentinel(uint8_t value, int len)
    {
        for (int i = 0; i < len; i++) {
            ram[FAKE_SCREEN_BASE + i] = value;
        }
    }
    bool screen_sentinel_intact(uint8_t value, int len) const
    {
        for (int i = 0; i < len; i++) {
            if (ram[FAKE_SCREEN_BASE + i] != value) {
                return false;
            }
        }
        return true;
    }

    void arm_capture_context(uint16_t pc, uint8_t sp, uint8_t a,
                             uint8_t x, uint8_t y, uint8_t sr)
    {
        capture_override_armed = true;
        capture_override_pc = pc;
        capture_override_sp = sp;
        capture_override_a = a;
        capture_override_x = x;
        capture_override_y = y;
        capture_override_sr = sr;
    }

protected:
    virtual bool backend_ready(void) const { return true; }
    virtual uint8_t current_cpu_port(void) const { return 0x07; }
    virtual bool begin_stopped_session(void) { return false; }
    virtual void end_stopped_session(bool) { }
    virtual uint8_t peek_cpu(uint16_t a, uint8_t) { return ram[a]; }
    virtual void poke_cpu(uint16_t a, uint8_t b, uint8_t)
    {
        if (b == 0x00) {
            if (brk_patch_writes < MAX_RECORDED_BRK_PATCHES) {
                brk_patch_addrs[brk_patch_writes] = a;
            }
            brk_patch_writes++;
            last_brk_patch_addr = a;
        }
        ram[a] = b;
    }
    virtual uint8_t peek_visible(uint16_t a) { return ram[a]; }
    virtual void poke_visible(uint16_t a, uint8_t b)
    {
        ram[a] = b;
        // nmi_redirect_to() writes the NMI vector while the CPU is stopped, just
        // before releasing it. Fire the modelled gap free-run BRK hit here so a
        // correct launch must still clear the sentinel afterwards.
        if (stale_sentinel_during_nmi_setup && a == FAKE_NMI_VECTOR_HI) {
            ram[FAKE_SENTINEL_ADDR] = 0xFF;
        }
    }
    virtual void unfreeze_if_accessible(void)
    {
        bool accessible = frozen || accessible_when_unfrozen;
        if (!accessible) {
            return;
        }
        unfreeze_attempts++;
        if (frozen) {
            frozen = false;
            unfreeze_calls++;
        } else if (break_on_unfrozen_unfreeze_attempt) {
            sentinel_armed = false;
        }
    }
    virtual bool machine_is_frozen(void) const { return frozen; }
    virtual void refreeze_machine(void)
    {
        if (!frozen) {
            frozen = true;
            refreeze_calls++;
        }
    }
    virtual bool reset_machine(void) { reset_calls++; return true; }
    virtual void pulse_nmi_and_release(bool)
    {
        nmi_pulses++;
        if (nmi_from_spin_times_out &&
            nmi_pulses > 1 &&
            ram[FAKE_SPIN_LO] == 0x77 && ram[FAKE_SPIN_HI] == 0x03) {
            sentinel_armed = false;
        }
    }
    virtual void delay_ms(int)
    {
        // Model the BRK trampoline reaching the spin loop: the sentinel becomes
        // non-zero so the next wait_for_sentinel poll returns DBG_OK. When the
        // test disarms the sentinel, leave it clear so the engine runs to its
        // DBG_TIMEOUT.
        if (sentinel_armed) {
            if (capture_override_armed) {
                ram[FAKE_STORE_Y] = capture_override_y;
                ram[FAKE_STORE_X] = capture_override_x;
                ram[FAKE_STORE_A] = capture_override_a;
                ram[FAKE_STORE_SR] = capture_override_sr;
                ram[FAKE_STORE_PCLO] = (uint8_t)((capture_override_pc + 2) & 0xFF);
                ram[FAKE_STORE_PCHI] = (uint8_t)((capture_override_pc + 2) >> 8);
                ram[FAKE_STORE_SP] = capture_override_sp;
                capture_override_armed = false;
            }
            ram[FAKE_SENTINEL_ADDR] = 0xFF;
        }
    }
};

class FakeVisibleRomMachine : public FakeFreezeMachine
{
public:
    uint8_t basic_rom[8192];
    uint8_t kernal_rom[8192];
    uint8_t char_rom[4096];
    uint8_t cpu_port;
    bool allow_visible_rom_patching;
    bool force_raw_peek_brk;
    uint16_t force_raw_peek_brk_addr;
    int rom_patch_writes;
    uint16_t last_rom_patch_addr;

    FakeVisibleRomMachine(bool start_frozen)
        : FakeFreezeMachine(start_frozen), cpu_port(0x07),
          allow_visible_rom_patching(false), force_raw_peek_brk(false),
          force_raw_peek_brk_addr(0), rom_patch_writes(0), last_rom_patch_addr(0)
    {
        memset(basic_rom, 0, sizeof(basic_rom));
        memset(kernal_rom, 0, sizeof(kernal_rom));
        memset(char_rom, 0, sizeof(char_rom));
    }

protected:
    volatile uint8_t *rom_patch_ptr(uint16_t addr, uint8_t patch_cpu_port)
    {
        patch_cpu_port &= 0x07;
        if (addr >= 0xA000 && addr <= 0xBFFF &&
                ((patch_cpu_port & 0x03) == 0x03)) {
            return &basic_rom[addr - 0xA000];
        }
        if (addr >= 0xD000 && addr <= 0xDFFF &&
                ((patch_cpu_port & 0x03) != 0x00) &&
                ((patch_cpu_port & 0x04) == 0x00)) {
            return &char_rom[addr - 0xD000];
        }
        if (addr >= 0xE000 && (patch_cpu_port & 0x02)) {
            return &kernal_rom[addr - 0xE000];
        }
        return 0;
    }

    virtual uint8_t current_cpu_port(void) const { return cpu_port; }
    virtual bool supports_visible_rom_patching(void) const
    {
        return allow_visible_rom_patching;
    }
    virtual uint8_t peek_cpu(uint16_t a, uint8_t patch_cpu_port)
    {
        patch_cpu_port &= 0x07;
        if (force_raw_peek_brk && a == force_raw_peek_brk_addr) {
            return 0x00;
        }
        if (a >= 0xA000 && a <= 0xBFFF && ((patch_cpu_port & 0x03) == 0x03)) {
            return basic_rom[a - 0xA000];
        }
        if (a >= 0xD000 && a <= 0xDFFF &&
                ((patch_cpu_port & 0x03) != 0x00) &&
                ((patch_cpu_port & 0x04) == 0x00)) {
            return char_rom[a - 0xD000];
        }
        if (a >= 0xE000 && (patch_cpu_port & 0x02)) {
            return kernal_rom[a - 0xE000];
        }
        return ram[a];
    }
    virtual void poke_cpu(uint16_t a, uint8_t b, uint8_t)
    {
        ram[a] = b;
    }
    virtual uint8_t read_patch_byte(uint16_t a, uint8_t patch_cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(a, patch_cpu_port);
        if (rom && allow_visible_rom_patching) {
            return *rom;
        }
        return peek_cpu(a, patch_cpu_port);
    }
    virtual void write_patch_byte(uint16_t a, uint8_t b, uint8_t patch_cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(a, patch_cpu_port);
        if (rom && allow_visible_rom_patching) {
            if (b == 0x00) {
                if (brk_patch_writes < MAX_RECORDED_BRK_PATCHES) {
                    brk_patch_addrs[brk_patch_writes] = a;
                }
                brk_patch_writes++;
                last_brk_patch_addr = a;
            }
            *rom = b;
            rom_patch_writes++;
            last_rom_patch_addr = a;
            return;
        }
        poke_cpu(a, b, patch_cpu_port);
    }
};

class FakeCpuPortBackend : public MemoryBackend
{
public:
    uint8_t live_cpu_port;
    int live_reads;

    FakeCpuPortBackend()
        : live_cpu_port(0x05), live_reads(0)
    {
    }

    virtual uint8_t read(uint16_t address)
    {
        if (address == 0x0001) {
            live_reads++;
            return live_cpu_port;
        }
        return 0;
    }
    virtual void write(uint16_t, uint8_t) { }
};

static void fake_seed_nop_run(FakeFreezeMachine &m, uint16_t addr);
static void fake_seed_captured_context(FakeFreezeMachine &m, uint16_t pc,
                                       uint8_t sp, uint8_t a,
                                       uint8_t x, uint8_t y, uint8_t sr);

class BrkSessionBackend : public FakeMemoryBackend
{
public:
    FakeFreezeMachine *last_session;
    bool start_frozen;

    BrkSessionBackend(bool frozen = false)
        : last_session(NULL), start_frozen(frozen)
    {
    }

    virtual DebugSession *create_debug_session(void)
    {
        last_session = new FakeFreezeMachine(start_frozen);
        fake_seed_nop_run(*last_session, 0x0801);
        fake_seed_captured_context(*last_session, 0x0802, 0xFE, 0x11, 0x22, 0x33, 0x20);
        return last_session;
    }
};

// Build a one-byte NOP "program" at addr so the predictor classifies it as a
// linear step and install_brk_at has a non-zero byte to overwrite.
static void fake_seed_nop_run(FakeFreezeMachine &m, uint16_t addr)
{
    m.ram[addr] = 0xEA;     // NOP at PC
    m.ram[addr + 1] = 0xEA; // fall-through target (BRK lands here; must be != 0)
    m.ram[addr + 2] = 0xEA;
}

static void fake_seed_rom_nop_run(FakeVisibleRomMachine &m, uint16_t addr)
{
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        m.basic_rom[addr - 0xA000] = 0xEA;
        m.basic_rom[addr - 0xA000 + 1] = 0xEA;
        m.basic_rom[addr - 0xA000 + 2] = 0xEA;
        return;
    }
    if (addr >= 0xE000) {
        m.kernal_rom[addr - 0xE000] = 0xEA;
        m.kernal_rom[addr - 0xE000 + 1] = 0xEA;
        m.kernal_rom[addr - 0xE000 + 2] = 0xEA;
    }
}

static void fake_seed_captured_context(FakeFreezeMachine &m, uint16_t pc,
                                       uint8_t sp, uint8_t a,
                                       uint8_t x, uint8_t y, uint8_t sr)
{
    m.ram[FAKE_STORE_Y] = y;
    m.ram[FAKE_STORE_X] = x;
    m.ram[FAKE_STORE_A] = a;
    m.ram[FAKE_STORE_SR] = sr;
    m.ram[FAKE_STORE_PCLO] = (uint8_t)((pc + 2) & 0xFF);
    m.ram[FAKE_STORE_PCHI] = (uint8_t)((pc + 2) >> 8);
    m.ram[FAKE_STORE_SP] = sp;
}

static bool fake_recorded_brk_patch(const FakeFreezeMachine &m, uint16_t addr)
{
    int n = m.brk_patch_writes;
    if (n > FakeFreezeMachine::MAX_RECORDED_BRK_PATCHES) {
        n = FakeFreezeMachine::MAX_RECORDED_BRK_PATCHES;
    }
    for (int i = 0; i < n; i++) {
        if (m.brk_patch_addrs[i] == addr) {
            return true;
        }
    }
    return false;
}

static bool fake_nmi_trampoline_stores_cpu_port(const FakeFreezeMachine &m,
                                                uint8_t cpu_port)
{
    for (int i = 0; i <= 20; i++) {
        if (m.ram[FAKE_NMI_TRAMP + i] == 0xA9 &&
                m.ram[FAKE_NMI_TRAMP + i + 1] == (uint8_t)(cpu_port & 0x07) &&
                m.ram[FAKE_NMI_TRAMP + i + 2] == 0x85 &&
                m.ram[FAKE_NMI_TRAMP + i + 3] == 0x01) {
            return true;
        }
    }
    return false;
}

} // namespace

static int trim_end(const char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\0')) n--;
    return n;
}

static int expect_help_token_not_accented(CaptureScreen &screen, const char *needle,
                                          const char *message)
{
    char row[40];
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        const char *at = strstr(row, needle);
        if (at) {
            int x = 1 + (int)(at - row);
            int n = (int)strlen(needle);
            for (int i = 0; i < n; i++) {
                if (screen.colors[y][x + i] == 1) {
                    return expect(false, message);
                }
            }
            return 0;
        }
    }
    return expect(false, message);
}

static int find_row_with_address(CaptureScreen &screen, const char *address)
{
    char row[40];

    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, address, 4) == 0) {
            return y;
        }
    }
    return -1;
}

static int expect_breakpoint_label_only_accent(CaptureScreen &screen, int row_y,
                                               const char *label,
                                               uint8_t normal_color,
                                               uint8_t accent_color,
                                               const char *message)
{
    char row[40];
    screen.get_slice(1, row_y, 38, row);
    const char *at = strstr(row, label);
    if (expect(at != NULL, message)) return 1;

    int label_start = (int)(at - row);
    int label_len = (int)strlen(label);
    for (int i = 0; i < 38; i++) {
        bool label_cell = (i >= label_start && i < label_start + label_len);
        uint8_t expected = label_cell ? accent_color : normal_color;
        if (screen.colors[row_y][1 + i] != expected) {
            char detail[160];
            sprintf(detail, "%s color at row column %d", message, i);
            return expect(false, detail);
        }
    }
    return 0;
}

static int test_predictor_classifies_control_flow()
{
    DebugPredictResult p;

    uint8_t jsr[3] = { 0x20, 0x34, 0x12 };
    debug_predict(0x1000, jsr, false, &p);
    if (expect(p.kind == DBG_PREDICT_JSR, "JSR should classify as JSR")) return 1;
    if (expect(p.length == 3, "JSR length should be 3")) return 1;
    if (expect(p.fall_through == 0x1003, "JSR fall-through should be PC+3")) return 1;
    if (expect(p.has_target && p.branch_target == 0x1234, "JSR target should decode")) return 1;

    uint8_t jmp[3] = { 0x4C, 0x34, 0x12 };
    debug_predict(0x1800, jmp, false, &p);
    if (expect(p.kind == DBG_PREDICT_JMP_ABS, "JMP should classify as absolute jump")) return 1;
    if (expect(p.has_target && p.branch_target == 0x1234, "JMP target should decode")) return 1;

    uint8_t branch[2] = { 0xD0, 0x02 }; // BNE +2
    debug_predict(0x2000, branch, false, &p);
    if (expect(p.kind == DBG_PREDICT_BRANCH, "BNE should classify as BRANCH")) return 1;
    if (expect(p.fall_through == 0x2002, "BNE fall-through should be PC+2")) return 1;
    if (expect(p.has_target && p.branch_target == 0x2004, "BNE target should decode")) return 1;

    uint8_t brk_bytes[1] = { 0x00 };
    debug_predict(0x3000, brk_bytes, false, &p);
    if (expect(p.kind == DBG_PREDICT_BRK, "BRK should classify as BRK")) return 1;

    uint8_t rts[1] = { 0x60 };
    debug_predict(0x4000, rts, false, &p);
    if (expect(p.kind == DBG_PREDICT_RTS, "RTS should classify as RTS")) return 1;

    uint8_t linear[2] = { 0xA9, 0x42 }; // LDA #$42
    debug_predict(0x5000, linear, false, &p);
    if (expect(p.kind == DBG_PREDICT_LINEAR, "LDA imm should classify as LINEAR")) return 1;
    if (expect(p.length == 2 && p.fall_through == 0x5002, "LDA imm length/fall-through")) return 1;

    return 0;
}

static int test_breakpoint_slots_allocate_clear_and_format()
{
    MonitorBreakpoints bps;
    if (expect(bps.find_at(0xC000) < 0, "Empty table must not find $C000")) return 1;
    int s0 = bps.allocate(0xC000, 0x07);
    if (expect(s0 == 0, "First allocation must use slot 0")) return 1;
    int s1 = bps.allocate(0xC100, 0x07);
    if (expect(s1 == 1, "Second allocation must use slot 1")) return 1;
    int dup = bps.allocate(0xC000, 0x07);
    if (expect(dup == 0, "Allocating an existing address returns its slot")) return 1;

    for (int i = 2; i < MONITOR_BREAKPOINT_SLOT_COUNT; i++) {
        bps.allocate((uint16_t)(0xD000 + i), 0x07);
    }
    int full = bps.allocate(0xE000, 0x07);
    if (expect(full < 0, "Allocation must fail when all slots are used")) return 1;

    char row[40];
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strstr(row, "0 SET $C000") == row, "Slot 0 popup row should start with state and address")) return 1;
    bps.set_enabled(0, false);
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strstr(row, "0 OFF $C000") == row, "Disabled slot should show OFF")) return 1;
    bps.set_label(0, "readc");
    if (expect(strcmp(bps.get(0)->label, "READ") == 0,
               "Breakpoint labels must normalize to four uppercase chars")) return 1;
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strstr(row, "0 OFF READ $C000") == row,
               "Breakpoint popup row should include an assigned label")) return 1;

    bps.clear_slot(0);
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strcmp(row, "0 EMPTY") == 0, "Cleared slot must format as EMPTY")) return 1;
    return 0;
}

static int test_footer_layout_blanks_unknown_fields()
{
    DebugContext ctx;
    debug_context_reset(&ctx);
    char header[40];
    char values[40];
    MonitorDebug::format_footer_header(header, sizeof(header));
    MonitorDebug::format_footer_values(ctx, values, sizeof(values));

    if (expect(strncmp(header + 0, "PC", 2) == 0, "PC label position")) return 1;
    if (expect(strncmp(header + 5, "AC", 2) == 0, "AC label position")) return 1;
    if (expect(strncmp(header + 8, "XR", 2) == 0, "XR label position")) return 1;
    if (expect(strncmp(header + 11, "YR", 2) == 0, "YR label position")) return 1;
    if (expect(strncmp(header + 14, "SP", 2) == 0, "SP label position")) return 1;
    if (expect(strncmp(header + 17, "NV-BDIZC", 8) == 0, "Flags label position")) return 1;
    if (expect(strncmp(header + 26, "IRQ", 3) == 0, "IRQ label position")) return 1;
    if (expect(strncmp(header + 31, "NMI", 3) == 0, "NMI label position")) return 1;

    // Unknown-value row must contain only spaces in the 35-column footer area
    // (no zeroes, no question marks, no placeholder text).
    for (int i = 0; i < 35; i++) {
        if (expect(values[i] == ' ', "Unknown footer value must be a blank")) return 1;
    }
    if (expect(strchr(values, '?') == NULL, "Unknown footer must not show '?'")) return 1;
    if (expect(strstr(values, "??") == NULL, "Unknown footer must not show question marks")) return 1;

    ctx.valid = true;
    ctx.pc = 0xC003;
    ctx.sp = 0xF7;
    ctx.a = 0x01;
    ctx.x = 0x00;
    ctx.y = 0xFF;
    ctx.sr = 0x24;
    ctx.irq_valid = true;
    ctx.irq_vec = 0xC123;
    ctx.nmi_valid = true;
    ctx.nmi_vec = 0xEA31;
    MonitorDebug::format_footer_values(ctx, values, sizeof(values));
    if (expect(strncmp(values + 0, "C003", 4) == 0, "PC hex must align under label")) return 1;
    if (expect(strncmp(values + 5, "01", 2) == 0, "AC hex must align under label")) return 1;
    if (expect(strncmp(values + 8, "00", 2) == 0, "XR hex must align under label")) return 1;
    if (expect(strncmp(values + 11, "FF", 2) == 0, "YR hex must align under label")) return 1;
    if (expect(strncmp(values + 14, "F7", 2) == 0, "SP hex must align under label")) return 1;
    if (expect(strncmp(values + 17, "00100100", 8) == 0, "Flags must render as NV-BDIZC binary string")) return 1;
    if (expect(strncmp(values + 26, "C123", 4) == 0, "IRQ vector hex position")) return 1;
    if (expect(strncmp(values + 31, "EA31", 4) == 0, "NMI vector hex position")) return 1;

    ctx.nmi_valid = false;
    MonitorDebug::format_footer_values(ctx, values, sizeof(values));
    for (int i = 31; i < 35; i++) {
        if (expect(values[i] == ' ', "NMI must blank when context says vector is unknown")) return 1;
    }
    return 0;
}

static int test_debug_footer_sits_above_normal_status()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Switch ASM ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;

    char values[40];
    char row[40];
    int label_y = -1;
    int status_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strstr(row, "PC") != NULL && strstr(row, "NV-BDIZC") != NULL) {
            label_y = y;
        }
        if (strstr(row, "CPU") != NULL && strstr(row, "VIC") != NULL) {
            status_y = y;
        }
    }
    if (expect(label_y >= 0, "Debug label row must be visible")) return 1;
    if (expect(status_y >= 0, "Normal CPU/VIC status footer must remain visible in Debug mode")) return 1;
    if (expect(status_y == label_y + 2,
               "Debug labels/values must occupy the two rows immediately above normal status")) return 1;
    screen.get_slice(1, label_y + 1, 38, values);
    bool values_blank = true;
    for (int i = 0; i < 35; i++) {
        if (values[i] != ' ') {
            values_blank = false;
            break;
        }
    }
    if (expect(values_blank, "Unknown Debug value row must be directly above normal status")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_d_enters_debug_without_executing()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Switch to ASM view should succeed")) return 1;
    int before_creations = backend.session_creations;
    int before_calls = backend.last_session ? backend.last_session->over_calls : 0;
    if (expect(monitor.poll(0) == 0, "D should enter Debug mode and not exit")) return 1;
    if (expect(backend.last_session != NULL || backend.session_creations >= before_creations,
               "Entering Debug may create a session for snapshot, but must not call Over")) return 1;
    if (expect(backend.last_session == NULL || backend.last_session->over_calls == before_calls,
               "D entry must not execute (Over count unchanged)")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM") != NULL,
               "Header must still read MONITOR ASM, not MONITOR DBG")) return 1;
    if (expect(strstr(header, "Dbg") != NULL,
               "Dbg flag must appear in the header status area")) return 1;
    if (expect(monitor.poll(0) == 0, "ESC inside Debug must not exit the monitor")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL,
               "Dbg flag must clear after leaving Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    if (expect(backend.last_session == NULL || backend.session_creations > 0,
               "Sanity check: session lifecycle ran")) return 1;
    return 0;
}

static int test_d_inside_debug_performs_over()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // The session's canned PC is $C003; populate a benign LDA #$00 there so
    // the predictor classifies it as LINEAR rather than BRK.
    backend.write(0xC003, 0xA9);
    backend.write(0xC004, 0x00);
    backend.write(0xC005, 0xEA);

    const int keys[] = { 'A', 'D', 'D', KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 5; i++) {
        int r = monitor.poll(0);
        if (i < 4) {
            if (expect(r == 0, "Non-exit polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP should exit")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->over_calls >= 1, "D inside Debug should call Over")) return 1;
    if (expect(backend.last_session->over_breakpoint_calls >= 1,
               "D inside Debug must pass active breakpoints to Over")) return 1;
    monitor.deinit();
    return 0;
}

static int test_d_inside_debug_without_context_overs_from_cursor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x1000, 0xA9);
    backend.write(0x1001, 0x77);
    backend.write(0x1002, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "No-context Over setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->over_calls == 0,
               "No-context D must not call context-based Over")) return 1;
    if (expect(backend.last_session->over_at_calls == 1,
               "No-context D in Debug must execute Over from the cursor")) return 1;
    if (expect(backend.last_session->over_at_breakpoint_calls == 1,
               "No-context D must pass active breakpoints to cursor Over")) return 1;
    if (expect(backend.last_session->last_start_pc == 0x1000,
               "No-context Over must start at the Assembly cursor address")) return 1;
    if (expect(ui.popup_count == 0 || strstr(ui.last_popup, "NO CONTEXT") == NULL,
               "No-context Over must not show NO CONTEXT")) return 1;
    monitor.deinit();
    return 0;
}

static int test_d_inside_debug_without_context_goto_keeps_cursor_authoritative()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x1000, 0xA9);
    backend.write(0x1001, 0x11);
    backend.write(0x1002, 0xEA);
    backend.write(0x2000, 0xA9);
    backend.write(0x2001, 0x22);
    backend.write(0x2002, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'J', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);
    ui.set_prompt("2000", 3);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        int r = monitor.poll(0);
        if (i < 6) {
            if (expect(r == 0, "No-context Debug goto polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->over_calls == 0,
               "No-context Debug goto must not fall back to captured context")) return 1;
    if (expect(backend.last_session->over_at_calls == 1,
               "No-context Debug goto must execute Over from cursor")) return 1;
    if (expect(backend.last_session->last_start_pc == 0x2000,
               "No-context Debug goto must keep the new cursor address authoritative")) return 1;
    monitor.deinit();
    return 0;
}

static int test_d_refuses_undocumented_opcode_with_specific_warning()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x1000, 0x1A);
    backend.write(0x1001, 0xEA);

    const int keys[] = { 'J', 'A', 'U', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        int r = monitor.poll(0);
        if (i < 6) {
            if (expect(r == 0, "Undocumented opcode refusal polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(ui.popup_count > 0 && strcmp(ui.last_popup, "UNSUPPORTED OPCODE") == 0,
               "Undocumented opcode refusal must use UNSUPPORTED OPCODE")) return 1;
    monitor.deinit();
    return 0;
}

static int test_debug_predictor_uses_session_bytes_over_backend_reads()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0xA000, 0x00);
    backend.write(0xA001, 0x00);
    backend.write(0xA002, 0x00);

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Goto A000 should stay in monitor")) return 1;
    if (expect(monitor.poll(0) == 0, "Switch to ASM should stay in monitor")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug should stay in monitor")) return 1;

    if (expect(backend.last_session != NULL, "Debug session must exist before stepping")) return 1;
    backend.last_session->predict_bytes_valid = true;
    backend.last_session->predict_bytes[0] = 0xEA;
    backend.last_session->predict_bytes[1] = 0xEA;
    backend.last_session->predict_bytes[2] = 0xEA;

    if (expect(monitor.poll(0) == 0,
               "Debug over at A000 should use session bytes instead of unsafe backend BRK bytes")) return 1;
    if (expect((backend.last_session->over_calls + backend.last_session->over_at_calls) == 1,
               "Debug over should dispatch through the session when session bytes decode safely")) return 1;
    if (expect(ui.popup_count == 0 || strstr(ui.last_popup, "UNSAFE TARGET") == NULL,
               "Valid ROM step bytes must not surface UNSAFE TARGET")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_t_traces_and_o_outs_inside_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0xC003, 0xA9);
    backend.write(0xC004, 0x00);
    backend.write(0xC005, 0xEA);

    const int keys[] = { 'A', 'D', 'T', 'O', KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "Non-exit polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP should exit")) return 1;
        }
    }
    if (expect(backend.last_session->trace_calls == 1, "T should call trace exactly once")) return 1;
    if (expect(backend.last_session->out_calls == 1, "O should call step-out exactly once")) return 1;
    if (expect(backend.last_session->over_calls == 0, "Neither T nor O should call Over")) return 1;
    monitor.deinit();
    return 0;
}

static int test_o_without_traced_jsr_reports_not_in_subroutine()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_OK;
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x2003;
    backend.canned_snapshot.sp = 0xFA;
    backend.canned_snapshot.sr = 0x24;

    const int keys[] = { 'A', 'D', 'O', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(backend.last_session != NULL, "Debug session must exist")) return 1;
    backend.last_session->out_result = DebugSession::DBG_NOT_IN_SUBROUTINE;
    if (expect(monitor.poll(0) == 0, "Step Out refusal stays in monitor")) return 1;
    if (expect(strstr(ui.last_popup, "NOT IN SUBROUTINE") != NULL,
               "Step Out without a traced JSR must say NOT IN SUBROUTINE")) return 1;
    if (expect(strstr(ui.last_popup, "UNSAFE TARGET") == NULL,
               "Step Out without a traced JSR must not say UNSAFE TARGET")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_o_without_context_reports_not_in_subroutine()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'A', 'D', 'O', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Step Out without context stays in monitor")) return 1;
    if (expect(strstr(ui.last_popup, "NOT IN SUBROUTINE") != NULL,
               "Step Out without context must say NOT IN SUBROUTINE")) return 1;
    if (expect(strstr(ui.last_popup, "NO CONTEXT") == NULL,
               "Step Out without context must not say NO CONTEXT")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_t_inside_debug_without_context_traces_from_cursor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x2000, 0x20);
    backend.write(0x2001, 0x00);
    backend.write(0x2002, 0x30);

    const int keys[] = { 'J', 'A', 'D', 'T', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("2000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "No-context Trace setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->trace_calls == 0,
               "No-context T must not call context-based Trace")) return 1;
    if (expect(backend.last_session->trace_at_calls == 1,
               "No-context T in Debug must execute Trace from the cursor")) return 1;
    if (expect(backend.last_session->last_start_pc == 0x2000,
               "No-context Trace must start at the Assembly cursor address")) return 1;
    monitor.deinit();
    return 0;
}

static int test_g_invalidates_context()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'G', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Switch ASM ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "G performs Go and stays in monitor")) return 1;
    if (expect(backend.last_session != NULL && backend.last_session->go_calls == 1,
               "G should call go() exactly once")) return 1;
    char values[40];
    char row[40];
    int label_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strstr(row, "PC") != NULL && strstr(row, "NV-BDIZC") != NULL) {
            label_y = y;
            break;
        }
    }
    if (expect(label_y >= 0, "G invalidation test must find Debug footer labels")) return 1;
    screen.get_slice(1, label_y + 1, 38, values);
    // After G the captured context is invalidated: values row should be blanks.
    bool all_blank = true;
    for (int i = 0; i < 35; i++) if (values[i] != ' ') { all_blank = false; break; }
    if (expect(all_blank, "G must invalidate the captured context (footer blank)")) return 1;
    monitor.deinit();
    return 0;
}

static int test_return_remains_non_executing_navigation()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // Set up a JSR at the cursor so RETURN can follow it without executing.
    backend.write(0x1000, 0x20);
    backend.write(0x1001, 0x00);
    backend.write(0x1002, 0x20);

    const int keys[] = { 'J', 'A', 'D', KEY_RETURN, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "Pre-exit polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP should exit")) return 1;
        }
    }
    if (expect(backend.last_session == NULL ||
               (backend.last_session->over_calls == 0 &&
                backend.last_session->trace_calls == 0 &&
                backend.last_session->out_calls == 0 &&
                backend.last_session->go_calls == 0),
               "RETURN must not invoke any execution path")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_d_leaves_debug_only()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_CTRL_D, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 4; i++) {
        int r = monitor.poll(0);
        if (i < 3) {
            if (expect(r == 0, "C=+D should leave Debug but stay in monitor")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL, "C=+D must clear the Dbg flag")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_d_leaves_debug_but_keeps_edit()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'E', KEY_CTRL_D, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.color_fg = 12;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit ok")) return 1;
    if (expect(monitor.poll(0) == 0, "C=+D should leave Debug only")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL && strstr(header, "Edit") != NULL,
               "C=+D in Debug+Edit must clear Dbg and keep Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "ESC leaves remaining Edit")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_escape_leaves_edit_before_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'E', KEY_ESCAPE, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit ok")) return 1;
    if (expect(monitor.poll(0) == 0, "First ESC should leave Edit only")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL && strstr(header, "Edit") == NULL,
               "ESC in Debug+Edit must leave Dbg active while clearing Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "Second ESC should leave Debug")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL && strstr(header, "Edit") == NULL,
               "Second ESC must clear the remaining Dbg flag")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_runstop_leaves_edit_before_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'E', KEY_BREAK, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit ok")) return 1;
    if (expect(monitor.poll(0) == 0, "First RUN/STOP should leave Edit only")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL && strstr(header, "Edit") == NULL,
               "RUN/STOP in Debug+Edit must leave Dbg active while clearing Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "Second RUN/STOP should leave Debug")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL && strstr(header, "Edit") == NULL,
               "Second RUN/STOP must clear the remaining Dbg flag")) return 1;
    if (expect(monitor.poll(0) == 1, "Third RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_step_from_memory_view_recenters_asm_on_debug_pc()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xC100;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.a = 0x01;
    backend.canned_snapshot.x = 0x00;
    backend.canned_snapshot.y = 0xFF;
    backend.canned_snapshot.sr = 0x24;
    backend.canned_snapshot.irq_valid = true;
    backend.canned_snapshot.irq_vec = 0xC123;
    backend.canned_snapshot.nmi_valid = true;
    backend.canned_snapshot.nmi_vec = 0xEA31;

    const int keys[] = { 'A', 'D', 'M', KEY_RIGHT, 'D', 'A', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 8);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Switch to memory view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Move away from debug address ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug step from memory view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Return to ASM view ok")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $C100") != NULL,
               "Returning to ASM in Debug must snap back to the current debug PC")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_x_resets_and_keeps_debug_open()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x0801, 0xEA);

    const int keys[] = { 'A', 'D', KEY_CTRL_X, 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "CTRL+X reset ok")) return 1;
    if (expect(backend.reset_calls == 1,
               "CTRL+X must issue one backend reset while the monitor stays open")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL,
               "CTRL+X in Debug must keep Debug mode active")) return 1;
    char values[40];
    char row[40];
    int label_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strstr(row, "PC") != NULL && strstr(row, "NV-BDIZC") != NULL) {
            label_y = y;
            break;
        }
    }
    if (expect(label_y >= 0, "Debug footer label row must remain visible after reset")) return 1;
    screen.get_slice(1, label_y + 1, 38, values);
    bool values_blank = true;
    for (int i = 0; i < 35; i++) {
        if (values[i] != ' ') {
            values_blank = false;
            break;
        }
    }
    if (expect(values_blank,
               "CTRL+X reset in Debug must clear the stale register footer")) return 1;
    if (expect(monitor.poll(0) == 0, "Stepping after reset should still work")) return 1;
    if (expect(backend.session_creations == 2,
               "Stepping after reset must recreate the debug session")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_x_local_reset_exits_monitor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x0801, 0xEA);

    const int keys[] = { 'A', 'D', KEY_CTRL_X };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == MENU_EXIT,
               "Local UI CTRL+X reset must exit the monitor so host ownership is released")) return 1;
    if (expect(backend.reset_calls == 1,
               "Local UI CTRL+X reset must issue one backend reset")) return 1;
    if (expect(monitor.consume_reopen_after_reset(),
               "Local UI CTRL+X reset while monitor is open must request monitor re-entry")) return 1;
    monitor.deinit();

    const int reentry_keys[] = { KEY_BREAK, KEY_BREAK };
    FakeKeyboard reentry_keyboard(reentry_keys, 2);
    ui.keyboard = &reentry_keyboard;
    BackendMachineMonitor reentered(&ui, &backend);
    reentered.set_reset_exits_monitor(true);
    reentered.init(&screen, &reentry_keyboard);
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $0000") != NULL,
               "Reset re-entry must restore the monitor view and address")) return 1;
    if (expect(strstr(header, "Dbg") != NULL,
               "Reset re-entry must restore Debug mode when reset happened in Debug")) return 1;
    if (expect(reentered.poll(0) == 0, "RUN/STOP leaves restored Debug")) return 1;
    if (expect(reentered.poll(0) == 1, "Second RUN/STOP exits restored monitor")) return 1;
    reentered.deinit();
    return 0;
}

static int test_ctrl_x_reenters_monitor_without_debug_when_not_debugging()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', KEY_CTRL_X };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == MENU_EXIT,
               "Local UI CTRL+X reset must exit the current monitor instance")) return 1;
    if (expect(monitor.consume_reopen_after_reset(),
               "Local UI CTRL+X reset outside Debug must request monitor re-entry")) return 1;
    monitor.deinit();

    const int reentry_keys[] = { KEY_BREAK };
    FakeKeyboard reentry_keyboard(reentry_keys, 1);
    ui.keyboard = &reentry_keyboard;
    BackendMachineMonitor reentered(&ui, &backend);
    reentered.set_reset_exits_monitor(true);
    reentered.init(&screen, &reentry_keyboard);
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $0000") != NULL,
               "Reset re-entry outside Debug must restore the monitor view and address")) return 1;
    if (expect(strstr(header, "Dbg") == NULL,
               "Reset re-entry outside Debug must not enter Debug mode")) return 1;
    if (expect(reentered.poll(0) == 1, "RUN/STOP exits restored non-Debug monitor")) return 1;
    reentered.deinit();
    return 0;
}

static int test_breakpoints_survive_ctrl_x_reset_reentry()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xC000, 0xEA);

    // J $A000 -> A (asm) -> D (debug on) -> R (BP at A000) -> C=+X (reset).
    const int keys[] = { 'J', 'A', 'D', 'R', KEY_CTRL_X };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 4; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint setup before reset")) return 1;
    }
    if (expect(monitor.poll(0) == MENU_EXIT,
               "C=+X must exit the current monitor instance so it can re-enter")) return 1;
    if (expect(monitor.consume_reopen_after_reset(),
               "C=+X must request a monitor re-entry")) return 1;
    monitor.deinit();

    // Re-enter the monitor. The reset-reentry path constructs a fresh
    // MachineMonitor; breakpoints must come back from process-lifetime storage.
    const int reentry_keys[] = { 'A', KEY_BREAK, KEY_BREAK };
    FakeKeyboard reentry_keyboard(reentry_keys, 3);
    ui.keyboard = &reentry_keyboard;
    BackendMachineMonitor reentered(&ui, &backend);
    reentered.set_reset_exits_monitor(true);
    reentered.init(&screen, &reentry_keyboard);
    if (expect(reentered.poll(0) == 0, "Switch back to Assembly view after reset")) return 1;

    char row[40];
    int target_row = find_row_with_address(screen, "A000");
    if (expect(target_row >= 0, "Re-entered monitor must show $A000 row")) return 1;
    screen.get_slice(1, target_row, 38, row);
    if (expect(strstr(row, "[BRK0]") != NULL,
               "Breakpoint stored before C=+X must reappear after reset reentry")) return 1;

    if (expect(reentered.poll(0) == 0, "RUN/STOP leaves restored Debug")) return 1;
    if (expect(reentered.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    reentered.deinit();
    return 0;
}

static int test_breakpoints_survive_normal_close_reopen()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xA000, 0xEA);

    // J $A000 -> A -> D -> R -> BREAK (leave Debug) -> BREAK (exit monitor).
    const int keys[] = { 'J', 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 5; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint setup before close")) return 1;
    }
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP closes the monitor")) return 1;
    monitor.deinit();

    // Reopen later with no reset in between. The persisted table must come
    // back so the user does not have to re-create their breakpoints.
    const int reopen_keys[] = { KEY_BREAK };
    FakeKeyboard reopen_keyboard(reopen_keys, 1);
    ui.keyboard = &reopen_keyboard;
    BackendMachineMonitor reopened(&ui, &backend);
    reopened.init(&screen, &reopen_keyboard);

    char row[40];
    int target_row = find_row_with_address(screen, "A000");
    if (expect(target_row >= 0, "Reopened monitor must show $A000 row")) return 1;
    screen.get_slice(1, target_row, 38, row);
    if (expect(strstr(row, "[BRK0]") != NULL,
               "Breakpoint must survive a normal close+reopen")) return 1;
    if (expect(reopened.poll(0) == 1, "RUN/STOP exits the reopened monitor")) return 1;
    reopened.deinit();
    return 0;
}

static int test_monitor_reset_saved_state_clears_breakpoints()
{
    // Seed the persisted table with a breakpoint by opening + closing once.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        TrackingDebugBackend backend;
        monitor_reset_saved_state();
        backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
        backend.write(0xA000, 0xEA);

        const int keys[] = { 'J', 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
        FakeKeyboard keyboard(keys, 6);
        ui.screen = &screen;
        ui.keyboard = &keyboard;
        ui.set_prompt("A000", 1);

        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);
        for (int i = 0; i < 5; i++) {
            if (expect(monitor.poll(0) == 0, "Seed monitor setup")) return 1;
        }
        if (expect(monitor.poll(0) == 1, "Seed monitor exit")) return 1;
        monitor.deinit();
    }

    // monitor_reset_saved_state() simulates a fresh process boot in tests; the
    // persisted breakpoint table must be empty after it runs.
    monitor_reset_saved_state();

    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xA000, 0xEA);

    const int keys[] = { 'J', 'A', KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump to A000")) return 1;
    if (expect(monitor.poll(0) == 0, "Switch to Assembly")) return 1;

    char row[40];
    int target_row = find_row_with_address(screen, "A000");
    if (expect(target_row >= 0, "Fresh-process monitor must show $A000 row")) return 1;
    screen.get_slice(1, target_row, 38, row);
    if (expect(strstr(row, "[BRK") == NULL,
               "monitor_reset_saved_state must clear the persisted breakpoint table")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the fresh monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_toggle_via_r()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // RUN/STOP first leaves Debug, then a second RUN/STOP exits the monitor.
    const int keys[] = { 'A', 'D', 'R', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    int popups_before = ui.popup_count;
    if (expect(monitor.poll(0) == 0, "R toggles breakpoint and stays in monitor")) return 1;
    if (expect(ui.popup_count == popups_before, "Toggle-on must not show a success popup")) return 1;
    if (expect(monitor.poll(0) == 0, "Second R clears the same address")) return 1;
    if (expect(ui.popup_count == popups_before, "Toggle-off must not show a success popup")) return 1;
    if (expect(monitor.poll(0) == 0, "First RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_popup_store_reuses_selected_slot()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0x1000, 0xEA);
    backend.write(0x1100, 0xEA);

    const int keys[] = {
        'J', 'A', 'D', 'R',
        'J', KEY_CTRL_R, 'S', KEY_ESCAPE, KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 10);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("1000", 1);
    ui.push_prompt("1100", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint store setup should stay in monitor")) return 1;
    }

    char row[42];
    bool found = false;
    for (int y = 0; y < 25 && !found; y++) {
        screen.get_slice(0, y, 40, row);
        if (strstr(row, "0 SET $1100") != NULL) {
            found = true;
        }
    }
    if (expect(found, "S must store the current address into the selected slot")) return 1;

    if (expect(monitor.poll(0) == 0, "ESC closes the breakpoint popup")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_popup_digit_jumps_to_slot()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0x1000, 0xEA);
    backend.write(0x1100, 0xEA);
    backend.write(0x1200, 0xEA);

    const int keys[] = {
        'J', 'A', 'D', 'R',
        'J', 'R',
        'J', KEY_CTRL_R, '1',
        KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 11);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("1000", 1);
    ui.push_prompt("1100", 1);
    ui.push_prompt("1200", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 9; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint digit-jump setup should stay in monitor")) return 1;
    }

    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "$1100") != NULL,
               "Digit shortcut in breakpoint popup must jump directly to that slot")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_row_indicator_and_color()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xC000, 0xAD);
    backend.write(0xC001, 0x34);
    backend.write(0xC002, 0x12);

    const int keys[] = { 'J', 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("C000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 4; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint visual setup should stay in monitor")) return 1;
    }

    char row[40];
    screen.get_slice(1, 4, 38, row);
    if (expect(strstr(row, "C000") != NULL, "Breakpoint row should show the target address")) return 1;
    if (expect(strstr(row, "LDA $1234") != NULL,
               "Breakpoint row should show mnemonic and operand text")) return 1;
    if (expect(strstr(row, "[BRK0][RAM]") != NULL,
               "Breakpoint row must show [BRKx] before normalized source")) return 1;
    if (expect(strstr(row, "[RAM]") != NULL,
               "Assembly source indicator must use the 3-character RAM label")) return 1;

    // Enabled breakpoint + Debug active: only the complete breakpoint label is
    // accented; address, bytes, mnemonic, source, and row fill remain normal.
    if (expect_breakpoint_label_only_accent(screen, 4, "[BRK0]",
                                           ui.color_fg, 1,
                                           "Enabled breakpoint should accent only its label")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    screen.get_slice(1, 4, 38, row);
    if (expect(strstr(row, "[BRK0][RAM]") != NULL,
               "Row still shows the stored breakpoint after Debug exits")) return 1;
    // Debug off: every breakpoint row reverts to the regular foreground color.
    for (int x = 1; x <= 38; x++) {
        if (expect(screen.colors[4][x] == ui.color_fg,
                   "Breakpoint row must use color_fg once Debug mode is off")) return 1;
    }
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_disabled_breakpoint_row_uses_regular_color()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xA000, 0xEA);

    // J A000 -> A (asm) -> D (debug on) -> R (toggle BP on) -> C=+R (open popup)
    // -> E (disable selected slot) -> ESC (close popup) -> BREAK -> BREAK
    const int keys[] = {
        'J', 'A', 'D', 'R', KEY_CTRL_R, 'E', KEY_ESCAPE, KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 9);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        if (expect(monitor.poll(0) == 0, "Disabled-BP setup should stay in monitor")) return 1;
    }

    char row[40];
    screen.get_slice(1, 4, 38, row);
    if (expect(strstr(row, "[BRK0][BAS]") != NULL,
               "Disabled breakpoint still shows [BRKx] on the opcode row")) return 1;
    // Disabled breakpoint, even while Debug mode is still active, must stay
    // in the regular foreground color so the user sees it as quiet.
    for (int x = 1; x <= 38; x++) {
        if (expect(screen.colors[4][x] == ui.color_fg,
                   "Disabled breakpoint row must render in color_fg, not accent")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_label_replaces_row_indicator()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xC000, 0xEA);

    const int keys[] = {
        'J', 'A', 'D', 'R', KEY_CTRL_R, 'L', KEY_ESCAPE, KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 9);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("C000", 1);
    ui.push_prompt("readc", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint label setup should stay in monitor")) return 1;
    }

    char popup_row[42];
    get_popup_line(screen, 2, popup_row, sizeof(popup_row));
    if (expect(strstr(popup_row, "0 SET READ $C000") == popup_row,
               "Breakpoint popup row must show the assigned label")) return 1;

    if (expect(monitor.poll(0) == 0, "ESC closes labelled breakpoint popup")) return 1;
    char row[40];
    screen.get_slice(1, 4, 38, row);
    if (expect(strstr(row, "[READ][RAM]") != NULL,
               "Assembly row must show the breakpoint label instead of [BRKx]")) return 1;
    if (expect(strstr(row, "[BRK0]") == NULL,
               "Labelled breakpoint row must not also show [BRK0]")) return 1;
    if (expect_breakpoint_label_only_accent(screen, 4, "[READ]",
                                           ui.color_fg, 1,
                                           "Labelled breakpoint should accent the whole bracketed label only")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_source_indicators_are_three_chars()
{
    TestUserInterface ui;
    CaptureScreen screen;
    SourceLabelDebugBackend backend;
    monitor_reset_saved_state();

    for (int i = 0; i < 5; i++) {
        backend.write((uint16_t)(0x1000 + i), 0xEA);
    }

    const int keys[] = { 'J', 'A', KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Goto source label area")) return 1;
    if (expect(monitor.poll(0) == 0, "Switch to ASM view")) return 1;

    const char *expected[] = { "[RAM]", "[BAS]", "[KRN]", "[CHR]", "[I/O]" };
    for (int y = 0; y < 5; y++) {
        char row[40];
        screen.get_slice(1, 4 + y, 38, row);
        if (expect(strstr(row, expected[y]) != NULL,
                   "Assembly row must normalize memory source indicators")) return 1;
        if (expect(strstr(row, "BASIC") == NULL && strstr(row, "KERNAL") == NULL &&
                   strstr(row, "CHAR ") == NULL && strstr(row, "[IO]") == NULL,
                   "Assembly row must not show legacy long source labels")) return 1;
    }

    if (expect(monitor.poll(0) == 1, "RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_r_opens_breakpoint_popup()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_CTRL_R, KEY_ESCAPE, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 3; i++) {
        if (expect(monitor.poll(0) == 0, "Setup polls should return 0")) return 1;
    }
    // After C=+R the breakpoint popup should be visible somewhere on the
    // screen: the title "BREAKPOINTS" appears in the popup body.
    char row[42];
    bool found = false;
    for (int y = 0; y < 25 && !found; y++) {
        screen.get_slice(0, y, 40, row);
        if (strstr(row, "BREAKPOINTS") != NULL) { found = true; }
    }
    if (expect(found, "C=+R must open the breakpoint list popup")) return 1;
    int left = 0, top = 0, right = 0, bottom = 0;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Breakpoint popup must draw a complete border")) return 1;
    if (expect(right - left - 1 == 38,
               "Breakpoint popup must have 38 inner columns")) return 1;
    if (expect(bottom == top + 15,
               "Breakpoint popup must match the Bookmark popup height")) return 1;
    if (expect(screen.colors[top + 1][left + 1] == ui.color_fg,
               "Breakpoint popup title must use the normal popup foreground color")) return 1;
    get_popup_line(screen, 2, row, sizeof(row));
    if (expect(strncmp(row, "0 EMPTY", strlen("0 EMPTY")) == 0,
               "Breakpoint popup must show slot 0")) return 1;
    get_popup_line(screen, 11, row, sizeof(row));
    if (expect(strncmp(row, "9 EMPTY", strlen("9 EMPTY")) == 0,
               "Breakpoint popup must show slot 9 without scrolling")) return 1;
    get_popup_line(screen, 12, row, sizeof(row));
    if (expect(strspn(row, " ") == strlen(row),
               "Breakpoint popup must separate slots from the help row")) return 1;
    get_popup_line(screen, 13, row, sizeof(row));
    if (expect(strncmp(row, "0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res",
                       strlen("0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res")) == 0,
               "Breakpoint popup help row must list jump/store/label/enable/reset")) return 1;
    int slot_y = top + 1 + 2;
    for (int x = left + 1; x < right; x++) {
        if (expect(screen.colors[slot_y][x] == ui.color_fg,
                   "Breakpoint popup selected row must use normal popup foreground color")) return 1;
        if (expect(screen.colors[slot_y][x] != 1,
                   "Breakpoint popup selected row must not use white accent color")) return 1;
        if (expect(screen.reverse_chars[slot_y][x],
                   "Breakpoint popup selected row must use reverse highlight")) return 1;
    }
    if (expect(monitor.poll(0) == 0, "ESC closes the popup")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug first")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_r_opens_breakpoint_popup_outside_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'R', KEY_BREAK, KEY_CTRL_R, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Breakpoint toggle should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 0, "C=+R outside Debug should stay in monitor")) return 1;

    char row[42];
    bool found = false;
    for (int y = 0; y < 25 && !found; y++) {
        screen.get_slice(0, y, 40, row);
        if (strstr(row, "BREAKPOINTS") != NULL) {
            found = true;
        }
    }
    if (expect(found, "C=+R outside Debug must open the breakpoint popup when breakpoints exist")) return 1;
    get_popup_line(screen, 13, row, sizeof(row));
    if (expect(strncmp(row, "0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res",
                       strlen("0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res")) == 0,
               "Non-Debug breakpoint popup must use the requested help row")) return 1;
    if (expect(monitor.poll(0) == 0, "ESC closes the non-Debug breakpoint popup")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_popup_navigation_redraws_only_popup()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = {
        'A', 'D', KEY_CTRL_R, KEY_DOWN, KEY_ESCAPE, KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 3; i++) {
        if (expect(monitor.poll(0) == 0, "Setup should open breakpoint popup")) return 1;
    }

    int left = 0, top = 0, right = 0, bottom = 0;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Breakpoint popup rect should be discoverable")) return 1;

    screen.reset_write_counts();
    if (expect(monitor.poll(0) == 0, "DOWN should navigate inside breakpoint popup")) return 1;
    if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
               "Breakpoint popup navigation must not redraw monitor content or frame")) return 1;

    screen.reset_write_counts();
    if (expect(monitor.poll(0) == 0, "ESC should close breakpoint popup")) return 1;
    char row[42];
    bool title_found = false;
    for (int y = 0; y < 25 && !title_found; y++) {
        screen.get_slice(0, y, 40, row);
        if (strstr(row, "BREAKPOINTS") != NULL) title_found = true;
    }
    if (expect(!title_found, "Closing breakpoint popup must remove its title")) return 1;
    if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
               "Closing breakpoint popup must not leave stale popup border corners")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug first")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_debug_pc_viewport_keeps_context_margin()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    for (int i = 0; i < 32; i++) {
        backend.write((uint16_t)(0xC000 + i), 0xEA);
    }
    backend.canned_snapshot_set = true;
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xC003;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.sr = 0x24;

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("C000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Goto viewport program")) return 1;
    if (expect(monitor.poll(0) == 0, "Switch to ASM")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug and sync to PC")) return 1;

    int row_c003 = find_row_with_address(screen, "C003");
    int row_c000 = find_row_with_address(screen, "C000");
    if (expect(row_c003 > row_c000, "Current PC must not be pinned to the first ASM row")) return 1;
    if (expect(row_c003 - row_c000 >= 3,
               "Current PC should preserve three instruction rows above where possible")) return 1;

    if (expect(backend.last_session != NULL, "Debug session should exist before stepping")) return 1;
    backend.last_session->next_ctx.pc = 0xC004;
    if (expect(monitor.poll(0) == 0, "Over should step to next current PC")) return 1;
    int row_c004 = find_row_with_address(screen, "C004");
    if (expect(row_c004 == row_c003 + 1,
               "Linear stepping should move the highlighted PC down naturally")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_breakpoint_popup_blocks_debug_execution_keys()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = {
        'A', 'D', KEY_CTRL_R, 'D', 'T', 'O', 'G', 'R',
        KEY_ESCAPE, KEY_BREAK, KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 11);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 11; i++) {
        int r = monitor.poll(0);
        if (i < 10) {
            if (expect(r == 0, "Breakpoint popup/modal polls should stay in monitor")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits the monitor")) return 1;
        }
    }
    StubDebugSession *sess = backend.last_session;
    if (expect(sess != NULL, "Entering Debug should create a session")) return 1;
    if (expect(sess->over_calls == 0 && sess->trace_calls == 0 &&
               sess->over_at_calls == 0 && sess->trace_at_calls == 0 &&
               sess->out_calls == 0 && sess->go_calls == 0,
               "Breakpoint popup must block Debug execution commands")) return 1;
    monitor.deinit();
    return 0;
}

static int test_unsupported_session_emits_refusal()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    backend.refuse_session = true;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'T', KEY_RETURN, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "Setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(ui.popup_count >= 1, "Unsupported Trace must surface a refusal popup")) return 1;
    if (expect(strstr(ui.last_popup, "NOT SUPPORTED") != NULL,
               "Refusal message must be explicit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_b_keeps_binary_view_in_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'B', KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 5; i++) {
        int r = monitor.poll(0);
        if (i < 4) {
            if (expect(r == 0, "Setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
        if (i == 2) {
            char header[40];
            screen.get_slice(1, 3, 38, header);
            if (expect(strstr(header, "MONITOR BIN") != NULL,
                       "B must switch to Binary view while Debug is active")) return 1;
        }
    }
    monitor.deinit();
    return 0;
}

static int test_ctrl_b_keeps_bookmark_popup_in_debug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_CTRL_B, KEY_ESCAPE, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 3; i++) {
        if (expect(monitor.poll(0) == 0, "Setup polls should return 0")) return 1;
    }
    char row[42];
    bool found = false;
    for (int y = 0; y < 25 && !found; y++) {
        screen.get_slice(0, y, 40, row);
        if (strstr(row, "BOOKMARKS") != NULL) { found = true; }
    }
    if (expect(found, "C=+B must open the bookmark popup while Debug is active")) return 1;
    int left = 0, top = 0, right = 0, bottom = 0;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup border must remain intact above the Debug footer")) return 1;
    if (expect(right - left - 1 == 38,
               "Bookmark popup must keep its 38-column body in Debug mode")) return 1;
    if (expect(backend.last_session == NULL ||
               (backend.last_session->over_calls == 0 &&
                backend.last_session->trace_calls == 0 &&
                backend.last_session->over_at_calls == 0 &&
                backend.last_session->trace_at_calls == 0 &&
                backend.last_session->out_calls == 0 &&
                backend.last_session->go_calls == 0),
               "C=+B must not be treated as a Debug execution command")) return 1;
    get_popup_line(screen, 13, row, sizeof(row));
    if (expect(strncmp(row, "0-9/RET:Jmp  S:Set  L:Label  DEL:Reset",
                       strlen("0-9/RET:Jmp  S:Set  L:Label  DEL:Reset")) == 0,
               "Bookmark popup help row must use the requested wording")) return 1;
    if (expect(monitor.poll(0) == 0, "ESC closes the bookmark popup")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_cleanup_runs_on_deinit()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0xC003, 0xA9);
    backend.write(0xC004, 0x00);
    backend.write(0xC005, 0xEA);
    const int keys[] = { 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 5; i++) {
        int r = monitor.poll(0);
        if (i < 4) {
            if (expect(r == 0, "Setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    StubDebugSession *sess = backend.last_session;
    if (expect(sess != NULL, "Session must have been created")) return 1;
    // First BREAK left Debug mode, which already calls cleanup() to restore
    // any in-flight BRK patches. The session itself stays alive until deinit.
    int cleanup_during_lifetime = sess->cleanup_calls;
    if (expect(cleanup_during_lifetime >= 1,
               "Leaving Debug must invoke session cleanup at least once")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_o_after_debug_step_closes_without_go_handoff()
{
    // CTRL+O closing the monitor after a debug step must NOT schedule a deferred
    // go-handoff. The session's cleanup() (driven by debug_leave) hands the
    // parked CPU back to the interrupted program directly, so run_machine_monitor
    // must not also release ownership and NMI-redirect into a torn-down spin
    // loop - that double path is what left the C64 frozen on hardware.
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0xC003, 0xA9);
    backend.write(0xC004, 0x00);
    backend.write(0xC005, 0xEA);
    const int keys[] = { 'A', 'D', 'D', KEY_CTRL_O };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug step should return 0")) return 1;

    StubDebugSession *sess = backend.last_session;
    if (expect(sess != NULL, "Session must have been created")) return 1;

    if (expect(monitor.poll(0) == 1,
               "CTRL+O after a debug step should close the monitor")) return 1;
    if (expect(sess->cleanup_calls >= 1,
               "CTRL+O close must run session cleanup to resume the parked CPU")) return 1;

    DebugContext go_ctx;
    bool has_ctx = true;
    uint16_t go_addr = 0;
    if (expect(!monitor.consume_pending_go(&go_addr, &go_ctx, &has_ctx),
               "CTRL+O close must NOT schedule a deferred go-handoff")) return 1;
    monitor.deinit();
    return 0;
}

static int test_stop_debugging_clears_stale_go_handoff()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x2000;
    backend.canned_snapshot.sp = 0xF2;
    backend.canned_snapshot.a = 0x11;
    backend.canned_snapshot.x = 0x22;
    backend.canned_snapshot.y = 0x33;
    backend.canned_snapshot.sr = 0x24;
    backend.write(0x2000, 0xEE);
    backend.write(0x2001, 0x21);
    backend.write(0x2002, 0xD0);
    backend.write(0x2003, 0x4C);
    backend.write(0x2004, 0x00);
    backend.write(0x2005, 0x20);

    const int keys[] = { 'A', 'D', 'G', KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_debug_run_window_refreeze_enabled(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug should return 0")) return 1;
    if (expect(monitor.poll(0) == 1,
               "Freeze-mode G without breakpoints should schedule a handoff")) return 1;

    if (expect(monitor.poll(0) == 0,
               "Stop Debugging after a pending handoff should stay in monitor")) return 1;

    DebugContext go_ctx;
    bool has_ctx = true;
    uint16_t go_addr = 0;
    if (expect(!monitor.consume_pending_go(&go_addr, &go_ctx, &has_ctx),
               "Stop Debugging must clear any stale pending Go handoff")) return 1;
    if (expect(backend.read(0x2000) == 0xEE && backend.read(0x2001) == 0x21 &&
               backend.read(0x2002) == 0xD0 && backend.read(0x2003) == 0x4C &&
               backend.read(0x2004) == 0x00 && backend.read(0x2005) == 0x20,
               "Stop Debugging must not leave a stale BRK patch in the $2000 loop")) return 1;
    monitor.deinit();
    return 0;
}

static int test_stop_debugging_resumes_current_debug_context()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xC123;
    backend.canned_snapshot.sp = 0xEF;
    backend.canned_snapshot.a = 0x44;
    backend.canned_snapshot.x = 0x55;
    backend.canned_snapshot.y = 0x66;
    backend.canned_snapshot.sr = 0x24;
    backend.write(0x2000, 0xEE);
    backend.write(0x2001, 0x21);
    backend.write(0x2002, 0xD0);
    backend.write(0x2003, 0x4C);
    backend.write(0x2004, 0x00);
    backend.write(0x2005, 0x20);

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("2000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Goto $2000 should stay in monitor")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM switch should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug should return 0")) return 1;
    StubDebugSession *sess = backend.last_session;
    if (expect(sess != NULL, "Session must have been created")) return 1;
    sess->next_ctx.valid = true;
    sess->next_ctx.pc = 0x2003;
    sess->next_ctx.sp = 0xEC;
    sess->next_ctx.a = 0x45;
    sess->next_ctx.x = 0x56;
    sess->next_ctx.y = 0x67;
    sess->next_ctx.sr = 0x25;
    if (expect(monitor.poll(0) == 0, "Step from target cursor should return 0")) return 1;

    if (expect(sess->over_at_calls == 1 && sess->last_start_pc == 0x2000,
               "Step must execute from the displayed $2000 target")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP should stop Debug")) return 1;
    if (expect(sess->cleanup_to_context_calls >= 1,
               "Stop Debugging must clean up toward a CPU context")) return 1;
    if (expect(sess->cleanup_target_ctx.valid && sess->cleanup_target_ctx.pc == 0x2003 &&
               sess->cleanup_target_ctx.sp == 0xEC && sess->cleanup_target_ctx.a == 0x45 &&
               sess->cleanup_target_ctx.x == 0x56 && sess->cleanup_target_ctx.y == 0x67 &&
               sess->cleanup_target_ctx.sr == 0x25,
               "Stop Debugging cleanup target must be the current post-step context")) return 1;
    if (expect(sess->cleanup_target_ctx.pc != 0xC123,
               "Stop Debugging must not discard post-step state by restoring entry PC")) return 1;
    if (expect(backend.reset_calls == 0,
               "Stop Debugging must never reset the machine")) return 1;
    monitor.deinit();
    return 0;
}

static int test_help_screen_shows_debug_commands()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_HELP, KEY_ESCAPE, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "help test: ASM switch should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: Debug entry should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: opening help should return 0")) return 1;

    const char *lines[20];
    int n = MonitorDebug::format_help_lines(lines, 20);
    static const char *expected_lines[] = {
        "",
        "D Step Over  T Step Into  O Step Out",
        "G Continue   K Cont Crsr  RET Follow",
        "R Breakpt    C=+R Brkpts  C=+X Reset",
        "",
        "M Memory     I ASCII      V Screen",
        "A Assembly   B Binary     U Undoc/Case",
        "J Jump       P Poll       N Number",
        "E Edit       F Fill       W Width",
        "C Compare    H Hunt       Z Freeze",
        "L Load       S Save",
        "",
        "Bookmarks:     C=+B List  C=+0-9 Jump",
        "Monitor:       C=+O Open/Close",
        "Leave debug:   C=+D/RSTOP",
        "Leave edit:    C=+E/RSTOP",
        "Copy/Paste:    C=+C / C=+V",
    };
    if (expect(n == (int)(sizeof(expected_lines) / sizeof(expected_lines[0])),
               "Debug help line count must match the requested layout")) return 1;
    for (int i = 0; i < n; i++) {
        if (expect(strcmp(lines[i], expected_lines[i]) == 0,
                   "Debug help text did not match the requested layout")) return 1;
        if (expect((int)strlen(lines[i]) <= 38,
                   "Debug help line must fit within 38 columns")) return 1;
        if (expect(strstr(lines[i], "SH+O") == NULL,
                   "Debug help must not show SH+O")) return 1;
        if (expect(strstr(lines[i], "ESC") == NULL,
                   "Debug help must show RSTOP rather than ESC")) return 1;
    }
    char row[40];
    screen.get_slice(1, 3, 38, row);
    if (expect(strstr(row, "HELP ASM DEBUG") == row,
               "Debug help header must identify the ASM debug help screen")) return 1;
    for (int i = 0; i < n; i++) {
        screen.get_slice(1, 4 + i, 38, row);
        if (expect(strncmp(row, expected_lines[i], strlen(expected_lines[i])) == 0,
                   "Rendered debug help text did not match the requested layout")) return 1;
    }
    screen.get_slice(1, 22, 38, row);
    if (expect(strstr(row, "Page Up/Down:  F1/SH+SPACE / F7/SPACE") == row,
               "Debug help must show the requested paging footer")) return 1;
    if (expect_help_token_not_accented(screen, "D Step Over",
        "D Step Over must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "T Step Into",
        "T Step Into must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "O Step Out",
        "O Step Out must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "G Continue",
        "G Continue must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "K Cont Crsr",
        "K Cont Crsr must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "R Breakpt",
        "R Breakpt must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+R Brkpts",
        "C=+R Brkpts must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+X Reset",
        "C=+X Reset must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "RET Follow",
        "RET Follow must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+D/RSTOP",
        "C=+D/RSTOP must not use a distinct debug help colour")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: ESC should close help")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "help test: final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_k_runs_to_cursor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'J', 'K', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("C123", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;
    if (expect(backend.last_session != NULL, "Debug entry must create a session")) return 1;
    backend.last_session->next_ctx.pc = 0xC123;
    if (expect(monitor.poll(0) == 0, "Jump should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "K Cursor should return 0")) return 1;
    if (expect(backend.last_session->run_to_calls == 1,
               "K Cursor must use the dedicated run-to-cursor session path")) return 1;
    if (expect(backend.last_session->last_run_to_target == 0xC123,
               "K Cursor must use the current cursor address as its target")) return 1;
    if (expect(backend.last_session->last_start_pc == 0xC003,
               "K Cursor must continue from the captured debug PC")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_k_without_context_runs_from_debug_entry_to_cursor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0xE000, 0x85);
    backend.write(0xE001, 0x56);
    backend.write(0xE002, 0x20);
    backend.write(0xE003, 0x0F);
    backend.write(0xE004, 0xBC);

    const int keys[] = { 'J', 'A', 'D', 'J', 'K', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);
    ui.push_prompt("E002", 3);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        int r = monitor.poll(0);
        if (i < 6) {
            if (expect(r == 0, "No-context K setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->run_to_calls == 1,
               "No-context K must use run-to-cursor")) return 1;
    if (expect(backend.last_session->last_start_pc == 0xE000,
               "No-context K must continue from the debug entry address, not the moved cursor")) return 1;
    if (expect(backend.last_session->last_run_to_target == 0xE002,
               "No-context K must target the current cursor address")) return 1;
    monitor.deinit();
    return 0;
}

static int test_ctrl_i_swaps_interface_in_monitor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    g_swap_interface_type_calls = 0;

    const int keys[] = { KEY_CTRL_I };
    FakeKeyboard keyboard(keys, 1);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == MENU_HIDE,
               "C=+I in the monitor must route through swap_interface_type")) return 1;
    if (expect(g_swap_interface_type_calls == 1,
               "C=+I must invoke swap_interface_type exactly once")) return 1;
    monitor.deinit();
    return 0;
}

static int test_jump_rejects_non_hex_input_and_uppercases()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'J', KEY_BREAK };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("G123", 1);
    ui.push_prompt("c0de", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump command should return 0")) return 1;
    if (expect(strcmp(ui.last_popup, "HEX 0-9/A-F ONLY") == 0,
               "Jump must reject non-hex input with a monitor-local validation popup")) return 1;
    if (expect(ui.last_prompt_maxlen == 4,
               "Jump prompt must limit input to four characters")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "$C0DE") != NULL,
               "Jump must normalize lowercase hex input to uppercase")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP should exit the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_edit_and_debug_compose_in_header()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // Use ASCII view because hex edit needs nibble input the keymap doesn't
    // intercept here. Sequence: switch to ASM, enter Debug, then E for edit.
    const int keys[] = { 'A', 'D', 'E', KEY_CTRL_E, KEY_ESCAPE, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit ok")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL && strstr(header, "Edit") != NULL,
               "Header must show both Dbg and Edit when both modes are active")) return 1;
    if (expect(monitor.poll(0) == 0, "C=+E drops Edit first")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL && strstr(header, "Edit") == NULL,
               "C=+E in Debug+Edit must keep Dbg and clear Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "ESC drops Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

// --- Freeze-mode render-target regression (temporary-BRK step-over et al.) ---
//
// These tests drive the real BrkDebugSession engine through FakeFreezeMachine.
// The invariant under test: after any debug run that temporarily unfroze the
// machine, the engine must re-freeze it before returning, so the monitor's
// render target (the firmware/menu screen) is preserved instead of leaking into
// the live C64 screen RAM. Before the fix the engine unfroze and never
// re-froze, so `frozen` would be false here and the C64 screen sentinel would
// later be overwritten by monitor redraws.

static int test_freeze_step_over_refreezes_and_preserves_screen()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.stamp_screen_sentinel(0x55, 40); // BASIC-like content in $0400

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);

    if (expect(r == DebugSession::DBG_OK, "freeze step-over must complete")) return 1;
    if (expect(m.frozen,
               "freeze step-over must re-freeze the C64 before returning to the monitor")) return 1;
    if (expect(m.unfreeze_calls == 1,
               "freeze step-over must unfreeze exactly once to run the step")) return 1;
    if (expect(m.refreeze_calls == 1,
               "freeze step-over must re-freeze exactly once after the step")) return 1;
    if (expect(m.screen_sentinel_intact(0x55, 40),
               "freeze step-over must not write monitor UI into C64 screen RAM")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze step-over must set screen_render_target_invalidated so caller can restore chrome")) return 1;
    return 0;
}

// Regression for the sporadic freeze-mode runaway-stepping loop seen around the
// BASIC FAC multiply loop at $B9A6. The freeze-mode run window unfreezes the
// whole machine, so the live CPU free-runs from its frozen PC before the
// controlled NMI single-step is set up. In a hot tight loop it can reach the
// step BRK we already installed during that gap, fire the handler, and leave
// the sentinel set with a context the engine never controlled (a "$4Cxx"-style
// junk PC was observed). nmi_redirect_to() must clear the sentinel as the last
// act of the controlled launch so wait_for_sentinel() only ever observes the
// result of THIS run. Without the clear, the engine returns the stale context
// and desyncs cpu_parked_in_spin from the real CPU, which is what produced the
// runaway loop.
static int test_freeze_nmi_step_ignores_stale_sentinel()
{
    FakeVisibleRomMachine m(true); // freeze mode, BASIC ROM range like $B9A6
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;
    m.basic_rom[0xB9A6 - 0xA000] = 0xEA; // NOP at $B9A6
    m.basic_rom[0xB9A7 - 0xA000] = 0xEA; // fall-through / step BRK target
    m.basic_rom[0xB9A8 - 0xA000] = 0xEA;

    // Model the gap free-run hitting the installed BRK while the NMI launch is
    // being set up: the sentinel goes high mid-setup, with STORE_* holding a
    // stale junk PC the engine never asked to stop at ($4C27 was observed).
    m.stale_sentinel_during_nmi_setup = true;
    m.ram[FAKE_STORE_PCLO] = (uint8_t)((0x4C27 + 2) & 0xFF);
    m.ram[FAKE_STORE_PCHI] = (uint8_t)((0x4C27 + 2) >> 8);
    // The controlled run captures the real next PC on the next delay_ms tick.
    m.arm_capture_context(0xB9A7, 0xFF, 0, 0, 0, 0);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA }; // NOP at $B9A6, fall-through $B9A7
    DebugPredictResult pred;
    debug_predict(0xB9A6, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xB9A6, pred, &ctx); // first step -> NMI launch
    if (expect(r == DebugSession::DBG_OK, "freeze NMI step must complete")) return 1;
    if (expect(m.nmi_pulses == 1,
               "freeze first step must launch via exactly one NMI redirect")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xB9A7,
               "freeze NMI step must report the controlled run's PC, not the stale gap hit")) return 1;
    if (expect(m.ram[FAKE_SENTINEL_ADDR] == 0x00,
               "freeze NMI step must leave the sentinel cleared")) return 1;
    return 0;
}

static int test_freeze_step_over_with_context_refreezes()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.stamp_screen_sentinel(0x41, 40);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2000;
    from.sp = 0xFF;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over(from, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "freeze step-over (with context) must complete")) return 1;
    if (expect(m.frozen, "freeze step-over (with context) must re-freeze")) return 1;
    if (expect(m.screen_sentinel_intact(0x41, 40),
               "freeze step-over (with context) must not touch C64 screen RAM")) return 1;
    return 0;
}

static int test_cleanup_to_context_without_entry_context_never_resets()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);
    m.ram[0x1234] = 0x5A;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Step without entry context must still complete")) return 1;

    m.cleanup_to_context(NULL);
    if (expect(m.reset_calls == 0,
               "Leaving Debug without an entry context must never reset the machine")) return 1;
    if (expect(m.ram[0x1234] == 0x5A,
               "Leaving Debug must preserve unrelated memory changes")) return 1;
    return 0;
}

static int test_freeze_step_into_refreezes()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.stamp_screen_sentinel(0x42, 40);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "freeze step-into must complete")) return 1;
    if (expect(m.frozen, "freeze step-into must re-freeze")) return 1;
    if (expect(m.unfreeze_attempts == 1, "freeze step-into must call the unfreeze hook once")) return 1;
    if (expect(m.refreeze_calls == 1, "freeze step-into must re-freeze once")) return 1;
    if (expect(m.screen_sentinel_intact(0x42, 40),
               "freeze step-into must not touch C64 screen RAM")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze step-into must set render-target invalidation after refreeze")) return 1;
    return 0;
}

static int test_freeze_step_out_refreezes()
{
    FakeFreezeMachine m(true);
    m.stamp_screen_sentinel(0x43, 40);

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0x30;
    m.ram[0x2004] = 0xEA;
    m.ram[0x3000] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0x00, 0x30 };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    fake_seed_captured_context(m, 0x3000, 0xFB, 0, 0, 0, 0x24);
    DebugContext from;
    if (expect(m.trace_at(0x2001, pred, &from) == DebugSession::DBG_OK,
               "freeze trace into JSR must prepare a Step Out target")) return 1;
    m.unfreeze_attempts = 0;
    m.unfreeze_calls = 0;
    m.refreeze_calls = 0;
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.arm_capture_context(0x2004, 0xFD, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.step_out(from, &ctx);
    if (expect(r == DebugSession::DBG_OK, "freeze step-out must complete")) return 1;
    if (expect(m.frozen, "freeze step-out must re-freeze")) return 1;
    if (expect(m.unfreeze_attempts == 1, "freeze step-out must call the unfreeze hook once")) return 1;
    if (expect(m.refreeze_calls == 1, "freeze step-out must re-freeze once")) return 1;
    if (expect(m.screen_sentinel_intact(0x43, 40),
               "freeze step-out must not touch C64 screen RAM")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze step-out must set render-target invalidation after refreeze")) return 1;
    return 0;
}

static int test_step_out_uses_traced_jsr_target_even_when_target_is_above_pc()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;

    m.kernal_rom[0x0021] = 0x20;
    m.kernal_rom[0x0022] = 0x00;
    m.kernal_rom[0x0023] = 0xE2;
    m.kernal_rom[0x0024] = 0xEA;
    m.kernal_rom[0x0200] = 0xEA;

    // Older stack data that must not be selected; Step Out is driven by the
    // return target recorded by the actual Trace-Into operation.
    m.ram[0x01FA] = 0x03;
    m.ram[0x01FB] = 0xC1;
    m.ram[0xC101] = 0x20;
    m.ram[0xC102] = 0x00;
    m.ram[0xC103] = 0xC0;
    m.ram[0xC104] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0x00, 0xE2 };
    DebugPredictResult pred;
    debug_predict(0xE021, jsr, false, &pred);
    fake_seed_captured_context(m, 0xE200, 0xF7, 0, 0, 0, 0x24);
    DebugContext from;
    if (expect(m.trace_at(0xE021, pred, &from) == DebugSession::DBG_OK,
               "KERNAL trace must prepare the Step Out return target")) return 1;
    from.pc = 0xE100;
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.arm_capture_context(0xE024, 0xF9, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.step_out(from, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "KERNAL Step Out must accept the traced JSR return target")) return 1;
    if (expect(m.last_rom_patch_addr == 0xE024,
               "KERNAL Step Out must patch the nearest KERNAL return target")) return 1;
    if (expect(m.last_brk_patch_addr != 0xC104,
               "KERNAL Step Out must not fall back to an older RAM caller")) return 1;
    return 0;
}

static int test_step_out_ignores_nearby_rts_and_patches_after_traced_jsr()
{
    FakeFreezeMachine m(false);

    // Active trace: $C6A0 JSR $C6E0 records return address $C6A3, so
    // Step Out must install its temporary BRK at $C6A3. The current PC is
    // deliberately below the JSR target to catch the old target<=PC heuristic.
    m.ram[0xC6A0] = 0x20;
    m.ram[0xC6A1] = 0xE0;
    m.ram[0xC6A2] = 0xC6;
    m.ram[0xC6A3] = 0xEA;
    m.ram[0xC6E0] = 0xEA;

    // Decoy RTS opcodes near both the JSR entry and current helper body. A
    // nearby-RTS implementation would patch one of these instead of $C6A3.
    m.ram[0xC6D2] = 0x60;
    m.ram[0xC6D5] = 0x60;
    m.ram[0xC6E1] = 0x60;

    uint8_t jsr[3] = { 0x20, 0xE0, 0xC6 };
    DebugPredictResult pred;
    debug_predict(0xC6A0, jsr, false, &pred);
    fake_seed_captured_context(m, 0xC6E0, 0xFD, 0, 0, 0, 0x24);
    DebugContext from;
    if (expect(m.trace_at(0xC6A0, pred, &from) == DebugSession::DBG_OK,
               "RAM trace must prepare the Step Out return target")) return 1;
    from.pc = 0xC6D0;
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.arm_capture_context(0xC6A3, 0xFF, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.step_out(from, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Step Out must use the active JSR stack frame")) return 1;
    if (expect(m.last_brk_patch_addr == 0xC6A3,
               "Step Out must patch the opcode immediately after the active JSR")) return 1;
    if (expect(m.last_brk_patch_addr != 0xC6D2 &&
               m.last_brk_patch_addr != 0xC6D5 &&
               m.last_brk_patch_addr != 0xC6E1,
               "Step Out must not patch nearby RTS opcodes")) return 1;
    return 0;
}

static int test_step_out_refuses_stale_far_stack_frame()
{
    FakeFreezeMachine m(false);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2003;
    from.sp = 0xF7;

    // Stale interpreter frame left on the C64 stack while the user is debugging
    // an arbitrary loop at $2000. It is a real JSR-looking frame, but it cannot
    // be the active caller of the current program counter.
    m.ram[0x01F8] = 0x03;
    m.ram[0x01F9] = 0xC1;
    m.ram[0xC101] = 0x20;
    m.ram[0xC102] = 0x00;
    m.ram[0xC103] = 0xC0;
    m.ram[0xC104] = 0xEA;

    DebugContext ctx;
    DebugSession::Result r = m.step_out(from, &ctx);
    if (expect(r == DebugSession::DBG_NOT_IN_SUBROUTINE,
               "Step Out must report that no traced subroutine is active")) return 1;
    if (expect(m.brk_patch_writes == 0,
               "Refused Step Out must not install a temporary BRK")) return 1;
    if (expect(m.ram[FAKE_SENTINEL_ADDR] == 0x00,
               "Refused Step Out must not start execution")) return 1;
    return 0;
}

static int test_over_rts_refuses_non_jsr_stack_target()
{
    FakeFreezeMachine m(false);
    m.ram[0xE042] = 0x60;   // RTS at cursor
    m.ram[0xE043] = 0xEA;
    m.ram[0xE044] = 0xEA;

    // Forge an RTS return target whose recorded caller is not a JSR.
    // RTS pops 0x21FF and jumps to 0x2200.
    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE042;
    from.sp = 0xFC;
    m.ram[0x01FD] = 0xFF;
    m.ram[0x01FE] = 0x21;
    m.ram[0x21FD] = 0xEA;   // caller opcode at ret-2 (not JSR)

    uint8_t rts[3] = { 0x60, 0x00, 0x00 };
    DebugPredictResult pred;
    debug_predict(0xE042, rts, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over(from, pred, &ctx);
    if (expect(r == DebugSession::DBG_NOT_IN_SUBROUTINE,
               "Over on RTS with a non-JSR stack target must report not-in-subroutine")) return 1;
    if (expect(m.brk_patch_writes == 0,
               "Refused RTS Over must not install a temporary BRK")) return 1;
    if (expect(m.ram[FAKE_SENTINEL_ADDR] == 0x00,
               "Refused RTS Over must not start execution")) return 1;
    return 0;
}

static int test_traced_rts_uses_recorded_return_target_when_stack_is_unreliable()
{
    FakeFreezeMachine m(true);

    m.ram[0x2000] = 0x20; // JSR $3000
    m.ram[0x2001] = 0x00;
    m.ram[0x2002] = 0x30;
    m.ram[0x2003] = 0xEA;
    m.ram[0x3000] = 0xEA;
    m.ram[0x3005] = 0x60; // RTS

    uint8_t jsr[3] = { 0x20, 0x00, 0x30 };
    DebugPredictResult pred;
    debug_predict(0x2000, jsr, false, &pred);
    fake_seed_captured_context(m, 0x3000, 0xF8, 0, 0, 0, 0x24);

    DebugContext traced;
    if (expect(m.trace_at(0x2000, pred, &traced) == DebugSession::DBG_OK,
               "Trace Into must record the JSR fall-through return target")) return 1;

    traced.pc = 0x3005;
    traced.sp = 0xF8;
    m.ram[0x01F9] = 0x44;
    m.ram[0x01FA] = 0x44;
    m.ram[0x4442] = 0xEA; // stack-derived caller is deliberately not JSR
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.arm_capture_context(0x2003, 0xFA, 0, 0, 0, 0x24);

    uint8_t rts[3] = { 0x60, 0x00, 0x00 };
    debug_predict(0x3005, rts, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace(traced, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "RTS after Trace Into must use the recorded return target")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2003,
               "RTS must stop at the recorded JSR fall-through")) return 1;
    if (expect(m.last_brk_patch_addr == 0x2003,
               "RTS must patch the recorded return target, not stale stack data")) return 1;
    if (expect(m.frozen, "Freeze-mode RTS step must refreeze before returning")) return 1;
    return 0;
}

static int test_traced_kernal_to_basic_step_out_patches_kernal_return()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;

    m.kernal_rom[0x000B] = 0x20;
    m.kernal_rom[0x000C] = 0xD4;
    m.kernal_rom[0x000D] = 0xBA;
    m.kernal_rom[0x000E] = 0x20;
    m.basic_rom[0x1AD4] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0xD4, 0xBA };
    DebugPredictResult pred;
    debug_predict(0xE00B, jsr, false, &pred);

    fake_seed_captured_context(m, 0xBAD4, 0xF8, 0x00, 0xFF, 0xCF, 0x37);
    DebugContext traced;
    DebugSession::Result r = m.trace_at(0xE00B, pred, &traced);
    if (expect(r == DebugSession::DBG_OK,
               "Trace Into KERNAL JSR $BAD4 must complete")) return 1;
    if (expect(traced.pc == 0xBAD4,
               "Trace Into must capture the BASIC target PC")) return 1;

    fake_seed_captured_context(m, 0xE00E, 0xFA, 0x00, 0xFF, 0xCF, 0x37);
    m.arm_capture_context(0xE00E, 0xFA, 0x00, 0xFF, 0xCF, 0x37);
    DebugContext out;
    r = m.step_out(traced, &out);
    if (expect(r == DebugSession::DBG_OK,
               "Step Out from traced BASIC helper must complete")) return 1;
    if (expect(m.last_rom_patch_addr == 0xE00E,
               "Step Out must patch the KERNAL return target")) return 1;
    if (expect(m.kernal_rom[0x000E] == 0x20,
               "Step Out must restore the KERNAL return byte")) return 1;
    return 0;
}

static int test_freeze_go_breakpoint_refreezes()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.ram[0x2005] = 0xEA; // breakpoint target byte (non-zero)
    m.stamp_screen_sentinel(0x44, 40);

    MonitorBreakpoints bps;
    bps.allocate(0x2005, 0x07);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2000;
    from.sp = 0xFF;

    DebugSession::Result r = m.go(from, &bps, 0x2000);
    if (expect(r == DebugSession::DBG_OK, "freeze Go-to-breakpoint must complete")) return 1;
    if (expect(m.frozen,
               "freeze Go/continue to breakpoint must re-freeze before returning")) return 1;
    if (expect(m.unfreeze_attempts == 1, "freeze Go must call the unfreeze hook once")) return 1;
    if (expect(m.refreeze_calls == 1, "freeze Go must re-freeze once")) return 1;
    if (expect(m.screen_sentinel_intact(0x44, 40),
               "freeze Go must not touch C64 screen RAM")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze Go must set render-target invalidation after refreeze")) return 1;
    return 0;
}

static int test_go_from_current_breakpoint_stops_at_callee_breakpoint()
{
    FakeFreezeMachine m(false);
    m.ram[0x2000] = 0x20; // JSR $3000
    m.ram[0x2001] = 0x00;
    m.ram[0x2002] = 0x30;
    m.ram[0x2003] = 0xEA;
    m.ram[0x3000] = 0xEA;

    MonitorBreakpoints bps;
    bps.allocate(0x2000, 0x07);
    bps.allocate(0x3000, 0x07);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2000;
    from.sp = 0xFF;

    m.arm_capture_context(0x3000, 0xFD, 0, 0, 0, 0x24);
    DebugSession::Result r = m.go(from, &bps, 0x2000);
    if (expect(r == DebugSession::DBG_OK,
               "G from a breakpoint must stop at an enabled breakpoint in the callee")) return 1;

    DebugContext stopped;
    if (expect(m.snapshot(&stopped) == DebugSession::DBG_OK,
               "G callee breakpoint must leave a captured context")) return 1;
    if (expect(stopped.valid && stopped.pc == 0x3000,
               "G callee breakpoint must be the captured stop PC")) return 1;
    if (expect(m.brk_patch_writes == 1,
               "G must keep the current breakpoint masked for the whole continue run")) return 1;
    if (expect(fake_recorded_brk_patch(m, 0x3000),
               "G must arm the callee breakpoint while the current one stays masked")) return 1;
    return 0;
}

static int test_go_from_current_visible_rom_breakpoint_uses_patch_aware_bytes()
{
    FakeVisibleRomMachine m(true);
    m.allow_visible_rom_patching = true;
    m.basic_rom[0x1C9B] = 0xEA;
    m.basic_rom[0x1C9C] = 0xEA;
    m.basic_rom[0x1CF2] = 0xEA;
    m.force_raw_peek_brk = true;
    m.force_raw_peek_brk_addr = 0xBC9B;

    MonitorBreakpoints bps;
    bps.allocate(0xBC9B, 0x07);
    bps.allocate(0xBCF2, 0x07);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xBC9B;
    from.sp = 0xF7;

    m.arm_capture_context(0xBCF2, 0xF5, 0, 0, 0, 0x24);
    DebugSession::Result r = m.go(from, &bps, 0xE000);
    if (expect(r == DebugSession::DBG_OK,
               "G from a visible-ROM breakpoint must not classify raw BRK as the opcode")) return 1;
    if (expect(fake_recorded_brk_patch(m, 0xBCF2),
               "G must arm the next BASIC ROM breakpoint after skipping the current one")) return 1;

    DebugContext stopped;
    if (expect(m.snapshot(&stopped) == DebugSession::DBG_OK,
               "G must leave a captured context at the next breakpoint")) return 1;
    if (expect(stopped.valid && stopped.pc == 0xBCF2,
               "G must stop at the requested next BASIC breakpoint")) return 1;
    if (expect(m.frozen, "Freeze-mode visible-ROM G must refreeze before returning")) return 1;
    return 0;
}

static int test_step_over_stops_at_callee_breakpoint()
{
    FakeFreezeMachine m(false);
    m.ram[0x2000] = 0x20; // JSR $3000
    m.ram[0x2001] = 0x00;
    m.ram[0x2002] = 0x30;
    m.ram[0x2003] = 0xEA;
    m.ram[0x3000] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0x00, 0x30 };
    DebugPredictResult pred;
    debug_predict(0x2000, jsr, false, &pred);

    MonitorBreakpoints bps;
    bps.allocate(0x3000, 0x07);

    m.arm_capture_context(0x3000, 0xFD, 0, 0, 0, 0x24);
    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &bps, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Step Over must stop at an enabled breakpoint in the callee")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x3000,
               "Step Over callee breakpoint must be the captured stop PC")) return 1;
    if (expect(fake_recorded_brk_patch(m, 0x3000),
               "Step Over must arm active breakpoints before running")) return 1;
    if (expect(fake_recorded_brk_patch(m, 0x2003),
               "Step Over must still patch the JSR fall-through")) return 1;
    return 0;
}

static int test_visible_basic_step_uses_rom_patch_support()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xA000);
    m.allow_visible_rom_patching = true;
    m.ram[0xA001] = 0x5A;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xA000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0xA000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible BASIC step must succeed when ROM patching is supported")) return 1;
    if (expect(m.basic_rom[1] == 0xEA,
               "Visible BASIC patch byte must be restored after the step")) return 1;
    if (expect(m.ram[0xA001] == 0x5A,
               "Visible BASIC patching must not scribble into underlying RAM")) return 1;
    if (expect(m.rom_patch_writes == 2,
               "Visible BASIC step must patch and restore the ROM image exactly once")) return 1;
    return 0;
}

static int test_visible_rom_step_bytes_use_cpu_visible_mapping()
{
    FakeVisibleRomMachine m(false);
    uint8_t bytes[3] = { 0, 0, 0 };

    m.allow_visible_rom_patching = false;
    m.cpu_port = 0x07;
    m.kernal_rom[0] = 0x85;
    m.kernal_rom[1] = 0x56;
    m.kernal_rom[2] = 0xEA;
    m.ram[0xE000] = 0x00;
    m.ram[0xE001] = 0x00;
    m.ram[0xE002] = 0x00;

    if (expect(m.read_step_bytes(0xE000, bytes, 3),
               "Visible KERNAL read_step_bytes must succeed")) return 1;
    if (expect(bytes[0] == 0x85 && bytes[1] == 0x56 && bytes[2] == 0xEA,
               "Visible KERNAL read_step_bytes must use ROM bytes, not RAM under ROM")) return 1;

    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);
    if (expect(pred.kind != DBG_PREDICT_BRK && pred.kind != DBG_PREDICT_UNSAFE,
               "Visible KERNAL ROM bytes must not be classified from RAM-under-ROM BRK")) return 1;

    m.cpu_port = 0x05;
    if (expect(m.read_step_bytes(0xE000, bytes, 3),
               "Hidden KERNAL read_step_bytes must also succeed")) return 1;
    if (expect(bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x00,
               "Hidden KERNAL read_step_bytes must fall back to RAM under ROM")) return 1;

    m.cpu_port = 0x07;
    m.basic_rom[0] = 0x18;
    m.basic_rom[1] = 0xEA;
    m.basic_rom[2] = 0xEA;
    m.ram[0xA000] = 0x00;
    m.ram[0xA001] = 0x00;
    m.ram[0xA002] = 0x00;
    if (expect(m.read_step_bytes(0xA000, bytes, 3),
               "Visible BASIC read_step_bytes must succeed")) return 1;
    if (expect(bytes[0] == 0x18 && bytes[1] == 0xEA && bytes[2] == 0xEA,
               "Visible BASIC read_step_bytes must use ROM bytes, not RAM under ROM")) return 1;

    return 0;
}

static int test_u64_debug_cpu_port_uses_monitor_bank()
{
    FakeCpuPortBackend backend;

    backend.set_monitor_cpu_port(0x07);
    backend.live_cpu_port = 0x05;
    if (expect(u64_debug_step_cpu_port(&backend) == 0x07,
               "U64 Debug stepping must use the monitor-selected CPU bank")) return 1;
    if (expect(backend.live_reads == 0,
               "U64 Debug stepping must not sample the live CPU port for cursor stepping")) return 1;

    backend.set_monitor_cpu_port(0x05);
    backend.live_cpu_port = 0x07;
    if (expect(u64_debug_step_cpu_port(&backend) == 0x05,
               "U64 Debug stepping must follow monitor bank changes")) return 1;
    if (expect(backend.live_reads == 0,
               "U64 Debug stepping must not sample live bank changes")) return 1;
    if (expect(u64_debug_step_cpu_port(0) == 0x07,
               "U64 Debug stepping must default to all ROMs visible without a backend")) return 1;

    return 0;
}

static int test_cursor_visible_rom_step_sets_monitor_cpu_port_before_jump()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;
    m.ram[0x0001] = 0x05;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    m.arm_capture_context(0xE001, 0xFE, 0, 0, 0, 0x24);
    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "No-context visible-ROM Step Into must complete")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xE001,
               "No-context visible-ROM Step Into must capture the ROM fall-through")) return 1;
    if (expect(fake_nmi_trampoline_stores_cpu_port(m, 0x07),
               "No-context visible-ROM launch must select the monitor CPU bank before jumping")) return 1;
    if (expect(m.last_rom_patch_addr == 0xE001,
               "No-context visible-ROM Step Into must patch the monitor-bank ROM fall-through")) return 1;
    return 0;
}

static int test_visible_kernal_step_into_uses_rom_patch_support()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = true;
    m.ram[0xE001] = 0x6C;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL Step Into must succeed when ROM patching is supported")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Visible KERNAL Step Into patch byte must be restored")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Visible KERNAL Step Into must not patch RAM under ROM")) return 1;
    if (expect(m.rom_patch_writes == 2,
               "Visible KERNAL Step Into must patch and restore the ROM image")) return 1;
    return 0;
}

static int test_visible_kernal_step_over_jsr_patches_fallthrough_rom()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.kernal_rom[0] = 0x20;
    m.kernal_rom[1] = 0x0F;
    m.kernal_rom[2] = 0xBC;
    m.kernal_rom[3] = 0xEA;
    m.ram[0xE003] = 0x7B;

    uint8_t bytes[3] = { 0x20, 0x0F, 0xBC };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL Step Over JSR must patch the ROM fall-through target")) return 1;
    if (expect(m.kernal_rom[3] == 0xEA,
               "Visible KERNAL Step Over JSR must restore fall-through ROM byte")) return 1;
    if (expect(m.ram[0xE003] == 0x7B,
               "Visible KERNAL Step Over JSR must not patch RAM under ROM")) return 1;
    if (expect(m.rom_patch_writes == 2,
               "Visible KERNAL Step Over JSR must patch and restore ROM exactly once")) return 1;
    return 0;
}

static int test_visible_kernal_step_without_rom_patch_support_refuses_cleanly()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = false;
    m.ram[0xE001] = 0x6C;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_NOT_SUPPORTED,
               "Visible KERNAL step must report not-supported when ROM patching is unavailable")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Visible KERNAL step must leave the ROM byte untouched on refusal")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Visible KERNAL refusal must not write into underlying RAM")) return 1;
    if (expect(m.rom_patch_writes == 0,
               "Visible KERNAL refusal must not attempt ROM patch writes")) return 1;
    return 0;
}

static int test_visible_rom_breakpoint_go_uses_same_capability_gate()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = false;
    m.ram[0xE001] = 0x6C;

    MonitorBreakpoints bps;
    bps.allocate(0xE001, 0x07);
    DebugContext from;
    debug_context_reset(&from);

    DebugSession::Result r = m.go(from, &bps, 0xC000);
    if (expect(r == DebugSession::DBG_NOT_SUPPORTED,
               "Visible ROM breakpoint Go must refuse before execution when ROM patching is unavailable")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Unsupported visible ROM breakpoint must leave ROM untouched")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Unsupported visible ROM breakpoint must not patch RAM under ROM")) return 1;
    if (expect(m.rom_patch_writes == 0,
               "Unsupported visible ROM breakpoint must not attempt ROM writes")) return 1;
    if (expect(m.ram[FAKE_SENTINEL_ADDR] == 0x00,
               "Unsupported visible ROM breakpoint must refuse before starting execution")) return 1;
    return 0;
}

static int test_visible_rom_breakpoint_go_patches_rom_not_underlying_ram()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = true;
    m.ram[0xE001] = 0x6C;

    MonitorBreakpoints bps;
    bps.allocate(0xE001, 0x07);
    DebugContext from;
    debug_context_reset(&from);

    DebugSession::Result r = m.go(from, &bps, 0xC000);
    if (expect(r == DebugSession::DBG_OK,
               "Visible ROM breakpoint Go must run when ROM patching is supported")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Visible ROM breakpoint Go must restore the ROM byte")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Visible ROM breakpoint Go must not use RAM under ROM as proof")) return 1;
    if (expect(m.rom_patch_writes == 2,
               "Visible ROM breakpoint Go must patch and restore the ROM image")) return 1;
    return 0;
}

static int test_run_to_current_visible_kernal_jsr_enters_callee_before_rearming_cursor_breakpoint()
{
    FakeVisibleRomMachine m(true);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;
    m.kernal_rom[0x0000] = 0x20; // JSR $BC0F
    m.kernal_rom[0x0001] = 0x0F;
    m.kernal_rom[0x0002] = 0xBC;
    m.kernal_rom[0x0003] = 0xEA;
    m.basic_rom[0x1C0F] = 0xEA;
    fake_seed_captured_context(m, 0xBC0F, 0xFD, 0, 0, 0, 0x24);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE000;
    from.sp = 0xF7;
    from.sr = 0x24;

    DebugContext ctx;
    DebugSession::Result r = m.run_to(from, 0xE000, 0, 0xE000, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "K Cursor from the current visible KERNAL JSR must run successfully")) return 1;
    if (expect(m.brk_patch_writes >= 2,
               "K Cursor current-row JSR must patch the callee and then re-arm the cursor breakpoint")) return 1;
    if (expect(m.brk_patch_addrs[0] == 0xBC0F,
               "K Cursor current-row JSR must first arm the callee target, not the fall-through")) return 1;
    if (expect(fake_recorded_brk_patch(m, 0xE000),
               "K Cursor current-row JSR must re-arm the transient cursor breakpoint afterwards")) return 1;
    if (expect(!fake_recorded_brk_patch(m, 0xE003),
               "K Cursor current-row JSR must not step over to the fall-through address")) return 1;
    return 0;
}

static int test_overlay_step_over_never_freezes()
{
    // Overlay-mode non-regression: the machine is never frozen, so the engine
    // must neither unfreeze nor re-freeze, and must still complete the step.
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "overlay step-over must complete")) return 1;
    if (expect(!m.frozen, "overlay step-over must leave the machine unfrozen")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay step-over must not call the unfreeze hook")) return 1;
    if (expect(m.unfreeze_calls == 0, "overlay step-over must not unfreeze")) return 1;
    if (expect(m.refreeze_calls == 0, "overlay step-over must not re-freeze")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay step-over must not set screen_render_target_invalidated")) return 1;
    return 0;
}

static int test_overlay_accessible_unfrozen_step_over_does_not_unfreeze()
{
    // Hardware overlay access is not the same as freeze ownership. Even if a
    // backend reports that an unfreeze operation would be "accessible", the
    // run-window must not invoke it unless machine_is_frozen() was true.
    FakeFreezeMachine m(false);
    m.accessible_when_unfrozen = true;
    m.break_on_unfrozen_unfreeze_attempt = true;
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "overlay step-over must complete even if the unfreeze hook would be accessible")) return 1;
    if (expect(m.unfreeze_attempts == 0,
               "overlay run-window must not call unfreeze_if_accessible when not frozen")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay accessible/unfrozen step must not change freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay accessible/unfrozen step must not invalidate the render target")) return 1;
    return 0;
}

static int test_overlay_render_target_disables_stale_frozen_unfreeze()
{
    // The overlay UI is not the C64 freezer render target. If C64 accessibility
    // state is stale/true for any reason, the overlay run-window policy must
    // still prevent freeze ownership calls.
    FakeFreezeMachine m(true);
    m.set_run_window_refreeze_enabled(false);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "overlay render-target policy must still allow the BRK run")) return 1;
    if (expect(m.frozen,
               "overlay render-target policy must not change stale frozen state")) return 1;
    if (expect(m.unfreeze_attempts == 0,
               "overlay render-target policy must not call unfreeze even if machine_is_frozen is true")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay render-target policy must not touch freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay render-target policy must not invalidate the screen")) return 1;
    return 0;
}

static int test_overlay_step_into_never_freezes()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext c1;
    if (expect(m.trace_at(0x2000, pred, &c1) == DebugSession::DBG_OK,
               "overlay step-into must complete")) return 1;
    DebugContext c2;
    if (expect(m.trace_at(0x2000, pred, &c2) == DebugSession::DBG_OK,
               "overlay repeated step-into must complete")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay step-into must not call the unfreeze hook")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay step-into must never touch freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay step-into must not invalidate the render target")) return 1;
    return 0;
}

static int test_overlay_step_out_never_freezes()
{
    FakeFreezeMachine m(false);

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0x30;
    m.ram[0x2004] = 0xEA;
    m.ram[0x3000] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0x00, 0x30 };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    fake_seed_captured_context(m, 0x3000, 0xFB, 0, 0, 0, 0x24);
    DebugContext from;
    if (expect(m.trace_at(0x2001, pred, &from) == DebugSession::DBG_OK,
               "overlay trace into JSR must prepare a Step Out target")) return 1;
    m.unfreeze_attempts = 0;
    m.unfreeze_calls = 0;
    m.refreeze_calls = 0;
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.arm_capture_context(0x2004, 0xFD, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.step_out(from, &ctx);
    if (expect(r == DebugSession::DBG_OK, "overlay step-out must complete for a valid JSR frame")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay step-out must not call the unfreeze hook")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay step-out must never touch freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay step-out must not invalidate the render target")) return 1;
    return 0;
}

static int test_overlay_go_breakpoint_never_freezes()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);

    MonitorBreakpoints bps;
    bps.allocate(0x2001, 0x07);

    DebugContext from;
    debug_context_reset(&from);

    DebugSession::Result r = m.go(from, &bps, 0x2000);
    if (expect(r == DebugSession::DBG_OK, "overlay Go-to-breakpoint must complete")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay Go must not call the unfreeze hook")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay Go must never touch freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay Go must not invalidate the render target")) return 1;
    return 0;
}

// Two consecutive overlay steps must each complete and must never touch the
// freeze bracketing. A leaked run-window depth or a stale run_window_unfroze
// from step 1 would change step 2's behaviour (or, in freeze mode, skip the
// unfreeze); proving both steps run with zero unfreeze/refreeze and a clean
// invalidated flag is the strongest overlay non-regression we can model.
static int test_overlay_two_consecutive_steps_never_freeze()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext c1;
    if (expect(m.over_at(0x2000, pred, &c1) == DebugSession::DBG_OK,
               "overlay step 1 must complete")) return 1;
    DebugContext c2;
    if (expect(m.over_at(0x2000, pred, &c2) == DebugSession::DBG_OK,
               "overlay step 2 must complete (run-window depth recovered)")) return 1;
    if (expect(!m.frozen, "overlay steps must leave the machine unfrozen")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay steps must never call the unfreeze hook")) return 1;
    if (expect(m.unfreeze_calls == 0, "overlay steps must never unfreeze")) return 1;
    if (expect(m.refreeze_calls == 0, "overlay steps must never re-freeze")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay steps must not invalidate the render target")) return 1;
    return 0;
}

static int test_overlay_cursor_step_from_parked_spin_uses_release_path()
{
    FakeFreezeMachine m(false);
    m.nmi_from_spin_times_out = true;
    m.ram[0x2000] = 0xEE; // INC $D020
    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0xD0;
    m.ram[0x2003] = 0x4C; // JMP $2000
    m.ram[0x2004] = 0x00;
    m.ram[0x2005] = 0x20;

    uint8_t inc_bytes[3] = { 0xEE, 0x20, 0xD0 };
    DebugPredictResult inc_pred;
    debug_predict(0x2000, inc_bytes, false, &inc_pred);
    m.arm_capture_context(0x2003, 0xF7, 0, 0, 0, 0x24);
    DebugContext c1;
    if (expect(m.over_at(0x2000, inc_pred, &c1) == DebugSession::DBG_OK,
               "overlay INC step must park at JMP")) return 1;
    if (expect(c1.valid && c1.pc == 0x2003,
               "overlay INC step must capture $2003")) return 1;

    uint8_t jmp_bytes[3] = { 0x4C, 0x00, 0x20 };
    DebugPredictResult jmp_pred;
    debug_predict(0x2003, jmp_bytes, false, &jmp_pred);
    m.arm_capture_context(0x2000, 0xF7, 0, 0, 0, 0x24);
    DebugContext c2;
    if (expect(m.over_at(0x2003, jmp_pred, &c2) == DebugSession::DBG_OK,
               "cursor-driven overlay step from parked spin must not NMI-timeout")) return 1;
    if (expect(c2.valid && c2.pc == 0x2000,
               "overlay JMP step must capture $2000")) return 1;
    if (expect(!m.frozen, "overlay loop steps must leave the machine unfrozen")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay loop steps must never touch freeze state")) return 1;
    return 0;
}

// Two consecutive freeze steps must each unfreeze-then-refreeze exactly once.
// If step 1 leaked the run-window depth, step 2's outermost begin_run_window
// would not fire (depth != 0) and the second unfreeze/refreeze would be lost,
// dropping the counts below two. This pins the balance across steps.
static int test_freeze_two_consecutive_steps_rebalance()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.stamp_screen_sentinel(0x55, 40);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext c1;
    if (expect(m.over_at(0x2000, pred, &c1) == DebugSession::DBG_OK,
               "freeze step 1 must complete")) return 1;
    if (expect(m.frozen, "freeze step 1 must leave the machine re-frozen")) return 1;
    DebugContext c2;
    if (expect(m.over_at(0x2000, pred, &c2) == DebugSession::DBG_OK,
               "freeze step 2 must complete")) return 1;
    if (expect(m.frozen, "freeze step 2 must leave the machine re-frozen")) return 1;
    if (expect(m.unfreeze_calls == 2, "two freeze steps must unfreeze twice")) return 1;
    if (expect(m.refreeze_calls == 2, "two freeze steps must re-freeze twice")) return 1;
    if (expect(m.screen_sentinel_intact(0x55, 40),
               "freeze steps must never write into C64 screen RAM")) return 1;
    return 0;
}

// A timed-out overlay step must release control (DBG_TIMEOUT, not a hang) and
// leave the run-window depth at zero so a later step still works. It must also
// never freeze the machine on the failure path.
static int test_overlay_step_timeout_then_recovers()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    m.sentinel_armed = false; // force the wait loop to its timeout
    DebugContext c1;
    if (expect(m.over_at(0x2000, pred, &c1) == DebugSession::DBG_TIMEOUT,
               "overlay step must return DBG_TIMEOUT, never hang")) return 1;
    if (expect(!m.frozen, "overlay timeout must not freeze the machine")) return 1;
    if (expect(m.unfreeze_attempts == 0, "overlay timeout must not call the unfreeze hook")) return 1;
    if (expect(m.refreeze_calls == 0, "overlay timeout must not re-freeze")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay timeout must not invalidate the render target")) return 1;

    m.sentinel_armed = true; // engine recovers; depth must be back at zero
    DebugContext c2;
    if (expect(m.over_at(0x2000, pred, &c2) == DebugSession::DBG_OK,
               "overlay step after timeout must complete (depth recovered)")) return 1;
    if (expect(m.unfreeze_calls == 0 && m.refreeze_calls == 0,
               "overlay timeout+recovery must never touch freeze state")) return 1;
    if (expect(!m.screen_render_target_invalidated(),
               "overlay timeout+recovery must leave render-target invalidation clear")) return 1;
    return 0;
}

// A timed-out freeze step must still re-freeze on the failure path (so the
// monitor render target is preserved) and recover its run-window depth so the
// next step unfreezes/refreezes again.
static int test_freeze_step_timeout_refreezes_and_recovers()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2000);
    m.stamp_screen_sentinel(0x66, 40);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    m.sentinel_armed = false; // force timeout
    DebugContext c1;
    if (expect(m.over_at(0x2000, pred, &c1) == DebugSession::DBG_TIMEOUT,
               "freeze step must return DBG_TIMEOUT, never hang")) return 1;
    if (expect(m.frozen,
               "freeze timeout must re-freeze before returning to the monitor")) return 1;
    if (expect(m.unfreeze_attempts == 1 && m.unfreeze_calls == 1 && m.refreeze_calls == 1,
               "freeze timeout must balance its unfreeze with a re-freeze")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze timeout must invalidate the render target after refreeze")) return 1;

    m.sentinel_armed = true;
    DebugContext c2;
    if (expect(m.over_at(0x2000, pred, &c2) == DebugSession::DBG_OK,
               "freeze step after timeout must complete (depth recovered)")) return 1;
    if (expect(m.frozen, "freeze step after timeout must re-freeze")) return 1;
    if (expect(m.unfreeze_calls == 2 && m.refreeze_calls == 2,
               "freeze timeout+recovery must rebalance to two unfreeze/refreeze")) return 1;
    if (expect(m.screen_sentinel_intact(0x66, 40),
               "freeze timeout must never write into C64 screen RAM")) return 1;
    return 0;
}

static int test_cleanup_resume_trampoline_restores_full_context()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);
    fake_seed_captured_context(m, 0x2001, 0xF9, 0x42, 0x17, 0x99, 0xA4);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext next;
    if (expect(m.over_at(0x2000, pred, &next) == DebugSession::DBG_OK,
               "Setup step must succeed before cleanup")) return 1;

    m.cleanup();

    if (expect(m.ram[FAKE_SPIN_LO] == (uint8_t)(FAKE_RESUME_TRAMP & 0xFF) &&
               m.ram[FAKE_SPIN_HI] == (uint8_t)(FAKE_RESUME_TRAMP >> 8),
               "Cleanup must retarget the parked CPU to the resume trampoline")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 0] == 0xA2 &&
               m.ram[FAKE_RESUME_TRAMP + 1] == 0xF9 &&
               m.ram[FAKE_RESUME_TRAMP + 2] == 0x9A,
               "Cleanup trampoline must restore SP before returning")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 3] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 4] == 0x20 &&
               m.ram[FAKE_RESUME_TRAMP + 6] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 7] == 0x01 &&
               m.ram[FAKE_RESUME_TRAMP + 9] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 10] == 0xA4,
               "Cleanup trampoline must push PCH, PCL, then SR for RTI")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 6] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 7] == 0x01,
               "Cleanup trampoline must include the captured PC low byte")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 12] == 0xA0 &&
               m.ram[FAKE_RESUME_TRAMP + 13] == 0x99 &&
               m.ram[FAKE_RESUME_TRAMP + 14] == 0xA2 &&
               m.ram[FAKE_RESUME_TRAMP + 15] == 0x17 &&
               m.ram[FAKE_RESUME_TRAMP + 16] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 17] == 0x42 &&
               m.ram[FAKE_RESUME_TRAMP + 18] == 0x40,
               "Cleanup trampoline must restore Y, X, A and finish with RTI")) return 1;
    return 0;
}

// Regression for the step-mode stack side effect: the NMI used to launch a run
// pushes a 3-byte frame (PCH/PCL/SR). Because we resume with a JMP, that frame
// must be pulled back off inside the trampoline so the program's SP - and hence
// every subsequent push such as a JSR return address - is exactly what an
// undebugged run would have. The trampoline must therefore begin with PLA x3
// and end with a JMP to the launch target.
static int test_nmi_launch_trampoline_balances_stack()
{
    FakeFreezeMachine m(true); // freeze: first step launches via NMI
    fake_seed_nop_run(m, 0x2000); // NOP at 0x2000, fall-through 0x2001
    m.arm_capture_context(0x2001, 0xF7, 0, 0, 0, 0);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "NMI-launched step must complete")) return 1;
    if (expect(m.nmi_pulses == 1, "First step must launch via exactly one NMI")) return 1;

    // PLA x3 up front discards the NMI frame so SP is left balanced.
    if (expect(m.ram[FAKE_NMI_TRAMP + 0] == 0x68 &&
               m.ram[FAKE_NMI_TRAMP + 1] == 0x68 &&
               m.ram[FAKE_NMI_TRAMP + 2] == 0x68,
               "NMI trampoline must pull its 3-byte frame to keep SP balanced")) return 1;
    // The trampoline must still redirect to the launch target (JMP $2000).
    if (expect(m.ram[FAKE_NMI_TRAMP + 13] == 0x4C &&
               m.ram[FAKE_NMI_TRAMP + 14] == 0x00 &&
               m.ram[FAKE_NMI_TRAMP + 15] == 0x20,
               "NMI trampoline must JMP to the launch target after balancing SP")) return 1;
    return 0;
}

static int test_freeze_cleanup_after_step_allows_later_step()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2100);
    fake_seed_captured_context(m, 0x2101, 0xF3, 0x33, 0x44, 0x55, 0x66);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2100, bytes, false, &pred);

    DebugContext next;
    if (expect(m.over_at(0x2100, pred, &next) == DebugSession::DBG_OK,
               "Initial frozen step must succeed")) return 1;

    m.cleanup();

    if (expect(m.unfreeze_calls == 1 && m.refreeze_calls == 1,
               "Cleanup must preserve the balanced freeze run-window counts")) return 1;
    if (expect(m.ram[FAKE_SPIN_LO] == (uint8_t)(FAKE_RESUME_TRAMP & 0xFF) &&
               m.ram[FAKE_SPIN_HI] == (uint8_t)(FAKE_RESUME_TRAMP >> 8),
               "Frozen cleanup must stage a future resume path instead of leaving the spin loop stale")) return 1;

    fake_seed_nop_run(m, 0x2200);
    fake_seed_captured_context(m, 0x2201, 0xF2, 0x11, 0x22, 0x33, 0x44);
    debug_predict(0x2200, bytes, false, &pred);
    if (expect(m.over_at(0x2200, pred, &next) == DebugSession::DBG_OK,
               "A later frozen step must still work after cleanup")) return 1;
    return 0;
}

static int test_freeze_go_without_breakpoint_schedules_context_handoff()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_OK;
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xC123;
    backend.canned_snapshot.sp = 0xF1;
    backend.canned_snapshot.a = 0x11;
    backend.canned_snapshot.x = 0x22;
    backend.canned_snapshot.y = 0x33;
    backend.canned_snapshot.sr = 0x44;
    backend.write(0xC123, 0xEA);

    const int keys[] = { 'A', 'D', 'G' };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_debug_run_window_refreeze_enabled(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 1,
               "Freeze-mode G without breakpoints must unwind out of the monitor")) return 1;
    if (expect(backend.last_session && backend.last_session->go_calls == 0,
               "Freeze no-breakpoint G must not enter the BRK go path")) return 1;

    DebugContext go_ctx;
    bool has_ctx = false;
    uint16_t go_addr = 0;
    if (expect(monitor.consume_pending_go(&go_addr, &go_ctx, &has_ctx),
               "Freeze no-breakpoint G must schedule a pending handoff")) return 1;
    if (expect(has_ctx, "Freeze no-breakpoint G must preserve the captured CPU context")) return 1;
    if (expect(go_addr == 0xC123, "Pending handoff must target the captured PC")) return 1;
    if (expect(go_ctx.pc == 0xC123 && go_ctx.sp == 0xF1 &&
               go_ctx.a == 0x11 && go_ctx.x == 0x22 &&
               go_ctx.y == 0x33 && go_ctx.sr == 0x44,
               "Pending handoff must keep the full captured register state")) return 1;
    monitor.deinit();
    return 0;
}

static int test_freeze_go_with_breakpoint_still_uses_session_go()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x0801, 0xEA);

    const int keys[] = { 'A', 'D', 'R', 'G' };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_debug_run_window_refreeze_enabled(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Toggle breakpoint ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Freeze G with breakpoint should stay in monitor")) return 1;
    if (expect(backend.last_session && backend.last_session->go_calls == 1,
               "Freeze G with a breakpoint must still run through session->go")) return 1;
    if (expect(!monitor.consume_pending_go(NULL, NULL, NULL),
               "Breakpoint Go must not schedule the no-breakpoint exit path")) return 1;
    monitor.deinit();
    return 0;
}

static int test_wait_for_sentinel_drops_execution_keys()
{
    TestUserInterface ui;
    CaptureScreen screen;
    BrkSessionBackend backend(false);
    monitor_reset_saved_state();

    const int keys[] = { 'J', 'A', 'D', 'T', 'T', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("0801", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Initial trace should return 0")) return 1;
    if (expect(backend.last_session != NULL, "Trace must create the BRK session")) return 1;
    int brk_patches_after_trace = backend.last_session->brk_patch_writes;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug after the trace")) return 1;
    if (expect(backend.last_session->brk_patch_writes == brk_patches_after_trace,
               "Trace wait must drop repeated execution keys instead of replaying them")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_debug_ownership_blocks_second_stakeholder_until_remote_expires()
{
    FakeFreezeMachine telnet_owner(false);
    FakeFreezeMachine local_owner(false);

    set_fake_ms_timer(100);
    if (expect(telnet_owner.claim_debug_ownership(true),
               "Initial remote stakeholder must claim debug ownership")) return 1;
    advance_fake_ms_timer(1000);
    if (expect(!local_owner.claim_debug_ownership(false),
               "A second stakeholder must be blocked while the remote owner is still fresh")) return 1;
    advance_fake_ms_timer(2501);
    if (expect(local_owner.claim_debug_ownership(false),
               "A stale remote owner must expire so a new stakeholder can claim Debug")) return 1;
    local_owner.release_debug_ownership();
    telnet_owner.release_debug_ownership();
    return 0;
}

static int test_monitor_refuses_debug_when_owner_is_busy()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.session_claim_allowed = false;

    const int keys[] = { 'A', 'D', KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Blocked Debug entry should stay in the monitor")) return 1;
    if (expect(strcmp(ui.last_popup, "DEBUG IN USE") == 0,
               "Busy debug ownership must surface a clear popup")) return 1;
    if (expect(backend.last_session && backend.last_session->claim_calls == 1,
               "Debug entry must attempt to claim ownership exactly once")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP should exit once Debug was refused")) return 1;
    monitor.deinit();
    return 0;
}

static int test_cursor_row_highlights_jsr_target()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x1000, 0x20);
    backend.write(0x1001, 0x34);
    backend.write(0x1002, 0x12);
    backend.write(0x1003, 0xEA);
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x1000;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.a = 0x00;
    backend.canned_snapshot.x = 0x00;
    backend.canned_snapshot.y = 0x00;
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Breakpoint on JSR row should return 0")) return 1;

    char row[40];
    int row_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, "1000", 4) == 0) {
            row_y = y;
            break;
        }
    }
    if (expect(row_y >= 0, "Current JSR row must be visible")) return 1;
    const char *target = strstr(row, "$1234");
    if (expect(target != NULL, "JSR row must display the target address")) return 1;
    const char *label = strstr(row, "[BRK0]");
    if (expect(label != NULL, "JSR row with breakpoint must display the breakpoint label")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (int)(dbg - header)];
    int x = 1 + (int)(target - row);
    for (int i = 0; i < 5; i++) {
        if (expect(screen.colors[row_y][x + i] == accent,
                   "Current JSR target must use the accent colour")) return 1;
    }
    x = 1 + (int)(label - row);
    for (int i = 0; i < 6; i++) {
        if (expect(screen.colors[row_y][x + i] == accent,
                   "Breakpoint label must remain accented on a highlighted JSR row")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_cursor_row_highlights_jmp_target()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x1000, 0x4C);
    backend.write(0x1001, 0x34);
    backend.write(0x1002, 0x12);
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x1000;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;

    char row[40];
    int row_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, "1000", 4) == 0) {
            row_y = y;
            break;
        }
    }
    if (expect(row_y >= 0, "Current JMP row must be visible")) return 1;
    const char *target = strstr(row, "$1234");
    if (expect(target != NULL, "JMP row must display the target address")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (int)(dbg - header)];
    int x = 1 + (int)(target - row);
    for (int i = 0; i < 5; i++) {
        if (expect(screen.colors[row_y][x + i] == accent,
                   "Current JMP target must use the accent colour")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_cursor_row_highlights_taken_branch_target_only_when_taken()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x1000, 0xD0);
    backend.write(0x1001, 0x05);
    backend.write(0x1002, 0xEA);
    backend.write(0x1007, 0xEA);
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x1000;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.a = 0x00;
    backend.canned_snapshot.x = 0x00;
    backend.canned_snapshot.y = 0x00;
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;

    char row[40];
    int row_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, "1000", 4) == 0) {
            row_y = y;
            break;
        }
    }
    if (expect(row_y >= 0, "Current branch row must be visible")) return 1;
    const char *target = strstr(row, "$1007");
    if (expect(target != NULL, "Branch row must display the branch target")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (int)(dbg - header)];
    int x = 1 + (int)(target - row);
    for (int i = 0; i < 5; i++) {
        if (expect(screen.colors[row_y][x + i] == accent,
                   "Taken branch target must use the accent colour")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();

    backend.canned_snapshot.sr = 0x22;
    CaptureScreen not_taken_screen;
    FakeKeyboard not_taken_keyboard(keys, 4);
    ui.screen = &not_taken_screen;
    ui.keyboard = &not_taken_keyboard;
    BackendMachineMonitor not_taken_monitor(&ui, &backend);
    not_taken_monitor.init(&not_taken_screen, &not_taken_keyboard);
    if (expect(not_taken_monitor.poll(0) == 0, "ASM setup (not taken) should return 0")) return 1;
    if (expect(not_taken_monitor.poll(0) == 0, "Debug entry (not taken) should return 0")) return 1;
    row_y = -1;
    for (int y = 0; y < 25; y++) {
        not_taken_screen.get_slice(1, y, 38, row);
        if (strncmp(row, "1000", 4) == 0) {
            row_y = y;
            break;
        }
    }
    if (expect(row_y >= 0, "Current branch row (not taken) must be visible")) return 1;
    target = strstr(row, "$1007");
    if (expect(target != NULL, "Branch row (not taken) must display the target")) return 1;
    x = 1 + (int)(target - row);
    for (int i = 0; i < 5; i++) {
        if (expect(not_taken_screen.colors[row_y][x + i] == ui.color_fg,
                   "Untaken branch target must stay in the normal foreground colour")) return 1;
    }
    if (expect(not_taken_monitor.poll(0) == 0, "RUN/STOP should leave Debug (not taken)")) return 1;
    if (expect(not_taken_monitor.poll(0) == 1, "Final RUN/STOP should exit (not taken)")) return 1;
    not_taken_monitor.deinit();
    return 0;
}

// On the debug PC row, an RTS must show the return address inferred from the
// stack ($XXXX) and highlight it like any other jump/branch target.
static int test_debug_rts_shows_inferred_target()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x2000, 0x60); // RTS
    backend.write(0x2001, 0xEA);
    // Return frame on the stack: SP=0xFB, so RTS pulls $01FC/$01FD = $3322 and
    // resumes at $3322 + 1 = $3323.
    backend.write(0x01FC, 0x22);
    backend.write(0x01FD, 0x33);
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x2000;
    backend.canned_snapshot.sp = 0xFB;
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;

    char row[40];
    int row_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, "2000", 4) == 0) { row_y = y; break; }
    }
    if (expect(row_y >= 0, "RTS row must be visible")) return 1;
    if (expect(strstr(row, "RTS $3323") != NULL,
               "RTS must show the inferred return target")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (int)(dbg - header)];
    const char *target = strstr(row, "$3323");
    int x = 1 + (int)(target - row);
    for (int i = 0; i < 5; i++) {
        if (expect(screen.colors[row_y][x + i] == accent,
                   "RTS inferred target must use the accent colour")) return 1;
    }
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

// With an empty stack (SP=0xFF) there is no return address to infer, so RTS
// shows $???? instead.
static int test_debug_rts_empty_stack_shows_unknown()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x2000, 0x60); // RTS
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x2000;
    backend.canned_snapshot.sp = 0xFF; // empty stack
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;

    char row[40];
    int row_y = -1;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strncmp(row, "2000", 4) == 0) { row_y = y; break; }
    }
    if (expect(row_y >= 0, "RTS row must be visible")) return 1;
    if (expect(strstr(row, "RTS $????") != NULL,
               "RTS with empty stack must show $????")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

// The next-to-execute instruction (debug PC) is bracketed with > ... <, the
// marker follows the PC (not the movable cursor), and it is removed when Debug
// is left.
static int test_debug_next_opcode_bracket_markers()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    ui.color_fg = 12;

    backend.write(0x2000, 0x84); // STY $70
    backend.write(0x2001, 0x70);
    backend.write(0x2002, 0xEA); // NOP (next row, where the cursor will move)
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0x2000;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.sr = 0x20;

    const int keys[] = { 'A', 'D', KEY_DOWN, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;

    char row[40];

    if (expect(find_row_with_address(screen, "2000") >= 0, "PC row must be visible")) return 1;
    screen.get_slice(1, find_row_with_address(screen, "2000"), 38, row);
    if (expect(strstr(row, ">STY $70<") != NULL,
               "Debug PC row must bracket the next opcode with > and <")) return 1;

    // Move the cursor down; the marker must stay on the PC row, not follow it.
    if (expect(monitor.poll(0) == 0, "Cursor down should return 0")) return 1;
    if (expect(find_row_with_address(screen, "2000") >= 0, "PC row must still be visible")) return 1;
    screen.get_slice(1, find_row_with_address(screen, "2000"), 38, row);
    if (expect(strstr(row, ">STY $70<") != NULL,
               "Marker must stay on the PC row after the cursor moves")) return 1;
    if (expect(find_row_with_address(screen, "2002") >= 0, "Cursor row must be visible")) return 1;
    screen.get_slice(1, find_row_with_address(screen, "2002"), 38, row);
    if (expect(strchr(row, '>') == NULL && strchr(row, '<') == NULL,
               "The moved cursor row must not be bracketed")) return 1;

    // Leaving Debug removes the markup.
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave Debug")) return 1;
    if (expect(find_row_with_address(screen, "2000") >= 0, "PC row must be visible after leaving Debug")) return 1;
    screen.get_slice(1, find_row_with_address(screen, "2000"), 38, row);
    if (expect(strchr(row, '>') == NULL && strchr(row, '<') == NULL,
               "Bracket markup must be removed when Debug is left")) return 1;

    if (expect(monitor.poll(0) == 1, "Final RUN/STOP should exit")) return 1;
    monitor.deinit();
    return 0;
}

// --- Debug header/footer colour ---------------------------------------------
// Header mode/address tokens and breakpoint rows use color_fg.
// In Debug footer labels, PC/A/X/Y/SP and active flag letters use the primary
// accent; in the value row, PC/AC/XR/YR/SP and the SR bit string use the
// primary accent while IRQ/NMI stay in color_fg.

static int test_debug_footer_value_highlight_policy()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // Set a sentinel foreground (12) distinct from the primary accent so the
    // assertions are meaningful.
    ui.color_fg = 12;
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xC003;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.a = 0x01;
    backend.canned_snapshot.x = 0x00;
    backend.canned_snapshot.y = 0xFF;
    backend.canned_snapshot.sr = 0x35;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;

    // The Dbg badge colour is the white primary accent; sample it so the
    // assertions do not hard-code the accent constant.
    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (dbg - header)];

    // Locate the footer label row (contains PC/SP/AC labels).
    int label_y = -1;
    for (int y = 0; y < 25; y++) {
        char row[40];
        screen.get_slice(1, y, 38, row);
        if (strstr(row, "PC") != NULL && strstr(row, "SP") != NULL &&
            strstr(row, "AC") != NULL && strstr(row, "NV-BDIZC") != NULL) {
            label_y = y;
            break;
        }
    }
    if (expect(label_y >= 0, "Debug footer label row must be visible")) return 1;

    const int accent_positions[] = {
        MonitorDebug::FOOTER_POS_PC + 0,
        MonitorDebug::FOOTER_POS_PC + 1,
        MonitorDebug::FOOTER_POS_AC + 0,
        MonitorDebug::FOOTER_POS_XR + 0,
        MonitorDebug::FOOTER_POS_YR + 0,
        MonitorDebug::FOOTER_POS_SP + 0,
        MonitorDebug::FOOTER_POS_SP + 1,
        MonitorDebug::FOOTER_POS_FLAGS + 3,
        MonitorDebug::FOOTER_POS_FLAGS + 5,
        MonitorDebug::FOOTER_POS_FLAGS + 7,
    };
    for (unsigned i = 0; i < sizeof(accent_positions) / sizeof(accent_positions[0]); i++) {
        int x = 1 + accent_positions[i];
        if (expect(screen.colors[label_y][x] == accent,
                   "Requested Debug footer labels must use the accent")) return 1;
    }
    const int foreground_positions[] = {
        MonitorDebug::FOOTER_POS_AC + 1,
        MonitorDebug::FOOTER_POS_XR + 1,
        MonitorDebug::FOOTER_POS_YR + 1,
        MonitorDebug::FOOTER_POS_FLAGS + 0,
        MonitorDebug::FOOTER_POS_FLAGS + 1,
        MonitorDebug::FOOTER_POS_FLAGS + 2,
        MonitorDebug::FOOTER_POS_FLAGS + 4,
        MonitorDebug::FOOTER_POS_FLAGS + 6,
    };
    for (unsigned i = 0; i < sizeof(foreground_positions) / sizeof(foreground_positions[0]); i++) {
        int x = 1 + foreground_positions[i];
        if (expect(screen.colors[label_y][x] == ui.color_fg,
                   "Inactive flags and trailing register letters must stay in color_fg")) return 1;
    }
    for (int x = 1 + MonitorDebug::FOOTER_POS_IRQ;
         x < 1 + MonitorDebug::FOOTER_POS_IRQ + 3; x++) {
        if (expect(screen.colors[label_y][x] == ui.color_fg,
                   "IRQ label must stay in color_fg")) return 1;
    }
    for (int x = 1 + MonitorDebug::FOOTER_POS_NMI;
         x < 1 + MonitorDebug::FOOTER_POS_NMI + 3; x++) {
        if (expect(screen.colors[label_y][x] == ui.color_fg,
                   "NMI label must stay in color_fg")) return 1;
    }
    const int value_accent_ranges[][2] = {
        { MonitorDebug::FOOTER_POS_PC, 4 },
        { MonitorDebug::FOOTER_POS_AC, 2 },
        { MonitorDebug::FOOTER_POS_XR, 2 },
        { MonitorDebug::FOOTER_POS_YR, 2 },
        { MonitorDebug::FOOTER_POS_SP, 2 },
        { MonitorDebug::FOOTER_POS_FLAGS, 8 },
    };
    for (unsigned i = 0; i < sizeof(value_accent_ranges) / sizeof(value_accent_ranges[0]); i++) {
        int start = value_accent_ranges[i][0];
        int len = value_accent_ranges[i][1];
        for (int off = 0; off < len; off++) {
            int x = 1 + start + off;
            if (expect(screen.colors[label_y + 1][x] == accent,
                       "Requested Debug footer values must use the accent")) return 1;
        }
    }
    for (int x = 1 + MonitorDebug::FOOTER_POS_IRQ;
         x < 1 + MonitorDebug::FOOTER_POS_IRQ + 4; x++) {
        if (screen.chars[label_y + 1][x] == ' ') continue;
        if (expect(screen.colors[label_y + 1][x] == ui.color_fg,
                   "IRQ value must stay in color_fg")) return 1;
    }
    for (int x = 1 + MonitorDebug::FOOTER_POS_NMI;
         x < 1 + MonitorDebug::FOOTER_POS_NMI + 4; x++) {
        if (screen.chars[label_y + 1][x] == ' ') continue;
        if (expect(screen.colors[label_y + 1][x] == ui.color_fg,
                   "NMI value must stay in color_fg")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

static int test_debug_header_mode_and_address_use_foreground_color()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    // Set a sentinel foreground (12) distinct from the primary accent (1).
    ui.color_fg = 12;

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;

    char header[40];
    screen.get_slice(1, 3, 38, header);
    // The Dbg badge uses the white primary accent; sample it as the reference.
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must still show the Dbg badge")) return 1;
    uint8_t accent = screen.colors[3][1 + (dbg - header)];
    if (expect(accent != ui.color_fg,
               "Primary accent and color_fg must be distinct")) return 1;

    // The view-mode token and the current-address token in the header use
    // color_fg; only the Dbg badge uses the white primary accent.
    const char *mode = strstr(header, "ASM");
    if (expect(mode != NULL, "Header must show the ASM mode token")) return 1;
    const char *addr = strstr(header, "$");
    if (expect(addr != NULL, "Header must show the current address")) return 1;

    int mode_x = 1 + (int)(mode - header);
    for (int i = 0; i < 3; i++) {
        if (expect(screen.colors[3][mode_x + i] == ui.color_fg,
                   "Header mode text must use color_fg")) return 1;
        if (expect(screen.colors[3][mode_x + i] != accent,
                   "Header mode text must not use the white primary accent")) return 1;
    }
    int addr_x = 1 + (int)(addr - header);
    for (int i = 0; i < 5; i++) {
        if (expect(screen.colors[3][addr_x + i] == ui.color_fg,
                   "Header address text must use color_fg")) return 1;
    }

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();
    return 0;
}

} // namespace

#define RUN(name) do { fprintf(stderr, "RUN " #name "\n"); if (name()) { fprintf(stderr, "FAIL " #name "\n"); return 1; } } while(0)

int main()
{
    RUN(test_predictor_classifies_control_flow);
    RUN(test_breakpoint_slots_allocate_clear_and_format);
    RUN(test_footer_layout_blanks_unknown_fields);
    RUN(test_debug_footer_sits_above_normal_status);
    RUN(test_d_enters_debug_without_executing);
    RUN(test_d_inside_debug_performs_over);
    RUN(test_d_inside_debug_without_context_overs_from_cursor);
    RUN(test_d_inside_debug_without_context_goto_keeps_cursor_authoritative);
    RUN(test_d_refuses_undocumented_opcode_with_specific_warning);
    RUN(test_debug_predictor_uses_session_bytes_over_backend_reads);
    RUN(test_t_traces_and_o_outs_inside_debug);
    RUN(test_o_without_traced_jsr_reports_not_in_subroutine);
    RUN(test_o_without_context_reports_not_in_subroutine);
    RUN(test_t_inside_debug_without_context_traces_from_cursor);
    RUN(test_g_invalidates_context);
    RUN(test_return_remains_non_executing_navigation);
    RUN(test_ctrl_d_leaves_debug_only);
    RUN(test_ctrl_d_leaves_debug_but_keeps_edit);
    RUN(test_escape_leaves_edit_before_debug);
    RUN(test_runstop_leaves_edit_before_debug);
    RUN(test_step_from_memory_view_recenters_asm_on_debug_pc);
    RUN(test_ctrl_x_resets_and_keeps_debug_open);
    RUN(test_ctrl_x_local_reset_exits_monitor);
    RUN(test_ctrl_x_reenters_monitor_without_debug_when_not_debugging);
    RUN(test_breakpoints_survive_ctrl_x_reset_reentry);
    RUN(test_breakpoints_survive_normal_close_reopen);
    RUN(test_monitor_reset_saved_state_clears_breakpoints);
    RUN(test_breakpoint_toggle_via_r);
    RUN(test_breakpoint_popup_store_reuses_selected_slot);
    RUN(test_breakpoint_popup_digit_jumps_to_slot);
    RUN(test_breakpoint_row_indicator_and_color);
    RUN(test_disabled_breakpoint_row_uses_regular_color);
    RUN(test_breakpoint_label_replaces_row_indicator);
    RUN(test_source_indicators_are_three_chars);
    RUN(test_ctrl_r_opens_breakpoint_popup);
    RUN(test_ctrl_r_opens_breakpoint_popup_outside_debug);
    RUN(test_breakpoint_popup_navigation_redraws_only_popup);
    RUN(test_debug_pc_viewport_keeps_context_margin);
    RUN(test_breakpoint_popup_blocks_debug_execution_keys);
    RUN(test_unsupported_session_emits_refusal);
    RUN(test_b_keeps_binary_view_in_debug);
    RUN(test_ctrl_b_keeps_bookmark_popup_in_debug);
    RUN(test_cleanup_runs_on_deinit);
    RUN(test_ctrl_o_after_debug_step_closes_without_go_handoff);
    RUN(test_stop_debugging_clears_stale_go_handoff);
    RUN(test_stop_debugging_resumes_current_debug_context);
    RUN(test_help_screen_shows_debug_commands);
    RUN(test_k_runs_to_cursor);
    RUN(test_k_without_context_runs_from_debug_entry_to_cursor);
    RUN(test_ctrl_i_swaps_interface_in_monitor);
    RUN(test_jump_rejects_non_hex_input_and_uppercases);
    RUN(test_edit_and_debug_compose_in_header);
    RUN(test_freeze_step_over_refreezes_and_preserves_screen);
    RUN(test_freeze_nmi_step_ignores_stale_sentinel);
    RUN(test_freeze_step_over_with_context_refreezes);
    RUN(test_cleanup_to_context_without_entry_context_never_resets);
    RUN(test_freeze_step_into_refreezes);
    RUN(test_freeze_step_out_refreezes);
    RUN(test_step_out_uses_traced_jsr_target_even_when_target_is_above_pc);
    RUN(test_step_out_ignores_nearby_rts_and_patches_after_traced_jsr);
    RUN(test_step_out_refuses_stale_far_stack_frame);
    RUN(test_over_rts_refuses_non_jsr_stack_target);
    RUN(test_traced_rts_uses_recorded_return_target_when_stack_is_unreliable);
    RUN(test_traced_kernal_to_basic_step_out_patches_kernal_return);
    RUN(test_freeze_go_breakpoint_refreezes);
    RUN(test_go_from_current_breakpoint_stops_at_callee_breakpoint);
    RUN(test_go_from_current_visible_rom_breakpoint_uses_patch_aware_bytes);
    RUN(test_step_over_stops_at_callee_breakpoint);
    RUN(test_visible_basic_step_uses_rom_patch_support);
    RUN(test_visible_rom_step_bytes_use_cpu_visible_mapping);
    RUN(test_u64_debug_cpu_port_uses_monitor_bank);
    RUN(test_cursor_visible_rom_step_sets_monitor_cpu_port_before_jump);
    RUN(test_visible_kernal_step_into_uses_rom_patch_support);
    RUN(test_visible_kernal_step_over_jsr_patches_fallthrough_rom);
    RUN(test_visible_kernal_step_without_rom_patch_support_refuses_cleanly);
    RUN(test_visible_rom_breakpoint_go_uses_same_capability_gate);
    RUN(test_visible_rom_breakpoint_go_patches_rom_not_underlying_ram);
    RUN(test_run_to_current_visible_kernal_jsr_enters_callee_before_rearming_cursor_breakpoint);
    RUN(test_overlay_step_over_never_freezes);
    RUN(test_overlay_accessible_unfrozen_step_over_does_not_unfreeze);
    RUN(test_overlay_render_target_disables_stale_frozen_unfreeze);
    RUN(test_overlay_step_into_never_freezes);
    RUN(test_overlay_step_out_never_freezes);
    RUN(test_overlay_go_breakpoint_never_freezes);
    RUN(test_overlay_two_consecutive_steps_never_freeze);
    RUN(test_overlay_cursor_step_from_parked_spin_uses_release_path);
    RUN(test_freeze_two_consecutive_steps_rebalance);
    RUN(test_overlay_step_timeout_then_recovers);
    RUN(test_freeze_step_timeout_refreezes_and_recovers);
    RUN(test_cleanup_resume_trampoline_restores_full_context);
    RUN(test_nmi_launch_trampoline_balances_stack);
    RUN(test_freeze_cleanup_after_step_allows_later_step);
    RUN(test_freeze_go_without_breakpoint_schedules_context_handoff);
    RUN(test_freeze_go_with_breakpoint_still_uses_session_go);
    RUN(test_wait_for_sentinel_drops_execution_keys);
    RUN(test_debug_ownership_blocks_second_stakeholder_until_remote_expires);
    RUN(test_monitor_refuses_debug_when_owner_is_busy);
    RUN(test_cursor_row_highlights_jsr_target);
    RUN(test_cursor_row_highlights_jmp_target);
    RUN(test_cursor_row_highlights_taken_branch_target_only_when_taken);
    RUN(test_debug_rts_shows_inferred_target);
    RUN(test_debug_rts_empty_stack_shows_unknown);
    RUN(test_debug_next_opcode_bracket_markers);
    RUN(test_debug_footer_value_highlight_policy);
    RUN(test_debug_header_mode_and_address_use_foreground_color);

    puts("machine_monitor_debug_test: OK");
    return 0;
}
