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
#include "monitor_debug_interpreter_vectors.h"

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
    int reset_cancel_calls;
    int snapshot_calls;
    int claim_calls;
    int refresh_calls;
    int release_calls;
    uint16_t last_start_pc;
    uint16_t last_run_to_target;
    bool last_go_from_valid;
    uint16_t last_go_from_pc;
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
    bool go_produces_snapshot;
    bool parked_context_handoff_supported;
    bool parked_context;

    StubDebugSession()
        : over_calls(0), over_at_calls(0),
          over_breakpoint_calls(0), over_at_breakpoint_calls(0),
          trace_calls(0), trace_at_calls(0),
          out_calls(0), out_breakpoint_calls(0),
          go_calls(0), run_to_calls(0), cleanup_calls(0),
          cleanup_to_context_calls(0), reset_cancel_calls(0),
          snapshot_calls(0), claim_calls(0),
          refresh_calls(0), release_calls(0),
          last_start_pc(0), last_run_to_target(0),
          last_go_from_valid(false), last_go_from_pc(0),
          predict_bytes_valid(false),
          over_result(DBG_OK), trace_result(DBG_OK),
          out_result(DBG_OK), go_result(DBG_OK), run_to_result(DBG_OK),
          snapshot_result(DBG_OK), claim_allowed(true),
          go_produces_snapshot(false),
          parked_context_handoff_supported(false),
          parked_context(false)
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
    virtual Result go(const DebugContext &from, const MonitorBreakpoints *, uint16_t start_pc) {
        go_calls++;
        last_start_pc = start_pc;
        last_go_from_valid = from.valid;
        last_go_from_pc = from.pc;
        // Model the real session's semantic: Go invalidates the previous
        // capture. If a stop happens during this Go, a real session would
        // refresh `next_ctx`; the stub simulates "no stop happened" by
        // marking snapshot unavailable until the next over/trace/out call
        // explicitly captures something.
        if (snapshot_result == DBG_OK && !go_produces_snapshot) {
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
    virtual void request_reset_cancel(void) {
        reset_cancel_calls++;
    }
    virtual bool has_parked_context_handoff(void) const {
        return parked_context_handoff_supported;
    }
    virtual bool has_parked_context(void) const {
        return parked_context;
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
    bool go_produces_snapshot;
    bool parked_context_handoff_supported;

    TrackingDebugBackend() : last_session(NULL), session_creations(0),
                             refuse_session(false), reset_calls(0),
                             allow_reset(true), canned_snapshot_set(false),
                             snapshot_result(DebugSession::DBG_OK),
                             session_claim_allowed(true),
                             go_produces_snapshot(false),
                             parked_context_handoff_supported(false)
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
        last_session->go_produces_snapshot = go_produces_snapshot;
        last_session->parked_context_handoff_supported = parked_context_handoff_supported;
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
            default: break;
        }
        if (address >= 0xD000 && address <= 0xDFFF) {
            return "IO";
        }
        if (address >= 0xE000) {
            return "RAM";
        }
        return "RAM";
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
static const uint16_t FAKE_STORE_CPU_DDR = 0x03F8;
static const uint16_t FAKE_STORE_CPU_PORT = 0x03F9;
static const uint16_t FAKE_STORE_TRAP_MODE = 0x03FA;
static const uint16_t FAKE_STORE_HARD_CPU_DDR = 0x03FB;
static const uint16_t FAKE_STORE_HARD_CPU_PORT = 0x03ED;
static const uint16_t FAKE_HARD_BRK_ORIG_VECTOR_LO = 0x03EE;
static const uint16_t FAKE_DEBUG_AREA_START = 0x033C;
static const uint16_t FAKE_DEBUG_AREA_END = 0x03FB;
static const uint16_t FAKE_HANDLER_ADDR   = 0x035D;
static const uint16_t FAKE_HANDLER_LEN    = 45;
static const uint16_t FAKE_SPIN_JMP      = 0x0387;
static const uint16_t FAKE_SPIN_LO       = 0x0388;
static const uint16_t FAKE_SPIN_HI       = 0x0389;
static const uint16_t FAKE_RESUME_TRAMP  = FAKE_HANDLER_ADDR;
static const uint16_t FAKE_TRAMPOLINE    = 0x038A;
static const uint16_t FAKE_TRAMPOLINE_LEN = 38;
static const uint16_t FAKE_NMI_VECTOR_HI = 0x0319; // mirrors NMI_VECTOR_HI
static const uint16_t FAKE_NMI_TRAMP     = 0x03B0; // mirrors NMI_TRAMPOLINE_ADDR
static const uint16_t FAKE_NMI_TRAMP_LEN = 24;
static const uint16_t FAKE_HARD_BRK_STUB = 0x03C8; // mirrors HARD_BRK_STUB_ADDR
static const uint16_t FAKE_HARD_BRK_STUB_LEN = 37;
static const uint16_t FAKE_HARD_NMI_VECTOR_LO = 0xFFFA;
static const uint16_t FAKE_HARD_NMI_VECTOR_HI = 0xFFFB;
static const uint16_t FAKE_HARD_VECTOR_LO = 0xFFFE;
static const uint16_t FAKE_HARD_VECTOR_HI = 0xFFFF;

class FakeFreezeMachine : public BrkDebugSession
{
public:
    enum { MAX_RECORDED_BRK_PATCHES = 32 };
    enum { MAX_RECORDED_FREEZE_RESTORE_POKES = 160 };

    uint8_t ram[65536];
    bool frozen;
    bool accessible_when_unfrozen;
    bool break_on_unfrozen_unfreeze_attempt;
    int unfreeze_attempts;
    int unfreeze_calls;
    int refreeze_calls;
    int reset_calls;
    int nmi_pulses;
    int staged_nmi_requests;
    int staged_nmi_clears;
    int brk_patch_writes;
    int delay_calls;
    int pokes_at_cancel;
    uint16_t last_brk_patch_addr;
    uint16_t brk_patch_addrs[MAX_RECORDED_BRK_PATCHES];
    int freeze_restore_pokes;
    uint16_t freeze_restore_addrs[MAX_RECORDED_FREEZE_RESTORE_POKES];
    uint8_t freeze_restore_bytes[MAX_RECORDED_FREEZE_RESTORE_POKES];
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
    bool reset_cancel_on_delay;
    bool monitor_reset_cancel_on_delay;
    MachineMonitor *monitor_reset_cancel_target;
    bool capture_override_armed;
    uint16_t capture_override_pc;
    uint8_t capture_override_sp;
    uint8_t capture_override_a;
    uint8_t capture_override_x;
    uint8_t capture_override_y;
    uint8_t capture_override_sr;
    uint8_t capture_override_trap_mode;
    uint8_t capture_override_hard_cpu_ddr;
    uint8_t capture_override_hard_cpu_port;

    FakeFreezeMachine(bool start_frozen)
        : frozen(start_frozen), accessible_when_unfrozen(false),
          break_on_unfrozen_unfreeze_attempt(false), unfreeze_attempts(0),
          unfreeze_calls(0), refreeze_calls(0), reset_calls(0), nmi_pulses(0),
          staged_nmi_requests(0), staged_nmi_clears(0),
          brk_patch_writes(0), delay_calls(0), pokes_at_cancel(0),
          last_brk_patch_addr(0),
          freeze_restore_pokes(0),
          sentinel_armed(true), stale_sentinel_during_nmi_setup(false),
          nmi_from_spin_times_out(false), reset_cancel_on_delay(false),
          monitor_reset_cancel_on_delay(false),
          monitor_reset_cancel_target(NULL),
          capture_override_armed(false),
          capture_override_pc(0), capture_override_sp(0),
          capture_override_a(0), capture_override_x(0), capture_override_y(0),
          capture_override_sr(0), capture_override_trap_mode(0),
          capture_override_hard_cpu_ddr(0), capture_override_hard_cpu_port(0)
    {
        memset(ram, 0, sizeof(ram));
        memset(brk_patch_addrs, 0, sizeof(brk_patch_addrs));
        memset(freeze_restore_addrs, 0, sizeof(freeze_restore_addrs));
        memset(freeze_restore_bytes, 0, sizeof(freeze_restore_bytes));
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

    void reset_freeze_restore_pokes(void)
    {
        freeze_restore_pokes = 0;
        memset(freeze_restore_addrs, 0, sizeof(freeze_restore_addrs));
        memset(freeze_restore_bytes, 0, sizeof(freeze_restore_bytes));
    }

    bool recorded_freeze_restore_poke(uint16_t address, uint8_t byte) const
    {
        int n = freeze_restore_pokes;
        if (n > MAX_RECORDED_FREEZE_RESTORE_POKES) {
            n = MAX_RECORDED_FREEZE_RESTORE_POKES;
        }
        for (int i = 0; i < n; i++) {
            if (freeze_restore_addrs[i] == address &&
                    freeze_restore_bytes[i] == byte) {
                return true;
            }
        }
        return false;
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
        capture_override_trap_mode = 0;
        capture_override_hard_cpu_ddr = 0;
        capture_override_hard_cpu_port = 0;
    }

    void arm_hard_vector_capture_context(uint16_t pc, uint8_t sp, uint8_t a,
                                         uint8_t x, uint8_t y, uint8_t sr,
                                         uint8_t cpu_ddr, uint8_t cpu_port)
    {
        arm_capture_context(pc, sp, a, x, y, sr);
        capture_override_trap_mode = 0xA5;
        capture_override_hard_cpu_ddr = cpu_ddr;
        capture_override_hard_cpu_port = cpu_port;
    }

protected:
    virtual bool backend_ready(void) const { return true; }
    virtual uint8_t current_cpu_port(void) const { return 0x07; }
    virtual bool begin_stopped_session(void) { return false; }
    virtual void end_stopped_session(bool) { }
    virtual uint8_t peek_cpu(uint16_t a, uint8_t) { return ram[a]; }
    virtual void poke_cpu(uint16_t a, uint8_t b, uint8_t)
    {
        // $0000/$0001 are the 6510-port RAM mirror the capture refreshes;
        // they are never BRK patch sites, so keep them out of the counter.
        if (b == 0x00 &&
                a > 0x0001 &&
                a != FAKE_HARD_NMI_VECTOR_LO &&
                a != FAKE_HARD_NMI_VECTOR_HI &&
                a != FAKE_HARD_VECTOR_LO &&
                a != FAKE_HARD_VECTOR_HI) {
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
    virtual void poke_visible_preserving_freeze_restore(uint16_t a, uint8_t b)
    {
        if (freeze_restore_pokes < MAX_RECORDED_FREEZE_RESTORE_POKES) {
            freeze_restore_addrs[freeze_restore_pokes] = a;
            freeze_restore_bytes[freeze_restore_pokes] = b;
        }
        freeze_restore_pokes++;
        poke_visible(a, b);
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
            ram[FAKE_SPIN_LO] == (uint8_t)(FAKE_SPIN_JMP & 0xFF) &&
            ram[FAKE_SPIN_HI] == (uint8_t)(FAKE_SPIN_JMP >> 8)) {
            sentinel_armed = false;
        }
    }
    virtual void request_staged_nmi(void)
    {
        nmi_pulses++;
        staged_nmi_requests++;
    }
    virtual void clear_staged_nmi(void)
    {
        staged_nmi_clears++;
    }
    virtual void delay_ms(int)
    {
        delay_calls++;
        if (monitor_reset_cancel_on_delay && monitor_reset_cancel_target) {
            monitor_reset_cancel_target->request_debug_reset_cancel();
            pokes_at_cancel = freeze_restore_pokes;
            monitor_reset_cancel_on_delay = false;
            return;
        }
        if (reset_cancel_on_delay) {
            request_reset_cancel();
            pokes_at_cancel = freeze_restore_pokes;
            reset_cancel_on_delay = false;
            return;
        }
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
                ram[FAKE_STORE_TRAP_MODE] = capture_override_trap_mode;
                ram[FAKE_STORE_HARD_CPU_DDR] = capture_override_hard_cpu_ddr;
                ram[FAKE_STORE_HARD_CPU_PORT] = capture_override_hard_cpu_port;
                capture_override_trap_mode = 0;
                capture_override_hard_cpu_ddr = 0;
                capture_override_hard_cpu_port = 0;
                capture_override_armed = false;
            } else if (brk_patch_writes > 0) {
                uint16_t captured_pc = (uint16_t)(last_brk_patch_addr + 2);
                ram[FAKE_STORE_PCLO] = (uint8_t)(captured_pc & 0xFF);
                ram[FAKE_STORE_PCHI] = (uint8_t)(captured_pc >> 8);
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
    bool fetch_lags;
    int rom_patch_writes;
    uint16_t last_rom_patch_addr;
    int ram_patch_writes;
    uint16_t last_ram_patch_addr;
    int settle_calls;
    int sustained_settle_calls;
    bool last_settle_sustained;
    bool saw_sustained_settle;
    uint8_t settle_spin_lo;
    uint8_t settle_spin_hi;
    bool switch_cpu_port_on_delay;
    uint8_t cpu_port_after_delay;

    FakeVisibleRomMachine(bool start_frozen)
        : FakeFreezeMachine(start_frozen), cpu_port(0x07),
          allow_visible_rom_patching(false), force_raw_peek_brk(false),
          force_raw_peek_brk_addr(0), fetch_lags(false),
          rom_patch_writes(0), last_rom_patch_addr(0),
          ram_patch_writes(0), last_ram_patch_addr(0),
          settle_calls(0), sustained_settle_calls(0),
          last_settle_sustained(false),
          saw_sustained_settle(false),
          settle_spin_lo(0), settle_spin_hi(0),
          switch_cpu_port_on_delay(false), cpu_port_after_delay(0x07)
    {
        memset(basic_rom, 0, sizeof(basic_rom));
        memset(kernal_rom, 0, sizeof(kernal_rom));
        memset(char_rom, 0, sizeof(char_rom));
    }

    void mark_visible_rom_hit_for_test(uint16_t pc)
    {
        test_mark_visible_rom_breakpoint_hit(pc);
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
    virtual bool visible_rom_fetch_lags(void) const
    {
        return fetch_lags;
    }
    virtual void settle_visible_rom_for_live_fetch(bool sustained)
    {
        settle_calls++;
        last_settle_sustained = sustained;
        if (sustained) {
            saw_sustained_settle = true;
            sustained_settle_calls++;
            settle_spin_lo = ram[FAKE_SPIN_LO];
            settle_spin_hi = ram[FAKE_SPIN_HI];
        }
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
    virtual void poke_cpu(uint16_t a, uint8_t b, uint8_t patch_cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(a, patch_cpu_port);
        if (rom && allow_visible_rom_patching) {
            *rom = b;
            return;
        }
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
        if (b == 0x00) {
            if (brk_patch_writes < MAX_RECORDED_BRK_PATCHES) {
                brk_patch_addrs[brk_patch_writes] = a;
            }
            brk_patch_writes++;
            last_brk_patch_addr = a;
            ram_patch_writes++;
            last_ram_patch_addr = a;
        }
        poke_cpu(a, b, patch_cpu_port);
    }
    virtual void delay_ms(int ms)
    {
        if (switch_cpu_port_on_delay) {
            cpu_port = (uint8_t)(cpu_port_after_delay & 0x07);
            switch_cpu_port_on_delay = false;
        }
        FakeFreezeMachine::delay_ms(ms);
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
    m.ram[FAKE_STORE_CPU_DDR] = 0x07;
    m.ram[FAKE_STORE_CPU_PORT] = 0x07;
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

struct Mini6510 {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t sr;
    uint16_t pc;
    bool zero;
    bool rti;
};

static uint16_t mini_abs(const uint8_t *mem, uint16_t pc)
{
    return (uint16_t)(mem[pc] | (mem[(uint16_t)(pc + 1)] << 8));
}

static void mini_set_zn(Mini6510 &cpu, uint8_t value)
{
    cpu.zero = (value == 0);
}

static void mini_push(uint8_t *mem, Mini6510 &cpu, uint8_t value)
{
    mem[(uint16_t)(0x0100 | cpu.sp)] = value;
    cpu.sp--;
}

static uint8_t mini_pull(uint8_t *mem, Mini6510 &cpu)
{
    cpu.sp++;
    return mem[(uint16_t)(0x0100 | cpu.sp)];
}

static bool mini_run(uint8_t *mem, Mini6510 &cpu, uint16_t stop_pc, int max_steps)
{
    for (int steps = 0; steps < max_steps; steps++) {
        if (steps > 0 && cpu.pc == stop_pc) {
            return true;
        }
        uint8_t op = mem[cpu.pc++];
        switch (op) {
            case 0x29:
                cpu.a &= mem[cpu.pc++];
                mini_set_zn(cpu, cpu.a);
                break;
            case 0x40:
                cpu.sr = mini_pull(mem, cpu);
                cpu.pc = mini_pull(mem, cpu);
                cpu.pc = (uint16_t)(cpu.pc | (mini_pull(mem, cpu) << 8));
                cpu.rti = true;
                return true;
            case 0x48:
                mini_push(mem, cpu, cpu.a);
                break;
            case 0x4C:
                cpu.pc = mini_abs(mem, cpu.pc);
                break;
            case 0x68:
                cpu.a = mini_pull(mem, cpu);
                mini_set_zn(cpu, cpu.a);
                break;
            case 0x6C: {
                uint16_t ptr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(mem[ptr] | (mem[(uint16_t)(ptr + 1)] << 8));
                break;
            }
            case 0x85:
                mem[mem[cpu.pc++]] = cpu.a;
                break;
            case 0x8A:
                cpu.a = cpu.x;
                mini_set_zn(cpu, cpu.a);
                break;
            case 0x8D: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                mem[addr] = cpu.a;
                break;
            }
            case 0x8E: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                mem[addr] = cpu.x;
                break;
            }
            case 0x98:
                cpu.a = cpu.y;
                mini_set_zn(cpu, cpu.a);
                break;
            case 0x9A:
                cpu.sp = cpu.x;
                break;
            case 0x99: {
                uint16_t addr = (uint16_t)(mini_abs(mem, cpu.pc) + cpu.y);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                mem[addr] = cpu.a;
                break;
            }
            case 0x9D: {
                uint16_t addr = (uint16_t)(mini_abs(mem, cpu.pc) + cpu.x);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                mem[addr] = cpu.a;
                break;
            }
            case 0xA0:
                cpu.y = mem[cpu.pc++];
                mini_set_zn(cpu, cpu.y);
                break;
            case 0xA5:
                cpu.a = mem[mem[cpu.pc++]];
                mini_set_zn(cpu, cpu.a);
                break;
            case 0xA9:
                cpu.a = mem[cpu.pc++];
                mini_set_zn(cpu, cpu.a);
                break;
            case 0xAA:
                cpu.x = cpu.a;
                mini_set_zn(cpu, cpu.x);
                break;
            case 0xAC: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                cpu.y = mem[addr];
                mini_set_zn(cpu, cpu.y);
                break;
            }
            case 0xAD: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                cpu.a = mem[addr];
                mini_set_zn(cpu, cpu.a);
                break;
            }
            case 0xAE: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                cpu.x = mem[addr];
                mini_set_zn(cpu, cpu.x);
                break;
            }
            case 0xB9: {
                uint16_t addr = (uint16_t)(mini_abs(mem, cpu.pc) + cpu.y);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                cpu.a = mem[addr];
                mini_set_zn(cpu, cpu.a);
                break;
            }
            case 0xBA:
                cpu.x = cpu.sp;
                mini_set_zn(cpu, cpu.x);
                break;
            case 0xBD: {
                uint16_t addr = (uint16_t)(mini_abs(mem, cpu.pc) + cpu.x);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                cpu.a = mem[addr];
                mini_set_zn(cpu, cpu.a);
                break;
            }
            case 0xC0:
                cpu.zero = (cpu.y == mem[cpu.pc++]);
                break;
            case 0xC8:
                cpu.y++;
                mini_set_zn(cpu, cpu.y);
                break;
            case 0xD0: {
                int8_t rel = (int8_t)mem[cpu.pc++];
                if (!cpu.zero) {
                    cpu.pc = (uint16_t)(cpu.pc + rel);
                }
                break;
            }
            case 0xE8:
                cpu.x++;
                mini_set_zn(cpu, cpu.x);
                break;
            case 0xEE: {
                uint16_t addr = mini_abs(mem, cpu.pc);
                cpu.pc = (uint16_t)(cpu.pc + 2);
                mem[addr]++;
                mini_set_zn(cpu, mem[addr]);
                break;
            }
            case 0xEA:
                break;
            default:
                return false;
        }
    }
    return false;
}

static bool fake_install_debug_stubs(FakeFreezeMachine &m, uint16_t addr)
{
    fake_seed_nop_run(m, addr);
    fake_seed_captured_context(m, (uint16_t)(addr + 1), 0xF8,
                               0x11, 0x22, 0x33, 0x24);
    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(addr, bytes, false, &pred);
    DebugContext ctx;
    return m.trace_at(addr, pred, &ctx) == DebugSession::DBG_OK;
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

static int debug_visible_disasm_exists(CaptureScreen &screen,
                                       const char *prefix,
                                       const char *needle)
{
    char row[40];

    for (int y = 4; y <= 22; y++) {
        screen.get_slice(1, y, 38, row);
        if ((!prefix || strstr(row, prefix) == row) &&
            (!needle || strstr(row, needle) != NULL)) {
            return 1;
        }
    }
    return 0;
}

static int expect_debug_visible_disasm(CaptureScreen &screen,
                                       const char *prefix,
                                       const char *needle,
                                       const char *message)
{
    return expect(debug_visible_disasm_exists(screen, prefix, needle), message);
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
    int krn = bps.allocate(0xE000, 0x07);
    if (expect(krn == 2, "KERNAL breakpoint must allocate a distinct slot")) return 1;
    int ram = bps.allocate(0xE000, 0x05);
    if (expect(ram == 3, "RAM-under-KERNAL breakpoint at same address must coexist")) return 1;
    if (expect(bps.find_at(0xE000, MONITOR_BACKING_KERNAL) == krn,
               "KERNAL breakpoint lookup must use the target backing store")) return 1;
    if (expect(bps.find_at(0xE000, MONITOR_BACKING_RAM) == ram,
               "RAM breakpoint lookup must use the target backing store")) return 1;
    bps.set_enabled(krn, false);
    if (expect(!bps.get(krn)->enabled && bps.get(ram)->enabled,
               "Disabling one same-address target must not disable the other")) return 1;
    bps.set_enabled(krn, true);

    for (int i = 4; i < MONITOR_BREAKPOINT_SLOT_COUNT; i++) {
        bps.allocate((uint16_t)(0xD000 + i), 0x07);
    }
    int full = bps.allocate(0xE000, 0x07);
    if (expect(full == krn, "Full table should still return an existing same-target slot")) return 1;
    full = bps.allocate(0xE100, 0x07);
    if (expect(full < 0, "Allocation must fail when all slots are used")) return 1;

    char row[40];
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strcmp(row, "0 SET $C000 RAM") == 0,
               "Slot 0 popup row should include the RAM target tag")) return 1;
    bps.set_enabled(0, false);
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strcmp(row, "0 OFF $C000 RAM") == 0,
               "Disabled slot should keep the target tag")) return 1;
    bps.set_label(0, "readc");
    if (expect(strcmp(bps.get(0)->label, "READ") == 0,
               "Breakpoint labels must normalize to four uppercase chars")) return 1;
    MonitorBreakpoints::format_popup_row(row, sizeof(row), 0, bps.get(0));
    if (expect(strcmp(row, "0 OFF READ $C000 RAM") == 0,
               "Breakpoint popup row should include label and target tag")) return 1;
    MonitorBreakpoints::format_popup_row(row, sizeof(row), krn, bps.get(krn));
    if (expect(strcmp(row, "2 SET $E000 KRN") == 0,
               "KERNAL breakpoint popup row must show KRN target tag")) return 1;
    MonitorBreakpoints::format_popup_row(row, sizeof(row), ram, bps.get(ram));
    if (expect(strcmp(row, "3 SET $E000 RAM") == 0,
               "Same-address RAM breakpoint popup row must remain distinguishable")) return 1;

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
        if (((strstr(row, "CPU") != NULL) || (strstr(row, "C") == row && strstr(row, "O") != NULL)) &&
                strstr(row, "VIC") != NULL) {
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

    // Plain-RAM cursor target: a contextless non-JSR Step Over of visible ROM
    // is gated now, and this test is about the predictor's byte source, not
    // banking. The backend still serves BRK bytes the session must override.
    backend.write(0x4000, 0x00);
    backend.write(0x4001, 0x00);
    backend.write(0x4002, 0x00);

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("4000", 1);

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

static int test_debug_pc_disassembly_uses_session_live_bytes_over_view()
{
    TestUserInterface ui;
    CaptureScreen screen;
    DebugBackendController ctrl;
    FakeDebugBackend backend(&ctrl);
    monitor_reset_saved_state();

    ctrl.session.next_ctx.valid = true;
    ctrl.session.next_ctx.pc = 0xE000;
    ctrl.session.next_ctx.sp = 0xF7;
    ctrl.session.next_ctx.sr = 0x24;
    ctrl.session.predict_bytes_valid = true;
    ctrl.session.predict_bytes[0] = 0xA9;
    ctrl.session.predict_bytes[1] = 0x42;
    ctrl.session.predict_bytes[2] = 0xEA;
    backend.write(0x0001, 0x05);
    backend.set_monitor_cpu_port(0x07);

    // The monitor view would decode BRK from the backing memory. The active
    // Debug PC row must instead use the session's execution-facing bytes,
    // which are supplied by the live CPU-bank route.
    backend.write(0xE000, 0x00);
    backend.write(0xE001, 0x00);
    backend.write(0xE002, 0x00);

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Switch to ASM should stay in monitor")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug should stay in monitor")) return 1;

    char row[40];
    int row_y = find_row_with_address(screen, "E000");
    if (expect(row_y >= 0, "Debug PC row must be visible")) return 1;
    screen.get_slice(1, row_y, 38, row);
    if (expect(strstr(row, ">LDA #$42<") != NULL,
               "Debug PC row must decode live session bytes instead of monitor-view BRK")) return 1;
    if (expect(strstr(row, ">BRK<") == NULL,
               "Debug PC row must not display monitor-view BRK bytes")) return 1;
    if (expect(strstr(row, "[RAM]") != NULL,
               "Debug PC row source marker must describe the live CPU backing store")) return 1;

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

    const int keys[] = { 'A', 'D', 'T', 'U', KEY_ESCAPE, KEY_BREAK };
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
    if (expect(backend.last_session->out_calls == 1, "U should call step-out exactly once")) return 1;
    if (expect(backend.last_session->over_calls == 0, "Neither T nor U should call Over")) return 1;
    monitor.deinit();
    return 0;
}

static int test_debug_o_does_not_step_out()
{
    // Debug-mode Step Out moved from 'O' to 'U'. 'O'/'o' must no longer invoke
    // step-out; they fall through to the normal monitor dispatcher so the
    // monitor view CPU/VIC bank can still be cycled while Debug is active.
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', 'O', 'o', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(backend.last_session != NULL, "Debug session must exist")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug 'O' must stay in monitor")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug 'o' must stay in monitor")) return 1;
    if (expect(backend.last_session->out_calls == 0,
               "Debug-mode O/o must not invoke Step Out")) return 1;
    if (expect(backend.last_session->trace_calls == 0 &&
               backend.last_session->over_calls == 0,
               "Debug-mode O/o must not invoke any stepping command")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
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

    const int keys[] = { 'A', 'D', 'U', KEY_BREAK, KEY_BREAK };
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

    const int keys[] = { 'A', 'D', 'U', KEY_BREAK, KEY_BREAK };
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

static int test_t_after_goto_ignores_successful_stale_snapshot()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    DebugContext stale;
    debug_context_reset(&stale);
    stale.valid = true;
    stale.pc = 0x0000;
    stale.sp = 0xE4;
    stale.sr = 0x36;
    backend.canned_snapshot = stale;
    backend.canned_snapshot_set = true;
    backend.snapshot_result = DebugSession::DBG_OK;
    // Plain-RAM cursor target: contextless Step Into of visible ROM is gated
    // ("run to a breakpoint 1st"), and this test is about stale-snapshot
    // handling, not banking.
    backend.write(0x2000, 0x85);
    backend.write(0x2001, 0x56);
    backend.write(0x2002, 0xEA);
    backend.write(0x0000, 0xEA);
    backend.write(0x0001, 0xEA);

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
            if (expect(r == 0, "Stale-snapshot Trace setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->trace_calls == 0,
               "Cursor-authoritative Debug entry must ignore a successful stale snapshot")) return 1;
    if (expect(backend.last_session->trace_at_calls == 1,
               "Cursor-authoritative Debug entry must Trace from the cursor")) return 1;
    if (expect(backend.last_session->last_start_pc == 0x2000,
               "Trace after J 2000 must start at the cursor, not stale PC $0000")) return 1;
    monitor.deinit();
    return 0;
}

static int test_g_after_goto_ignores_successful_stale_snapshot()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    DebugContext stale;
    debug_context_reset(&stale);
    stale.valid = true;
    stale.pc = 0x0000;
    stale.sp = 0xE4;
    stale.sr = 0x36;
    backend.canned_snapshot = stale;
    backend.canned_snapshot_set = true;
    backend.snapshot_result = DebugSession::DBG_OK;
    backend.write(0xE000, 0x85);
    backend.write(0xE001, 0x56);
    backend.write(0xE002, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'G', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 6; i++) {
        int r = monitor.poll(0);
        if (i < 5) {
            if (expect(r == 0, "Stale-snapshot Go setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->go_calls == 1,
               "G must invoke the debug session Go path")) return 1;
    if (expect(backend.last_session->last_start_pc == 0xE000,
               "G after J E000 must start at the cursor, not stale PC $0000")) return 1;
    monitor.deinit();
    return 0;
}

static int test_g_after_cursor_stop_uses_captured_context()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    DebugContext stopped;
    debug_context_reset(&stopped);
    stopped.valid = true;
    stopped.pc = 0xE003;
    stopped.sp = 0xE4;
    stopped.sr = 0x36;
    backend.canned_snapshot = stopped;
    backend.canned_snapshot_set = true;
    backend.snapshot_result = DebugSession::DBG_OK;
    backend.go_produces_snapshot = true;
    backend.write(0xE000, 0x85);
    backend.write(0xE001, 0x56);
    backend.write(0xE002, 0xEA);
    backend.write(0xE003, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'G', 'G', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 7; i++) {
        int r = monitor.poll(0);
        if (i < 6) {
            if (expect(r == 0, "Two-Go cursor/context setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->go_calls == 2,
               "Two G commands must invoke the debug session twice")) return 1;
    if (expect(backend.last_session->last_go_from_valid,
               "Second G after a captured stop must use the captured context")) return 1;
    if (expect(backend.last_session->last_go_from_pc == 0xE003,
               "Second G must resume from the captured PC, not the cursor override")) return 1;
    monitor.deinit();
    return 0;
}

static int test_g_after_goto_with_active_context_uses_cursor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0x1000, 0xEA);
    backend.write(0xE000, 0x85);
    backend.write(0xE001, 0x56);
    backend.write(0xE002, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'D', 'J', 'G', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 8);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("1000", 1);
    ui.push_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 8; i++) {
        int r = monitor.poll(0);
        if (i < 7) {
            if (expect(r == 0, "Active-context Goto/Go setup polls should return 0")) return 1;
        } else {
            if (expect(r == 1, "Final RUN/STOP exits")) return 1;
        }
    }
    if (expect(backend.last_session != NULL, "Backend should have created a debug session")) return 1;
    if (expect(backend.last_session->go_calls == 1,
               "G after active-context J must invoke Go once")) return 1;
    if (expect(!backend.last_session->last_go_from_valid,
               "G after active-context J must ignore the previous captured context")) return 1;
    if (expect(backend.last_session->last_start_pc == 0xE000,
               "G after active-context J must start from the cursor address")) return 1;
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

static int test_ctrl_d_from_edit_clears_edit_for_redebug()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x2000, 0xEA);
    backend.write(0x2001, 0xEA);
    backend.write(0x2002, 0xEA);

    const int keys[] = { 'A', 'D', 'E', KEY_CTRL_D, 'J', 'D', 'D',
                         KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 9);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.color_fg = 12;
    ui.set_prompt("2000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit ok")) return 1;
    if (expect(monitor.poll(0) == 0, "C=+D should leave Debug and Edit")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") == NULL && strstr(header, "Edit") == NULL,
               "C=+D in Debug+Edit must clear both Dbg and Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "J after C=+D must be a monitor command")) return 1;
    if (expect(ui.prompt_index == 1, "J after C=+D must consume the jump prompt")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $2000") != NULL,
               "Jump after C=+D must move the Assembly cursor")) return 1;
    if (expect(monitor.poll(0) == 0, "Re-enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Step after re-enter ok")) return 1;
    if (expect(backend.last_session != NULL, "Re-entered Debug must have a session")) return 1;
    if (expect(backend.last_session->over_at_calls == 1,
               "Re-entered no-context Debug must step from the cursor")) return 1;
    if (expect(backend.last_session->last_start_pc == 0x2000,
               "Re-entered no-context Debug must use the post-C=+D jump target")) return 1;
    if (expect(monitor.poll(0) == 0, "First RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits the monitor")) return 1;
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
    if (expect(!monitor.consume_release_host_after_exit(),
               "Local UI CTRL+X reset must not take the host-release exit path")) return 1;
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

static int test_external_reset_during_debug_wait_exits_without_reopen()
{
    TestUserInterface ui;
    CaptureScreen screen;
    BrkSessionBackend backend(false);
    monitor_reset_saved_state();

    const int keys[] = { 'J', 'A', 'D', 'T' };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("0801", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM setup should return 0")) return 1;
    if (expect(monitor.poll(0) == 0, "Debug entry should return 0")) return 1;
    if (expect(backend.last_session != NULL,
               "Debug entry must create the BRK debug session before trace starts")) return 1;
    backend.last_session->monitor_reset_cancel_target = &monitor;
    backend.last_session->monitor_reset_cancel_on_delay = true;
    if (expect(monitor.poll(0) == MENU_EXIT,
               "External reset during an active debug wait must unwind out of the monitor")) return 1;
    if (expect(backend.last_session != NULL,
               "Active trace must create the BRK debug session")) return 1;
    if (expect(backend.last_session->freeze_restore_pokes > 0,
               "External reset during debug wait must clean up the active session")) return 1;
    if (expect(backend.last_session->reset_calls == 0,
               "External reset during debug wait must not issue a second machine reset")) return 1;
    if (expect(!monitor.consume_reopen_after_reset(),
               "External reset during debug wait must not request monitor re-entry")) return 1;
    monitor.deinit();
    return 0;
}

static int test_external_reset_cancel_exits_direct_monitor_without_next_key()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x0801, 0xEA);

    const int keys[] = { 'A', 'D' };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(backend.last_session != NULL,
               "Debug entry must create a session before external reset cancel")) return 1;

    monitor.request_debug_reset_cancel();

    if (expect(backend.last_session->reset_cancel_calls == 1,
               "External reset cancel must be forwarded to the active debug session")) return 1;
    if (expect(monitor.poll(0) == MENU_EXIT,
               "External reset cancel must exit a direct-render monitor without waiting for another key")) return 1;
    if (expect(!monitor.consume_reopen_after_reset(),
               "External reset cancel must not request monitor re-entry")) return 1;
    monitor.deinit();
    return 0;
}

static void seed_kernal_out_reset_decode_bytes(SourceLabelDebugBackend &backend)
{
    backend.write(0xDFFE, 0x21);
    backend.write(0xDFFF, 0xD0);
    backend.write(0xE000, 0xEE);
    backend.write(0xE001, 0x21);
    backend.write(0xE002, 0xD0);
    backend.write(0xE003, 0xE8);
    backend.write(0xE004, 0x88);
    backend.write(0xE005, 0x98);
    backend.write(0xE006, 0x4C);
    backend.write(0xE007, 0x00);
    backend.write(0xE008, 0xE0);
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xE000;
    backend.canned_snapshot.sp = 0xF7;
    backend.canned_snapshot.sr = 0x24;
    backend.canned_snapshot.live_cpu_port_valid = true;
    backend.canned_snapshot.live_cpu_port = 0x05;
    backend.canned_snapshot.cpu_port_registers_valid = true;
    backend.canned_snapshot.cpu_ddr = 0x37;
    backend.canned_snapshot.cpu_port_latch = 0x35;
    backend.canned_snapshot_set = true;
}

static int test_ctrl_x_reset_reentry_anchors_asm_at_source_boundary()
{
    TestUserInterface ui;
    CaptureScreen screen;
    SourceLabelDebugBackend backend;
    monitor_reset_saved_state();

    seed_kernal_out_reset_decode_bytes(backend);

    const int keys[] = { 'A', 'D', KEY_CTRL_X };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug at RAM-under-KERNAL PC ok")) return 1;
    if (expect(monitor.poll(0) == MENU_EXIT,
               "CTRL+X reset from direct UI must exit for re-entry")) return 1;
    if (expect(monitor.consume_reopen_after_reset(),
               "CTRL+X reset must request monitor re-entry")) return 1;
    monitor.deinit();

    const int reentry_keys[] = { KEY_BREAK, KEY_BREAK };
    FakeKeyboard reentry_keyboard(reentry_keys, 2);
    ui.keyboard = &reentry_keyboard;
    BackendMachineMonitor reentered(&ui, &backend);
    reentered.set_reset_exits_monitor(true);
    reentered.init(&screen, &reentry_keyboard);

    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $E000") != NULL,
               "Reset re-entry must anchor ASM at the captured $E000 PC")) return 1;
    int row_y = find_row_with_address(screen, "E000");
    if (expect(row_y == 4,
               "Reset re-entry must not prepend $DFFE/$DFFF before $E000")) return 1;
    char row[40];
    screen.get_slice(1, row_y, 38, row);
    if (expect(strstr(row, "EE 21 D0") != NULL &&
               strstr(row, "INC $D021") != NULL,
               "Reset re-entry must decode the $E000 INC $D021 instruction")) return 1;
    if (expect(find_row_with_address(screen, "DFFE") < 0 &&
               find_row_with_address(screen, "DFFF") < 0,
               "ASM context rows must not cross the IO/RAM source boundary")) return 1;

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

static int test_breakpoint_mismatch_message_uses_view_target_and_live_cpu()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    backend.write(0x0001, 0x07); // live CPU sees KERNAL at $E000
    backend.write(0xE000, 0xEA);

    int keys[14];
    int n = 0;
    keys[n++] = 'J';
    keys[n++] = 'A';
    for (int i = 0; i < 6; i++) {
        keys[n++] = 'o'; // monitor view CPU7 -> CPU5, RAM under KERNAL
    }
    keys[n++] = 'D';
    keys[n++] = 'R';
    keys[n++] = KEY_BREAK;
    keys[n++] = KEY_BREAK;
    FakeKeyboard keyboard(keys, n);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < n - 2; i++) {
        if (expect(monitor.poll(0) == 0,
                   "Mismatch breakpoint setup should stay in monitor")) return 1;
    }
    if (expect(strcmp(ui.last_popup, "BRK RAM, CPU KRN; not mapped now") == 0,
               "Mismatch popup must describe breakpoint target and live CPU mapping")) return 1;
    if (expect((int)strlen(ui.last_popup) <= 38,
               "Mismatch popup must fit one 38-column line")) return 1;

    if (expect(monitor.poll(0) == 0, "RUN/STOP leaves Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "Second RUN/STOP exits")) return 1;
    monitor.deinit();

    TestUserInterface matched_ui;
    CaptureScreen matched_screen;
    TrackingDebugBackend matched_backend;
    monitor_reset_saved_state();
    matched_backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;
    matched_backend.write(0x0001, 0x07);
    matched_backend.write(0xE000, 0xEA);
    const int matched_keys[] = { 'J', 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard matched_keyboard(matched_keys, 6);
    matched_ui.screen = &matched_screen;
    matched_ui.keyboard = &matched_keyboard;
    matched_ui.set_prompt("E000", 1);
    BackendMachineMonitor matched_monitor(&matched_ui, &matched_backend);
    matched_monitor.init(&matched_screen, &matched_keyboard);
    for (int i = 0; i < 4; i++) {
        if (expect(matched_monitor.poll(0) == 0,
                   "Matched breakpoint setup should stay in monitor")) return 1;
    }
    if (expect(matched_ui.popup_count == 0,
               "No mismatch popup should appear when target is currently mapped")) return 1;
    if (expect(matched_monitor.poll(0) == 0, "RUN/STOP leaves matched Debug")) return 1;
    if (expect(matched_monitor.poll(0) == 1, "Second RUN/STOP exits matched monitor")) return 1;
    matched_monitor.deinit();

    const MonitorBackingStore tags[] = {
        MONITOR_BACKING_RAM,
        MONITOR_BACKING_BASIC,
        MONITOR_BACKING_KERNAL,
        MONITOR_BACKING_IO,
        MONITOR_BACKING_CHAR
    };
    char msg[40];
    for (unsigned i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        for (unsigned j = 0; j < sizeof(tags) / sizeof(tags[0]); j++) {
            monitor_format_breakpoint_mismatch(msg, sizeof(msg), tags[i], tags[j]);
            if (expect((int)strlen(msg) <= 38,
                       "All mismatch tag combinations must fit the footer width")) return 1;
            if (expect(strstr(msg, "BRK ") == msg && strstr(msg, ", CPU ") != NULL &&
                       strstr(msg, "; not mapped now") != NULL,
                       "Mismatch formatter must keep the required template")) return 1;
        }
    }
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
        'A', 'D', KEY_CTRL_R, 'D', 'T', 'U', 'G', 'R',
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
        "D Step Over  T Step Into  U Step Out",
        "G Continue   K Cont Crsr  RET Follow",
        "R Breakpt    C=+R Brkpts  C=+X Reset",
        "",
        "M Memory     I ASCII      V Screen",
        "A Assembly   B Binary     O CPU Bank",
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
    if (expect_help_token_not_accented(screen, "U Step Out",
        "U Step Out must not use a distinct debug help colour")) return 1;
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

// Locate the rendered "CPUx ... VICy" / "CxOy ... VICy" monitor status footer
// (it begins at screen column 1). Returns true and fills `out` when found.
static bool capture_status_footer(CaptureScreen &screen, char *out, int out_len)
{
    char row[40];
    out[0] = 0;
    for (int y = 0; y < 25; y++) {
        screen.get_slice(1, y, 38, row);
        if (strstr(row, "VIC") != NULL &&
            (strncmp(row, "CPU", 3) == 0 ||
             (row[0] == 'C' && row[2] == 'O'))) {
            strncpy(out, row, out_len - 1);
            out[out_len - 1] = 0;
            return true;
        }
    }
    return false;
}

// Issue 1: after a C64 hardware reset, a fresh monitor open must follow the live
// CPU bank so re-entry decodes the memory the CPU actually sees (RAM-under-ROM
// when $01 low bits are 5), not a stale/forced view. Without the fix the reset
// hook forces the saved view to 7 and the footer shows the C5O7 mismatch.
static int test_reset_reentry_view_follows_live_cpu_bank()
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    monitor_reset_saved_state();
    ui.screen = &screen;

    // A C64 hardware reset fires the global reset hook (sets view -> 7 fallback
    // and arms the sync-to-live request for the next open).
    monitor_reset_saved_cpu_view();

    // After the reset the live CPU is running RAM-under-ROM: $01 low bits = 5.
    backend.live_cpu_port = 0x05;

    const int nokeys[] = { 0 };
    FakeKeyboard kb(nokeys, 0);
    ui.keyboard = &kb;
    MachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &kb);

    char footer[40];
    if (expect(capture_status_footer(screen, footer, sizeof(footer)),
               "Status footer must be visible after reset re-entry")) { monitor.deinit(); return 1; }
    // Matched form "CPU5" proves the view followed the live bank; the erroneous
    // "C5O7" mismatch must not appear.
    if (expect(strncmp(footer, "CPU5", 4) == 0,
               "Reset re-entry must follow live CPU bank (footer CPU5, not C5O7)")) {
        printf("  footer was: '%s'\n", footer);
        monitor.deinit();
        return 1;
    }
    monitor.deinit();
    return 0;
}

// Invariant #2: an ordinary monitor close/reopen WITHOUT an intervening reset
// must preserve a manually selected (O) exploratory view bank, i.e. the
// sync-to-live behaviour is gated strictly to the post-reset open.
static int test_reopen_without_reset_preserves_manual_cpu_view()
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    monitor_reset_saved_state();
    ui.screen = &screen;
    backend.live_cpu_port = 0x05;   // live CPU banks RAM under ROM

    // Session 1: manually cycle the view bank with 'o'. From the default 7 the
    // sequence is 7 -> 0 -> 1 -> 2 -> 3, so four presses select view bank 3.
    {
        const int keys[] = { 'o', 'o', 'o', 'o' };
        FakeKeyboard kb(keys, 4);
        ui.keyboard = &kb;
        MachineMonitor m(&ui, &backend);
        m.init(&screen, &kb);
        for (int i = 0; i < 4; i++) {
            if (expect(m.poll(0) == 0, "cycle CPU bank ok")) { m.deinit(); return 1; }
        }
        m.deinit();   // persists manual view bank 3
    }

    // Session 2: reopen with NO reset hook in between -> manual view preserved.
    const int nokeys[] = { 0 };
    FakeKeyboard kb(nokeys, 0);
    ui.keyboard = &kb;
    MachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &kb);

    char footer[40];
    if (expect(capture_status_footer(screen, footer, sizeof(footer)),
               "Status footer must be visible on ordinary reopen")) { monitor.deinit(); return 1; }
    if (expect(strncmp(footer, "C5O3", 4) == 0,
               "Ordinary reopen must preserve the manual O view bank (footer C5O3)")) {
        printf("  footer was: '%s'\n", footer);
        monitor.deinit();
        return 1;
    }
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

static int test_freeze_step_over_refreezes_when_start_predicate_is_stale()
{
    FakeFreezeMachine m(false);
    m.set_run_window_refreeze_enabled(true);
    fake_seed_nop_run(m, 0x2000);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0x2000, pred, &ctx);

    if (expect(r == DebugSession::DBG_OK,
               "freeze stale-predicate step-over must complete")) return 1;
    if (expect(m.unfreeze_attempts == 0,
               "freeze stale-predicate step-over must not unfreeze an already-live C64")) return 1;
    if (expect(m.refreeze_calls == 1,
               "freeze stale-predicate step-over must re-take freeze ownership")) return 1;
    if (expect(m.frozen,
               "freeze stale-predicate step-over must leave menu_screen readable")) return 1;
    if (expect(m.screen_render_target_invalidated(),
               "freeze stale-predicate step-over must request direct-screen redraw")) return 1;
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
    m.arm_capture_context(0x2001, 0xFF, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.over(from, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "freeze step-over (with context) must complete")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2001,
               "freeze step-over (with context) must capture the installed BRK")) return 1;
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

static int test_step_out_unwinds_past_eight_nested_traces()
{
    // Regression: Step Out walks the traced return-target stack. An 8-entry cap
    // silently dropped the oldest frames, so after tracing >8 nested JSRs a deep
    // Step Out falsely reported NOT IN SUBROUTINE even though the live 6510 stack
    // still held valid frames. Found on real hardware at depth 16 (the 9th Step
    // Out). The cap is now the 6510 physical nesting limit (128).
    FakeFreezeMachine m(false);
    const int N = 12;  // exceeds the historical 8-entry cap
    for (int i = 0; i <= N; i++) {
        uint16_t li = (uint16_t)(0xC000 + i * 0x10);
        if (i < N) {
            uint16_t lnext = (uint16_t)(0xC000 + (i + 1) * 0x10);
            m.ram[li] = 0x20;
            m.ram[li + 1] = (uint8_t)(lnext & 0xFF);
            m.ram[li + 2] = (uint8_t)(lnext >> 8);
        }
        m.ram[li + 3] = 0xEA;
    }

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xC000;
    from.sp = 0xF8;
    from.sr = 0x24;

    uint8_t sp = 0xF8;
    for (int i = 0; i < N; i++) {
        uint16_t jsr_pc = (uint16_t)(0xC000 + i * 0x10);
        uint16_t callee = (uint16_t)(0xC000 + (i + 1) * 0x10);
        uint8_t jsr[3] = { 0x20, (uint8_t)(callee & 0xFF), (uint8_t)(callee >> 8) };
        DebugPredictResult pred;
        debug_predict(jsr_pc, jsr, false, &pred);
        sp = (uint8_t)(sp - 2);
        m.arm_capture_context(callee, sp, 0, 0, 0, 0x24);
        DebugContext out;
        if (expect(m.trace_at(jsr_pc, pred, &out) == DebugSession::DBG_OK,
                   "deep Step Into of a JSR must succeed")) return 1;
        from = out;
    }

    for (int i = N - 1; i >= 0; i--) {
        uint16_t return_site = (uint16_t)((0xC000 + i * 0x10) + 3);
        sp = (uint8_t)(sp + 2);
        m.arm_capture_context(return_site, sp, 0, 0, 0, 0x24);
        DebugContext out;
        DebugSession::Result r = m.step_out(from, &out);
        if (expect(r == DebugSession::DBG_OK,
                   "Step Out must unwind every traced level past depth 8")) return 1;
        if (expect(out.pc == return_site,
                   "Step Out must land on the traced caller return site")) return 1;
        from = out;
    }
    return 0;
}

static int test_visible_rom_simple_linear_interprets_without_breakpoint()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;

    // Interpretation mutates the cached context, which is only truthful while
    // the CPU is parked in the debug spin loop; park it like a real session.
    m.ram[0x2000] = 0xEA;
    m.ram[0x2001] = 0xEA;
    {
        uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
        DebugPredictResult park_pred;
        debug_predict(0x2000, nop, false, &park_pred);
        m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
        DebugContext parked;
        if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                   "parking step must complete")) return 1;
    }

    m.kernal_rom[0x09E3] = 0xA5; // E9E3 LDA $AC
    m.kernal_rom[0x09E4] = 0xAC;
    m.ram[0x00AC] = 0x80;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE9E3;
    from.sp = 0xCB;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t bytes[3] = { 0xA5, 0xAC, 0x00 };
    DebugPredictResult pred;
    debug_predict(0xE9E3, bytes, false, &pred);

    int brk_before = m.brk_patch_writes;
    int rom_before = m.rom_patch_writes;
    DebugContext out;
    DebugSession::Result r = m.over(from, pred, &out);
    if (expect(r == DebugSession::DBG_OK,
               "Visible-ROM simple linear step must complete")) return 1;
    if (expect(out.pc == 0xE9E5 && out.a == 0x80 && (out.sr & 0x82) == 0x80,
               "Visible-ROM LDA zp must update PC, A, N, and Z")) return 1;
    if (expect(m.brk_patch_writes == brk_before && m.rom_patch_writes == rom_before,
               "Visible-ROM simple linear step must not install a breakpoint")) return 1;
    return 0;
}

static int test_visible_rom_basic_loop_interprets_index_register_and_flags()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;

    // Park the CPU first; context-mutating interpretation requires it.
    m.ram[0x2000] = 0xEA;
    m.ram[0x2001] = 0xEA;
    {
        uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
        DebugPredictResult park_pred;
        debug_predict(0x2000, nop, false, &park_pred);
        m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
        DebugContext parked;
        if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                   "parking step must complete")) return 1;
    }

    m.basic_rom[0x1C0F] = 0xA2; // BC0F LDX #$06
    m.basic_rom[0x1C10] = 0x06;
    m.basic_rom[0x1C11] = 0xB5; // BC11 LDA $60,X
    m.basic_rom[0x1C12] = 0x60;
    m.basic_rom[0x1C13] = 0x95; // BC13 STA $68,X
    m.basic_rom[0x1C14] = 0x68;
    m.basic_rom[0x1C15] = 0xCA; // BC15 DEX
    m.basic_rom[0x1C16] = 0xD0; // BC16 BNE $BC11
    m.basic_rom[0x1C17] = 0xF9;
    m.basic_rom[0x1C18] = 0x86; // BC18 STX $70
    m.basic_rom[0x1C19] = 0x70;

    DebugContext ctx;
    debug_context_reset(&ctx);
    ctx.valid = true;
    ctx.pc = 0xBC0F;
    ctx.sp = 0xCB;
    ctx.sr = 0x24;
    ctx.live_cpu_port_valid = true;
    ctx.live_cpu_port = 0x07;

    uint8_t ldx[3] = { 0xA2, 0x06, 0x00 };
    DebugPredictResult pred;
    debug_predict(ctx.pc, ldx, false, &pred);
    DebugSession::Result r = m.over(ctx, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK && ctx.pc == 0xBC11 &&
               ctx.x == 0x06 && (ctx.sr & 0x02) == 0,
               "Visible-ROM LDX immediate must update X and Z")) return 1;

    for (int x = 6; x >= 0; x--) {
        m.ram[(uint8_t)(0x60 + x)] = (uint8_t)(0x80 | x);
        uint8_t lda[3] = { 0xB5, 0x60, 0x00 };
        debug_predict(ctx.pc, lda, false, &pred);
        r = m.over(ctx, pred, &ctx);
        if (expect(r == DebugSession::DBG_OK && ctx.pc == 0xBC13 &&
                   ctx.a == (uint8_t)(0x80 | x) && (ctx.sr & 0x80),
                   "Visible-ROM LDA zp,X must update A and N")) return 1;

        uint8_t sta[3] = { 0x95, 0x68, 0x00 };
        debug_predict(ctx.pc, sta, false, &pred);
        r = m.over(ctx, pred, &ctx);
        if (expect(r == DebugSession::DBG_OK && ctx.pc == 0xBC15 &&
                   m.ram[(uint8_t)(0x68 + x)] == (uint8_t)(0x80 | x),
                   "Visible-ROM STA zp,X must write the indexed address")) return 1;

        uint8_t dex[3] = { 0xCA, 0x00, 0x00 };
        debug_predict(ctx.pc, dex, false, &pred);
        r = m.over(ctx, pred, &ctx);
        if (expect(r == DebugSession::DBG_OK && ctx.pc == 0xBC16,
                   "Visible-ROM DEX must advance to the branch")) return 1;
        if (expect(ctx.x == (uint8_t)(x - 1),
                   "Visible-ROM DEX must update X")) return 1;
        if (expect(((ctx.sr & 0x02) != 0) == (x == 1),
                   "Visible-ROM DEX must update Z for the following BNE")) return 1;

        uint16_t expected_pc = (x == 1) ? 0xBC18 : 0xBC11;
        ctx.pc = expected_pc;
        if (x == 1) {
            break;
        }
    }

    uint8_t stx[3] = { 0x86, 0x70, 0x00 };
    debug_predict(ctx.pc, stx, false, &pred);
    r = m.over(ctx, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK && ctx.pc == 0xBC1A &&
               m.ram[0x70] == 0,
               "Visible-ROM STX zp must store the final X")) return 1;
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
    m.arm_capture_context(0x2005, 0xFF, 0, 0, 0, 0x24);

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

    // Model a trap at the installed fall-through BRK ($A001) so the relaunch
    // backstop sees a legitimate capture and does not relaunch. Use the dynamic
    // override (a static seed is wiped by the handler install).
    m.arm_capture_context(0xA001, 0xF8, 0x11, 0x22, 0x33, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0xA000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible BASIC step must succeed when ROM patching is supported")) return 1;
    if (expect(m.basic_rom[1] == 0xEA,
               "Visible BASIC patch byte must be restored after the step")) return 1;
    if (expect(m.ram[0xA001] == 0x5A,
               "Visible BASIC patching must not scribble into underlying RAM")) return 1;
    // 4 ROM-image writes: install BRK + recommit launch-PC fetch byte + recommit
    // BRK + restore. The two recommits guard the live 6510 against a stale fetch.
    if (expect(m.rom_patch_writes == 4,
               "Visible BASIC step must patch, recommit launch byte + BRK, and restore the ROM image")) return 1;
    // The BRK ($00) is written twice for the single step: once by install_brk_at
    // and once by recommit_visible_rom_patches() in the launch's stopped session.
    if (expect(m.brk_patch_writes == 2,
               "Visible BASIC step must re-commit the ROM BRK as the final pre-launch write")) return 1;
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

static int test_u64_debug_cpu_port_uses_live_cpu_bank()
{
    FakeCpuPortBackend backend;

    backend.set_monitor_cpu_port(0x07);
    backend.live_cpu_port = 0x05;
    if (expect(u64_debug_step_cpu_port(&backend) == 0x05,
               "U64 Debug stepping must execute in the live CPU bank")) return 1;
    if (expect(backend.live_reads == 1,
               "U64 Debug stepping must sample live $0001 (the read also settles the bus)")) return 1;

    backend.set_monitor_cpu_port(0x05);
    backend.live_cpu_port = 0x07;
    if (expect(u64_debug_step_cpu_port(&backend) == 0x07,
               "U64 Debug stepping must ignore monitor-bank changes (view decoupling is read-side)")) return 1;
    if (expect(backend.live_reads == 2,
               "U64 Debug stepping must re-sample the live bank each step")) return 1;
    if (expect(u64_debug_step_cpu_port(0) == 0x07,
               "U64 Debug stepping must default to all ROMs visible without a backend")) return 1;

    return 0;
}

static int test_brk_debug_cassette_layout_is_compact_and_top_aligned()
{
    if (expect(FAKE_HANDLER_ADDR >= FAKE_DEBUG_AREA_START,
               "BRK debug handler must stay inside the cassette buffer")) return 1;
    if (expect(FAKE_HANDLER_LEN == 45,
               "BRK debug handler byte count must include sentinel-tail padding")) return 1;
    if (expect(FAKE_TRAMPOLINE_LEN == 38,
               "BRK debug trampoline byte count must stay compact")) return 1;
    if (expect(FAKE_NMI_TRAMP_LEN == 24,
               "NMI trampoline reservation must remain 24 bytes")) return 1;
    if (expect(FAKE_HARD_BRK_STUB_LEN == 37,
               "Hard BRK stub reservation must preserve the validated hardware entry address")) return 1;
    if (expect(FAKE_HANDLER_ADDR + FAKE_HANDLER_LEN == FAKE_TRAMPOLINE,
               "Handler and trampoline must be contiguous")) return 1;
    if (expect(FAKE_TRAMPOLINE + FAKE_TRAMPOLINE_LEN == FAKE_NMI_TRAMP,
               "Trampoline and NMI trampoline reservation must be contiguous")) return 1;
    if (expect(FAKE_NMI_TRAMP + FAKE_NMI_TRAMP_LEN == FAKE_HARD_BRK_STUB,
               "NMI reservation and hard BRK stub must be contiguous")) return 1;
    if (expect(FAKE_HARD_BRK_STUB + FAKE_HARD_BRK_STUB_LEN == FAKE_STORE_HARD_CPU_PORT,
               "Code stubs must pack directly below the fixed scratch block")) return 1;
    if (expect(FAKE_STORE_HARD_CPU_PORT == 0x03ED &&
               FAKE_STORE_HARD_CPU_DDR == FAKE_DEBUG_AREA_END,
               "Fixed scratch bytes must still end at DEBUG_AREA_END $03FB")) return 1;
    if (expect(FAKE_DEBUG_AREA_END == 0x03FB,
               "DEBUG_AREA_END must remain $03FB so $03FC-$03FF stay free")) return 1;
    if (expect(FAKE_HARD_BRK_STUB + FAKE_HARD_BRK_STUB_LEN <= 0x03FC,
               "Debug stubs must not reserve $03FC-$03FF")) return 1;
    if (expect((FAKE_SPIN_JMP >> 8) == 0x03 &&
               (FAKE_TRAMPOLINE >> 8) == 0x03 &&
               (FAKE_HANDLER_ADDR >> 8) == 0x03,
               "All spin targets must stay on page $03")) return 1;
    return 0;
}

static int test_brk_handler_bytes_preserve_stack_and_port_capture_semantics()
{
    FakeFreezeMachine m(false);
    if (expect(fake_install_debug_stubs(m, 0x2000),
               "Installing debug stubs for handler byte test must succeed")) return 1;

    const uint8_t sp = 0x80;
    const uint8_t frame[6] = { 0x33, 0x22, 0x11, 0xA4, 0x34, 0x12 };
    for (int i = 0; i < 6; i++) {
        m.ram[(uint16_t)(0x0101 + sp + i)] = frame[i];
    }
    m.ram[0x0000] = 0x37;
    m.ram[0x0001] = 0x35;
    m.ram[FAKE_SENTINEL_ADDR] = 0x00;
    m.ram[FAKE_SPIN_LO] = 0x00;
    m.ram[FAKE_SPIN_HI] = (uint8_t)(FAKE_SPIN_JMP >> 8);

    Mini6510 cpu = { 0, 0, 0, sp, 0, FAKE_HANDLER_ADDR, false, false };
    if (expect(mini_run(m.ram, cpu, FAKE_SPIN_JMP, 100),
               "Optimized handler bytes must reach the self-spin JMP")) return 1;
    if (expect(m.ram[FAKE_STORE_Y] == 0x33 &&
               m.ram[FAKE_STORE_X] == 0x22 &&
               m.ram[FAKE_STORE_A] == 0x11 &&
               m.ram[FAKE_STORE_SR] == 0xA4 &&
               m.ram[FAKE_STORE_PCLO] == 0x34 &&
               m.ram[FAKE_STORE_PCHI] == 0x12,
               "Optimized handler loop must preserve the old stack-frame mapping")) return 1;
    if (expect(m.ram[FAKE_STORE_SP] == (uint8_t)(sp + 6),
               "Optimized handler loop must preserve saved SP semantics")) return 1;
    if (expect(m.ram[FAKE_STORE_CPU_DDR] == 0x37 &&
               m.ram[FAKE_STORE_CPU_PORT] == 0x35,
               "Optimized handler must capture $00/$01 through zero-page loads")) return 1;
    if (expect(m.ram[FAKE_HANDLER_ADDR + 0x21] == 0x8D &&
               m.ram[FAKE_HANDLER_ADDR + 0x22] == (uint8_t)(FAKE_SENTINEL_ADDR & 0xFF) &&
               m.ram[FAKE_HANDLER_ADDR + 0x23] == (uint8_t)(FAKE_SENTINEL_ADDR >> 8),
               "Optimized handler must publish the sentinel before the padded tail")) return 1;
    if (expect(m.ram[FAKE_HANDLER_ADDR + 0x24] == 0xEA &&
               m.ram[FAKE_HANDLER_ADDR + 0x29] == 0xEA &&
               m.ram[FAKE_SPIN_JMP] == 0x4C,
               "Optimized handler padding must keep the self-spin JMP at $0387")) return 1;
    if (expect(m.ram[FAKE_SENTINEL_ADDR] != 0x00 &&
               m.ram[FAKE_SPIN_LO] == (uint8_t)(FAKE_SPIN_JMP & 0xFF) &&
               m.ram[FAKE_SPIN_HI] == (uint8_t)(FAKE_SPIN_JMP >> 8),
               "Optimized handler must mark sentinel nonzero and restore self-spin")) return 1;
    return 0;
}

static int test_brk_trampoline_bytes_preserve_rti_restore_semantics()
{
    FakeFreezeMachine m(false);
    if (expect(fake_install_debug_stubs(m, 0x2100),
               "Installing debug stubs for trampoline byte test must succeed")) return 1;

    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;
    m.ram[FAKE_STORE_SR] = 0xA5;
    m.ram[FAKE_STORE_PCLO] = 0x78;
    m.ram[FAKE_STORE_PCHI] = 0x56;
    m.ram[FAKE_STORE_SP] = 0xE8;
    m.ram[FAKE_STORE_Y] = 0x44;
    m.ram[FAKE_STORE_X] = 0x55;
    m.ram[FAKE_STORE_A] = 0x66;

    Mini6510 cpu = { 0x10, 0x20, 0x30, 0xF0, 0x00, FAKE_TRAMPOLINE, false, false };
    m.ram[0x01E9] = 0xDE;
    m.ram[0x01EA] = 0xAD;
    m.ram[0x01EB] = 0xBE;
    if (expect(mini_run(m.ram, cpu, 0, 100),
               "Optimized trampoline bytes must reach RTI")) return 1;
    if (expect(cpu.rti && cpu.pc == 0x5678 && cpu.sr == 0xA5,
               "Optimized trampoline must rebuild the RTI frame from stored SR/PC")) return 1;
    if (expect(m.ram[0x01E6] == 0xA5 &&
               m.ram[0x01E7] == 0x78 &&
               m.ram[0x01E8] == 0x56,
               "Optimized trampoline must build the RTI frame below the stored SP")) return 1;
    if (expect(m.ram[0x01E9] == 0xDE &&
               m.ram[0x01EA] == 0xAD &&
               m.ram[0x01EB] == 0xBE,
               "Optimized trampoline must not overwrite bytes above the stored SP")) return 1;
    if (expect(cpu.sp == 0xE8,
               "Optimized trampoline RTI must restore the stored SP")) return 1;
    if (expect(m.ram[0x0000] == 0x37 && m.ram[0x0001] == 0x35,
               "Optimized trampoline must restore $00/$01 before RTI")) return 1;
    if (expect(cpu.a == 0x66 && cpu.x == 0x55 && cpu.y == 0x44,
               "Optimized trampoline must restore A/X/Y before RTI")) return 1;
    return 0;
}

static int test_hard_brk_stub_bytes_preserve_irq_and_brk_paths()
{
    FakeFreezeMachine m(false);
    if (expect(fake_install_debug_stubs(m, 0x2200),
               "Installing debug stubs for hard-BRK byte test must succeed")) return 1;

    m.ram[FAKE_HARD_BRK_STUB + 19] = 0xA5;
    m.ram[FAKE_HARD_BRK_STUB + 24] = 0xA5;

    m.ram[FAKE_HARD_BRK_STUB + 0] = 0x48;
    m.ram[FAKE_HARD_BRK_STUB + 1] = 0x8A;
    m.ram[FAKE_HARD_BRK_STUB + 2] = 0x48;
    m.ram[FAKE_HARD_BRK_STUB + 3] = 0xBA;
    if (expect(m.ram[FAKE_HARD_BRK_STUB + 19] == 0xA5 &&
               m.ram[FAKE_HARD_BRK_STUB + 24] == 0xA5,
               "Hard BRK stub must use zero-page LDA $00/$01 opcodes")) return 1;

    m.ram[FAKE_HARD_BRK_ORIG_VECTOR_LO] = 0x34;
    m.ram[(uint16_t)(FAKE_HARD_BRK_ORIG_VECTOR_LO + 1)] = 0x12;
    m.ram[0x01FA] = 0x24; // B flag clear: non-BRK IRQ fallback.
    Mini6510 irq = { 0xA5, 0x5A, 0xC3, 0xF9, 0, FAKE_HARD_BRK_STUB, false, false };
    if (expect(mini_run(m.ram, irq, 0x1234, 100),
               "Hard BRK stub non-BRK path must jump through the original vector")) return 1;
    if (expect(irq.pc == 0x1234 && irq.a == 0xA5 && irq.x == 0x5A && irq.sp == 0xF9,
               "Hard BRK stub non-BRK path must restore A, X, and SP")) return 1;

    m.ram[FAKE_STORE_HARD_CPU_DDR] = 0x00;
    m.ram[FAKE_STORE_HARD_CPU_PORT] = 0x00;
    m.ram[FAKE_STORE_TRAP_MODE] = 0x00;
    m.ram[0x0000] = 0x37;
    m.ram[0x0001] = 0x35;
    m.ram[0x01FA] = 0x34; // B flag set: BRK path.
    m.ram[0x01FB] = 0x78;
    m.ram[0x01FC] = 0x56;
    Mini6510 brk = { 0xA5, 0x5A, 0xC3, 0xF9, 0, FAKE_HARD_BRK_STUB, false, false };
    if (expect(mini_run(m.ram, brk, FAKE_HANDLER_ADDR, 100),
               "Hard BRK stub BRK path must jump to the soft handler")) return 1;
    if (expect(brk.pc == FAKE_HANDLER_ADDR && brk.sp == 0xF6,
               "Hard BRK stub BRK path must stack Y above saved X/A before handler entry")) return 1;
    if (expect(m.ram[0x01F7] == 0xC3 &&
               m.ram[0x01F8] == 0x5A &&
               m.ram[0x01F9] == 0xA5,
               "Hard BRK stub BRK path must preserve Y, X, and A on the stack")) return 1;
    if (expect(m.ram[FAKE_STORE_HARD_CPU_DDR] == 0x37 &&
               m.ram[FAKE_STORE_HARD_CPU_PORT] == 0x35 &&
               m.ram[FAKE_STORE_TRAP_MODE] != 0x00,
               "Hard BRK stub BRK path must capture $00/$01 and mark hard-trap mode")) return 1;
    return 0;
}

static int test_brk_capture_records_live_cpu_port()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);
    fake_seed_captured_context(m, 0x2001, 0xF8, 0x11, 0x22, 0x33, 0x24);
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "BRK capture with live CPU port must complete")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2001,
               "BRK capture must keep the captured PC")) return 1;
    if (expect(ctx.live_cpu_port_valid && ctx.live_cpu_port == 0x05,
               "BRK capture must record the live $0001 execution bank")) return 1;
    if (expect(ctx.cpu_port_registers_valid && ctx.cpu_ddr == 0x37 &&
               ctx.cpu_port_latch == 0x35,
               "BRK capture must retain the full $00/$01 register bytes")) return 1;

    FakeFreezeMachine m2(false);
    fake_seed_nop_run(m2, 0x2100);
    fake_seed_captured_context(m2, 0x2101, 0xF8, 0x11, 0x22, 0x33, 0x24);
    m2.ram[FAKE_STORE_CPU_DDR] = 0x04;
    m2.ram[FAKE_STORE_CPU_PORT] = 0x00;
    debug_predict(0x2100, bytes, false, &pred);
    r = m2.trace_at(0x2100, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "BRK capture with input CPU-port bits must complete")) return 1;
    if (expect(ctx.live_cpu_port_valid && ctx.live_cpu_port == 0x03,
               "Input CPU-port bits must resolve high in the live bank")) return 1;

    FakeVisibleRomMachine m3(false);
    m3.cpu_port = 0x05;
    fake_seed_nop_run(m3, 0xE000);
    fake_seed_captured_context(m3, 0xE001, 0xF8, 0x11, 0x22, 0x33, 0x24);
    m3.ram[FAKE_STORE_CPU_DDR] = 0x00;
    m3.ram[FAKE_STORE_CPU_PORT] = 0x00;
    m3.arm_hard_vector_capture_context(0xE001, 0xF8, 0x11, 0x22, 0x33, 0x24,
                                       0x37, 0x35);
    debug_predict(0xE000, bytes, false, &pred);
    r = m3.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Hard-vector BRK capture must complete with hidden KERNAL")) return 1;
    if (expect(ctx.live_cpu_port_valid && ctx.live_cpu_port == 0x05,
               "Hard-vector BRK capture must keep the execution bank when DDR readback is empty")) return 1;
    if (expect(ctx.cpu_port_registers_valid && ctx.cpu_ddr == 0x37 &&
               ctx.cpu_port_latch == 0x35,
               "Hard-vector BRK capture must retain the full $00/$01 register bytes")) return 1;

    FakeVisibleRomMachine m4(false);
    m4.cpu_port = 0x05;
    fake_seed_nop_run(m4, 0xE100);
    fake_seed_captured_context(m4, 0xE101, 0xF8, 0x11, 0x22, 0x33, 0x24);
    m4.ram[FAKE_STORE_CPU_DDR] = 0x00;
    m4.ram[FAKE_STORE_CPU_PORT] = 0x00;
    m4.arm_hard_vector_capture_context(0xE101, 0xF8, 0x11, 0x22, 0x33, 0x24,
                                       0x00, 0x35);
    debug_predict(0xE100, bytes, false, &pred);
    r = m4.trace_at(0xE100, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Hard-vector BRK capture with unreadable DDR must complete")) return 1;
    if (expect(ctx.live_cpu_port_valid && ctx.live_cpu_port == 0x05,
               "Hard-vector BRK capture with unreadable DDR must keep the execution bank")) return 1;
    if (expect(ctx.cpu_port_registers_valid &&
               (ctx.cpu_ddr & 0x07) == 0x07 &&
               (ctx.cpu_port_latch & 0x07) == 0x05,
               "Hard-vector unreadable DDR fallback must resume in the captured execution bank")) return 1;

    return 0;
}

static int test_parked_step_resume_restores_captured_cpu_port_registers()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);
    fake_seed_captured_context(m, 0x2001, 0xF8, 0x11, 0x22, 0x33, 0x24);
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Initial step must park a captured context")) return 1;
    if (expect(ctx.cpu_port_registers_valid && ctx.cpu_ddr == 0x37 &&
               ctx.cpu_port_latch == 0x35,
               "Initial capture must retain the program CPU-port registers")) return 1;

    m.ram[0x0000] = 0x00;
    m.ram[0x0001] = 0x07;
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;
    m.arm_capture_context(0x2002, 0xF8, 0x11, 0x22, 0x33, 0x24);
    debug_predict(0x2001, bytes, false, &pred);

    DebugContext next;
    r = m.over(ctx, pred, &next);
    if (expect(r == DebugSession::DBG_OK,
               "Parked Step Over must complete after a CPU-port clobber")) return 1;
    if (expect(m.ram[FAKE_STORE_CPU_DDR] == 0x37 &&
               m.ram[FAKE_STORE_CPU_PORT] == 0x35,
               "Parked step resume must stage captured $00/$01 for the trampoline")) return 1;
    if (expect(m.ram[FAKE_TRAMPOLINE + 0] == 0xAD &&
               m.ram[FAKE_TRAMPOLINE + 1] == (uint8_t)(FAKE_STORE_CPU_DDR & 0xFF) &&
               m.ram[FAKE_TRAMPOLINE + 2] == (uint8_t)(FAKE_STORE_CPU_DDR >> 8) &&
               m.ram[FAKE_TRAMPOLINE + 3] == 0x85 &&
               m.ram[FAKE_TRAMPOLINE + 4] == 0x00 &&
               m.ram[FAKE_TRAMPOLINE + 5] == 0xAD &&
               m.ram[FAKE_TRAMPOLINE + 6] == (uint8_t)(FAKE_STORE_CPU_PORT & 0xFF) &&
               m.ram[FAKE_TRAMPOLINE + 7] == (uint8_t)(FAKE_STORE_CPU_PORT >> 8) &&
               m.ram[FAKE_TRAMPOLINE + 8] == 0x85 &&
               m.ram[FAKE_TRAMPOLINE + 9] == 0x01,
               "Parked step trampoline must restore $00/$01 in CPU-executed code")) return 1;
    return 0;
}

static int test_no_breakpoint_continue_restores_captured_cpu_port_registers()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2100);
    fake_seed_captured_context(m, 0x2101, 0xF8, 0x44, 0x55, 0x66, 0x24);
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2100, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2100, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Initial step must park a context for continue")) return 1;

    m.ram[0x0000] = 0x00;
    m.ram[0x0001] = 0x07;
    r = m.go(ctx, 0, ctx.pc);
    if (expect(r == DebugSession::DBG_OK,
               "No-breakpoint continue must release the parked context")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 0] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 1] == 0x37 &&
               m.ram[FAKE_RESUME_TRAMP + 2] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 3] == 0x00 &&
               m.ram[FAKE_RESUME_TRAMP + 4] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 5] == 0x35 &&
               m.ram[FAKE_RESUME_TRAMP + 6] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 7] == 0x01,
               "No-breakpoint continue stub must restore $00/$01 in CPU-executed code")) return 1;
    return 0;
}

// Regression: a plain G issued after single-stepping passes NO freshly captured
// context (resume_from.valid == false); the session must still resume through
// the banking-preserving register-restore stub using its parked last_context,
// NOT fall back to free_run_no_breakpoint()/jump_to(). The latter vectors
// through the (absent) KERNAL NMI handler and silently fails to execute a
// program running with KERNAL banked out (RAM under ROM). See the no-breakpoint
// branch in BrkDebugSession::go().
static int test_no_breakpoint_continue_after_step_without_passed_context()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2100);
    fake_seed_captured_context(m, 0x2101, 0xF8, 0x44, 0x55, 0x66, 0x24);
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2100, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0x2100, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Initial step must park a context for the no-context continue")) return 1;

    // Wipe the resume stub area so we can prove the Go rebuilds it.
    for (int i = 0; i < 16; i++) m.ram[FAKE_RESUME_TRAMP + i] = 0x00;
    m.ram[0x0000] = 0x00;
    m.ram[0x0001] = 0x07;

    // Plain G with NO passed context: this is what the monitor sends when the
    // user presses G after stepping (it relies on the session's parked context).
    DebugContext empty;
    debug_context_reset(&empty);
    r = m.go(empty, 0, ctx.pc);
    if (expect(r == DebugSession::DBG_OK,
               "No-breakpoint continue without a passed context must succeed")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 0] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 1] == 0x37 &&
               m.ram[FAKE_RESUME_TRAMP + 2] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 3] == 0x00 &&
               m.ram[FAKE_RESUME_TRAMP + 4] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 5] == 0x35 &&
               m.ram[FAKE_RESUME_TRAMP + 6] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 7] == 0x01,
               "No-context continue after a step must resume via the banking-"
               "preserving stub (parked last_context), not the KERNAL-NMI fallback")) return 1;
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

    // Model a trap at the installed fall-through BRK ($E001) (see arm_capture note).
    m.arm_capture_context(0xE001, 0xF8, 0x11, 0x22, 0x33, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL Step Into must succeed when ROM patching is supported")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Visible KERNAL Step Into patch byte must be restored")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Visible KERNAL Step Into must not patch RAM under ROM")) return 1;
    // 4 ROM-image writes: install BRK + recommit launch-PC fetch byte ($E000 is
    // visible ROM) + recommit BRK + restore.
    if (expect(m.rom_patch_writes == 4,
               "Visible KERNAL Step Into must patch, recommit launch byte + BRK, and restore the ROM image")) return 1;
    if (expect(m.brk_patch_writes == 2,
               "Visible KERNAL Step Into must re-commit the ROM BRK as the final pre-launch write")) return 1;
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

    // Step-over a 3-byte JSR places the BRK at the fall-through ($E003); model the
    // trap there so the relaunch backstop sees a legitimate capture.
    m.arm_capture_context(0xE003, 0xF8, 0x11, 0x22, 0x33, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.over_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL Step Over JSR must patch the ROM fall-through target")) return 1;
    if (expect(m.kernal_rom[3] == 0xEA,
               "Visible KERNAL Step Over JSR must restore fall-through ROM byte")) return 1;
    if (expect(m.ram[0xE003] == 0x7B,
               "Visible KERNAL Step Over JSR must not patch RAM under ROM")) return 1;
    // patch fall-through BRK + launch-PC ($E000 JSR) fetch-byte recommit + BRK
    // recommit + restore = 4 (launch PC is visible ROM, so its byte recommits too).
    if (expect(m.rom_patch_writes == 4,
               "Visible KERNAL Step Over JSR must patch, recommit launch byte + BRK, and restore ROM")) return 1;
    return 0;
}

static int test_visible_rom_sustained_settle_does_not_launch_before_sentinel_clear()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;
    m.kernal_rom[0] = 0x20;       // JSR $E100
    m.kernal_rom[1] = 0x00;
    m.kernal_rom[2] = 0xE1;
    m.kernal_rom[0x100] = 0xEA;   // captured visible-ROM target
    m.kernal_rom[0x101] = 0xEA;   // next Step Into fall-through

    uint8_t first_bytes[3] = { 0x20, 0x00, 0xE1 };
    DebugPredictResult first_pred;
    debug_predict(0xE000, first_bytes, false, &first_pred);
    m.arm_capture_context(0xE100, 0xF6, 0x37, 0x00, 0x00, 0x24);

    DebugContext from;
    DebugSession::Result r = m.trace_at(0xE000, first_pred, &from);
    if (expect(r == DebugSession::DBG_OK && from.valid && from.pc == 0xE100,
               "Setup visible-ROM JSR Step Into must park at the callee")) return 1;

    m.settle_calls = 0;
    m.last_settle_sustained = false;
    m.saw_sustained_settle = false;
    m.settle_spin_lo = 0;
    m.settle_spin_hi = 0;
    m.mark_visible_rom_hit_for_test(from.pc);

    uint8_t next_bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult next_pred;
    debug_predict(0xE100, next_bytes, false, &next_pred);
    m.arm_capture_context(0xE101, 0xF6, 0x37, 0x00, 0x00, 0x24);

    DebugContext ctx;
    r = m.trace(from, next_pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible-ROM parked Step Into after a breakpoint hit must complete")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xE101,
               "Visible-ROM parked Step Into must stop at fall-through")) return 1;
    if (expect(m.settle_calls > 0,
               "Visible-ROM breakpoint relaunch must call the settle hook")) return 1;
    if (expect(m.saw_sustained_settle,
               "Visible-ROM breakpoint relaunch must use the sustained settle")) return 1;
    if (expect(m.settle_spin_lo == (uint8_t)(FAKE_SPIN_JMP & 0xFF) &&
               m.settle_spin_hi == (uint8_t)(FAKE_SPIN_JMP >> 8),
               "Sustained visible-ROM settle must not arm the restore trampoline early")) return 1;
    return 0;
}

// Run a contextless bp+Go toward a visible-ROM breakpoint from a RAM bootstrap.
// Returns the number of sustained fetch-path settles performed. When
// force_first_miss is set, the first launch traps at an unrelated $00 so the
// self-heal relaunch backstop fires; otherwise the first launch traps at the
// installed breakpoint and no relaunch occurs.
static int run_contextless_visible_rom_go_settles(bool force_first_miss)
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;                 // KERNAL visible
    m.kernal_rom[1] = 0xEA;            // byte under the $E001 breakpoint
    m.ram[0xC000] = 0xEA;             // RAM bootstrap the NMI redirects toward

    MonitorBreakpoints bps;
    bps.allocate(0xE001, 0x07);        // visible-ROM (high-memory) breakpoint

    DebugContext from;
    debug_context_reset(&from);        // contextless launch (bp+Go)

    if (force_first_miss) {
        // First launch traps at a PC that matches no installed patch, so the
        // relaunch backstop treats it as a runaway and re-issues the launch.
        m.arm_capture_context(0xE050, 0xF8, 0, 0, 0, 0x24);
    }
    m.settle_calls = 0;
    m.sustained_settle_calls = 0;
    m.saw_sustained_settle = false;

    m.go(from, &bps, 0xC000);
    return m.sustained_settle_calls;
}

// Regression for the contextless visible-ROM relaunch settle gate. A bp+Go
// toward a visible-ROM breakpoint can miss on the first launch (the freshly
// DMA-committed ROM-image BRK lags the live 6510 fetch path), so the self-heal
// relaunch must settle the fetch path again before re-redirecting - exactly as
// the initial contextless launch does. The relaunch previously gated that
// settle on nmi_force_cpu_port, which is derived from the RAM-bootstrap launch
// PC and is therefore false here, so the settle was skipped and the relaunch
// re-issued the same NMI redirect that had just missed. Gating on the installed
// breakpoint bank (has_visible_rom_patch()) restores the settle. Proven as a
// differential: the relaunch path must add at least one sustained settle over
// the no-relaunch baseline.
static int test_contextless_visible_rom_relaunch_resettles_fetch_path()
{
    int baseline = run_contextless_visible_rom_go_settles(false);
    int with_relaunch = run_contextless_visible_rom_go_settles(true);
    if (expect(baseline >= 1,
               "Contextless visible-ROM launch must settle the fetch path once")) return 1;
    if (expect(with_relaunch > baseline,
               "Contextless visible-ROM relaunch must re-settle the fetch path")) return 1;
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

    // Model the run trapping at the installed breakpoint ($E001) so the relaunch
    // backstop recognises a legitimate capture (see test_visible_basic_step note).
    fake_seed_captured_context(m, 0xE001, 0xF8, 0x11, 0x22, 0x33, 0x24);

    DebugSession::Result r = m.go(from, &bps, 0xC000);
    if (expect(r == DebugSession::DBG_OK,
               "Visible ROM breakpoint Go must run when ROM patching is supported")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Visible ROM breakpoint Go must restore the ROM byte")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Visible ROM breakpoint Go must not use RAM under ROM as proof")) return 1;
    if (expect(m.rom_patch_writes == 3,
               "Visible ROM breakpoint Go must patch, recommit at launch, and restore the ROM image")) return 1;
    return 0;
}

static int test_mixed_kernal_and_ram_breakpoints_patch_distinct_backing_stores()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x05; // live CPU has KERNAL banked out
    m.ram[0xC000] = 0xEA;
    m.kernal_rom[0] = 0x4C;
    m.ram[0xE000] = 0xEA;

    MonitorBreakpoints bps;
    int krn = bps.allocate(0xE000, 0x07);
    int ram = bps.allocate(0xE000, 0x05);
    if (expect(krn != ram && krn >= 0 && ram >= 0,
               "KERNAL and RAM-under-KERNAL breakpoints must coexist")) return 1;
    bps.set_enabled(krn, false);
    if (expect(!bps.get(krn)->enabled && bps.get(ram)->enabled,
               "Disabling KERNAL breakpoint must leave RAM breakpoint enabled")) return 1;
    bps.set_enabled(krn, true);

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xC000;
    from.sp = 0xF7;
    from.sr = 0x24;

    m.arm_capture_context(0xE000, 0xF5, 0, 0, 0, 0x24);
    DebugSession::Result r = m.go(from, &bps, 0xC000);
    if (expect(r == DebugSession::DBG_OK,
               "Mixed KERNAL/RAM breakpoints must run and trap")) return 1;
    if (expect(m.last_rom_patch_addr == 0xE000 && m.rom_patch_writes == 3,
               "KERNAL breakpoint must patch, recommit at launch, and restore the ROM image")) return 1;
    if (expect(m.last_ram_patch_addr == 0xE000 && m.ram_patch_writes == 1,
               "RAM-under-KERNAL breakpoint must patch hidden RAM")) return 1;
    if (expect(m.kernal_rom[0] == 0x4C && m.ram[0xE000] == 0xEA,
               "Mixed breakpoint cleanup must restore both backing stores")) return 1;

    char row[40];
    MonitorBreakpoints::format_popup_row(row, sizeof(row), krn, bps.get(krn));
    if (expect(strstr(row, "$E000 KRN") != NULL,
               "Breakpoint popup must distinguish the KERNAL target")) return 1;
    MonitorBreakpoints::format_popup_row(row, sizeof(row), ram, bps.get(ram));
    if (expect(strstr(row, "$E000 RAM") != NULL,
               "Breakpoint popup must distinguish the RAM target")) return 1;
    return 0;
}

static int test_patch_restore_uses_recorded_destination_after_cpu_bank_changes()
{
    FakeVisibleRomMachine m(false);
    fake_seed_rom_nop_run(m, 0xE000);
    m.allow_visible_rom_patching = true;
    m.cpu_port = 0x07;
    m.ram[0xE001] = 0x6C;
    m.switch_cpu_port_on_delay = true;
    m.cpu_port_after_delay = 0x05;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL step must complete while live bank changes before cleanup")) return 1;
    if (expect(m.cpu_port == 0x05,
               "Test must change the live CPU bank before patch restoration")) return 1;
    if (expect(m.kernal_rom[1] == 0xEA,
               "Patch restoration must use the recorded KERNAL destination")) return 1;
    if (expect(m.ram[0xE001] == 0x6C,
               "Patch restoration must not recompute and restore into hidden RAM")) return 1;

    m.cleanup();
    m.cleanup();
    if (expect(m.kernal_rom[1] == 0xEA && m.ram[0xE001] == 0x6C,
               "Patch cleanup must be idempotent after the bank changed")) return 1;
    return 0;
}

static int test_kernal_out_hard_vector_installs_and_restores_on_cleanup()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x05;
    m.ram[FAKE_HARD_VECTOR_LO] = 0x34;
    m.ram[FAKE_HARD_VECTOR_HI] = 0x12;
    m.ram[0xE000] = 0xEA;
    m.ram[0xE001] = 0xEA;
    m.ram[0xE002] = 0xEA;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "RAM-under-KERNAL step must trap without Debug Timeout")) return 1;
    if (expect(m.ram[FAKE_HARD_VECTOR_LO] == (uint8_t)(FAKE_HARD_BRK_STUB & 0xFF) &&
               m.ram[FAKE_HARD_VECTOR_HI] == (uint8_t)(FAKE_HARD_BRK_STUB >> 8),
               "KERNAL-out hard IRQ/BRK vector must point at the debug stub while parked")) return 1;
    if (expect(m.ram_patch_writes == 1 && m.last_ram_patch_addr == 0xE001,
               "RAM-under-KERNAL step must patch hidden RAM fall-through")) return 1;
    if (expect(m.ram[0xE001] == 0xEA,
               "RAM-under-KERNAL patch byte must be restored after the step")) return 1;

    m.cleanup();
    if (expect(m.ram[FAKE_HARD_VECTOR_LO] == 0x34 &&
               m.ram[FAKE_HARD_VECTOR_HI] == 0x12,
               "Cleanup must restore hidden-RAM hard vector bytes")) return 1;
    return 0;
}

static int test_kernal_out_hard_vector_restores_on_timeout()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x05;
    m.ram[FAKE_HARD_VECTOR_LO] = 0x78;
    m.ram[FAKE_HARD_VECTOR_HI] = 0x56;
    m.ram[0xE000] = 0xEA;
    m.ram[0xE001] = 0xEA;
    m.ram[0xE002] = 0xEA;
    m.sentinel_armed = false;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_TIMEOUT,
               "Debug Timeout must remain a failure result")) return 1;
    if (expect(m.ram[FAKE_HARD_VECTOR_LO] == 0x78 &&
               m.ram[FAKE_HARD_VECTOR_HI] == 0x56,
               "Timeout cleanup must restore hidden-RAM hard vector bytes")) return 1;
    if (expect(m.ram[0xE001] == 0xEA,
               "Timeout cleanup must restore RAM-under-KERNAL patch bytes")) return 1;
    return 0;
}

static int test_visible_kernal_hard_vector_installs_and_restores()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x07;
    m.allow_visible_rom_patching = true;
    m.kernal_rom[0x1FFE] = 0x66;
    m.kernal_rom[0x1FFF] = 0xFE;
    m.kernal_rom[0x0000] = 0xEA;
    m.kernal_rom[0x0001] = 0xEA;
    m.kernal_rom[0x0002] = 0xEA;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Visible KERNAL step must trap through the direct hard vector")) return 1;
    if (expect(m.kernal_rom[0x1FFE] == (uint8_t)(FAKE_HARD_BRK_STUB & 0xFF) &&
               m.kernal_rom[0x1FFF] == (uint8_t)(FAKE_HARD_BRK_STUB >> 8),
               "Visible KERNAL hard IRQ/BRK vector must point at the debug stub while parked")) return 1;
    if (expect(m.kernal_rom[0x0001] == 0xEA,
               "Visible KERNAL patch byte must be restored after the step")) return 1;

    m.cleanup();
    if (expect(m.kernal_rom[0x1FFE] == 0x66 &&
               m.kernal_rom[0x1FFF] == 0xFE,
               "Cleanup must restore visible KERNAL hard vector bytes")) return 1;
    return 0;
}

static int test_stale_visible_kernal_hard_vector_is_recovered()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x07;
    m.allow_visible_rom_patching = true;
    m.kernal_rom[0x1FFE] = (uint8_t)(FAKE_HARD_BRK_STUB & 0xFF);
    m.kernal_rom[0x1FFF] = (uint8_t)(FAKE_HARD_BRK_STUB >> 8);
    m.ram[0xC000] = 0xEA;
    m.ram[0xC001] = 0xEA;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xC000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xC000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "RAM-only debug step must recover a stale visible KERNAL hard vector")) return 1;
    if (expect(m.kernal_rom[0x1FFE] == 0x48 &&
               m.kernal_rom[0x1FFF] == 0xFF,
               "Stale visible KERNAL hard vector must be restored before RAM-only stepping")) return 1;

    m.cleanup();
    if (expect(m.kernal_rom[0x1FFE] == 0x48 &&
               m.kernal_rom[0x1FFF] == 0xFF,
               "Cleanup must not reinstall the stale visible KERNAL hard vector")) return 1;
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

static int test_run_to_lagging_visible_rom_uses_step_bytes_for_trampoline()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    m.kernal_rom[0x0005] = 0xA5; // LDA $61 at $E005
    m.kernal_rom[0x0006] = 0x61;
    m.kernal_rom[0x0007] = 0xEA;
    m.force_raw_peek_brk = true;
    m.force_raw_peek_brk_addr = 0xE005;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE005;
    from.sp = 0xF7;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    m.arm_capture_context(0x0342, 0xF7, 0, 0, 0, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.run_to(from, 0xE007, 0, 0xE005, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "Lagging visible-ROM run-to launch prime must complete")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xE007,
               "Lagging visible-ROM run-to must report the real fall-through")) return 1;
    if (expect(m.ram[0x0340] == 0xA5 && m.ram[0x0341] == 0x61,
               "Run-to trampoline must copy coherent step bytes, not stale peek_cpu BRK")) return 1;
    return 0;
}

static int test_overlay_step_over_never_freezes()
{
    // Overlay-mode non-regression: the machine is never frozen, so the engine
    // must neither unfreeze nor re-freeze, and must still complete the step.
    FakeFreezeMachine m(false);
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);

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
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);
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
    m.set_run_window_refreeze_enabled(false);
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
    if (expect(m.ram[FAKE_RESUME_TRAMP + 0] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 1] == 0x07 &&
               m.ram[FAKE_RESUME_TRAMP + 2] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 3] == 0x00 &&
               m.ram[FAKE_RESUME_TRAMP + 4] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 5] == 0x07 &&
               m.ram[FAKE_RESUME_TRAMP + 6] == 0x85 &&
               m.ram[FAKE_RESUME_TRAMP + 7] == 0x01,
               "Cleanup trampoline must restore captured $00/$01 before returning")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 8] == 0xA2 &&
               m.ram[FAKE_RESUME_TRAMP + 9] == 0xF9 &&
               m.ram[FAKE_RESUME_TRAMP + 10] == 0x9A,
               "Cleanup trampoline must restore SP before returning")) return 1;
    // SR pushed for RTI is the captured 0xA4 with the I flag cleared (0xA0):
    // KERNAL is mapped here ($01=$07), so the hand-back re-enables interrupts so
    // the C64 returns to a live runtime instead of resuming with the cursor/
    // keyboard/jiffy dead. See test_cleanup_resume_preserves_interrupts_when
    // _kernal_banked_out for the complementary KERNAL-out (I preserved) case.
    if (expect(m.ram[FAKE_RESUME_TRAMP + 11] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 12] == 0x20 &&
               m.ram[FAKE_RESUME_TRAMP + 14] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 15] == 0x01 &&
               m.ram[FAKE_RESUME_TRAMP + 17] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 18] == 0xA0,
               "Cleanup trampoline must push PCH, PCL, then SR (I re-enabled) for RTI")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 20] == 0xA0 &&
               m.ram[FAKE_RESUME_TRAMP + 21] == 0x99 &&
               m.ram[FAKE_RESUME_TRAMP + 22] == 0xA2 &&
               m.ram[FAKE_RESUME_TRAMP + 23] == 0x17 &&
               m.ram[FAKE_RESUME_TRAMP + 24] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 25] == 0x42 &&
               m.ram[FAKE_RESUME_TRAMP + 26] == 0x40,
               "Cleanup trampoline must restore Y, X, A and finish with RTI")) return 1;
    return 0;
}

// Complement of the test above: when KERNAL is banked OUT ($01 HIRAM clear) there
// is no IRQ handler at $FFFE, so the resumed program legitimately runs with
// interrupts masked. The hand-back must PRESERVE the captured I flag in that case
// (clearing it would wedge the banked program worse than the freeze it avoids).
static int test_cleanup_resume_preserves_interrupts_when_kernal_banked_out()
{
    FakeFreezeMachine m(false);
    fake_seed_nop_run(m, 0x2000);
    fake_seed_captured_context(m, 0x2001, 0xF9, 0x42, 0x17, 0x99, 0xA4);
    // Override the seeded banking to KERNAL-out (HIRAM clear) before the step so
    // the captured context resumes with KERNAL unmapped.
    m.ram[FAKE_STORE_CPU_DDR] = 0x07;
    m.ram[FAKE_STORE_CPU_PORT] = 0x05;   // %101: HIRAM clear -> KERNAL banked out

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, bytes, false, &pred);

    DebugContext next;
    if (expect(m.over_at(0x2000, pred, &next) == DebugSession::DBG_OK,
               "Setup step must succeed before KERNAL-out cleanup")) return 1;

    m.cleanup();

    if (expect(m.ram[FAKE_RESUME_TRAMP + 5] == 0x05,
               "KERNAL-out resume must restore the banked-out $01")) return 1;
    if (expect(m.ram[FAKE_RESUME_TRAMP + 17] == 0xA9 &&
               m.ram[FAKE_RESUME_TRAMP + 18] == 0xA4,
               "KERNAL-out resume must PRESERVE the I flag (no $FFFE handler)")) return 1;
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

static int test_kernal_out_cold_nmi_launch_installs_hard_nmi_vector()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x05;
    m.ram[FAKE_HARD_NMI_VECTOR_LO] = 0x34;
    m.ram[FAKE_HARD_NMI_VECTOR_HI] = 0x12;
    fake_seed_nop_run(m, 0xE000);
    // Model the trap at the installed fall-through BRK $E001 (the relaunch backstop
    // requires the captured PC to be at an installed BRK).
    fake_seed_captured_context(m, 0xE001, 0xF0, 0x11, 0x22, 0x33, 0x24);
    m.arm_hard_vector_capture_context(0xE001, 0xF0, 0x11, 0x22, 0x33, 0x24,
                                      0x37, 0x35);

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "KERNAL-out cold NMI launch must complete")) return 1;
    if (expect(m.nmi_pulses == 1,
               "Cold no-context KERNAL-out step must launch via NMI")) return 1;
    if (expect(m.ram[FAKE_HARD_NMI_VECTOR_LO] == (uint8_t)(FAKE_NMI_TRAMP & 0xFF) &&
               m.ram[FAKE_HARD_NMI_VECTOR_HI] == (uint8_t)(FAKE_NMI_TRAMP >> 8),
               "KERNAL-out NMI hard vector must point at the debug trampoline while parked")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xE001 && ctx.live_cpu_port == 0x05,
               "KERNAL-out cold NMI launch must capture the RAM-under-KERNAL stop")) return 1;

    m.cleanup();
    if (expect(m.ram[FAKE_HARD_NMI_VECTOR_LO] == 0x34 &&
               m.ram[FAKE_HARD_NMI_VECTOR_HI] == 0x12,
               "Cleanup must restore hidden-RAM NMI hard vector bytes")) return 1;
    return 0;
}

static int test_kernal_out_cold_nmi_launch_restores_hard_nmi_vector_on_timeout()
{
    FakeVisibleRomMachine m(false);
    m.cpu_port = 0x05;
    m.ram[FAKE_HARD_NMI_VECTOR_LO] = 0x78;
    m.ram[FAKE_HARD_NMI_VECTOR_HI] = 0x56;
    fake_seed_nop_run(m, 0xE000);
    m.sentinel_armed = false;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0xE000, bytes, false, &pred);

    DebugContext ctx;
    DebugSession::Result r = m.trace_at(0xE000, pred, &ctx);
    if (expect(r == DebugSession::DBG_TIMEOUT,
               "KERNAL-out cold NMI timeout must remain a failure")) return 1;
    if (expect(m.ram[FAKE_HARD_NMI_VECTOR_LO] == 0x78 &&
               m.ram[FAKE_HARD_NMI_VECTOR_HI] == 0x56,
               "Timeout cleanup must restore hidden-RAM NMI hard vector bytes")) return 1;
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

static int test_freeze_cleanup_preserves_resume_bytes_across_unfreeze_restore()
{
    FakeFreezeMachine m(true);
    fake_seed_nop_run(m, 0x2100);
    fake_seed_captured_context(m, 0x2101, 0xF3, 0x33, 0x44, 0x55, 0x66);
    m.ram[FAKE_STORE_CPU_DDR] = 0x37;
    m.ram[FAKE_STORE_CPU_PORT] = 0x35;

    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2100, bytes, false, &pred);

    DebugContext next;
    if (expect(m.over_at(0x2100, pred, &next) == DebugSession::DBG_OK,
               "Initial frozen step with CPU5 context must succeed")) return 1;

    m.reset_freeze_restore_pokes();
    m.cleanup();

    if (expect(m.recorded_freeze_restore_poke(FAKE_STORE_CPU_DDR, 0x37) &&
               m.recorded_freeze_restore_poke(FAKE_STORE_CPU_PORT, 0x35),
               "Frozen cleanup must preserve captured CPU-port stores across unfreeze restore")) return 1;
    if (expect(m.recorded_freeze_restore_poke(0x0000, 0x37) &&
               m.recorded_freeze_restore_poke(0x0001, 0x35),
               "Frozen cleanup must preserve actual $00/$01 across unfreeze restore")) return 1;
    if (expect(m.recorded_freeze_restore_poke(FAKE_RESUME_TRAMP + 0, 0xA9) &&
               m.recorded_freeze_restore_poke(FAKE_RESUME_TRAMP + 1, 0x37) &&
               m.recorded_freeze_restore_poke(FAKE_RESUME_TRAMP + 4, 0xA9) &&
               m.recorded_freeze_restore_poke(FAKE_RESUME_TRAMP + 5, 0x35),
               "Frozen cleanup must preserve the CPU-executed $00/$01 restore stub")) return 1;
    if (expect(m.recorded_freeze_restore_poke(FAKE_SPIN_LO,
                                              (uint8_t)(FAKE_RESUME_TRAMP & 0xFF)) &&
               m.recorded_freeze_restore_poke(FAKE_SPIN_HI,
                                              (uint8_t)(FAKE_RESUME_TRAMP >> 8)),
               "Frozen cleanup must preserve the spin redirection across unfreeze restore")) return 1;
    return 0;
}

static int test_freeze_go_without_breakpoint_defers_handoff_until_after_exit()
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
    backend.parked_context_handoff_supported = true;
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
               "Freeze no-breakpoint G must defer execution instead of calling session->go immediately")) return 1;

    if (expect(monitor.has_deferred_debug_go(),
               "Freeze no-breakpoint G must mark a deferred debug handoff after exit")) return 1;
    if (expect(!monitor.consume_pending_go(NULL, NULL, NULL),
               "Freeze no-breakpoint G must not use the generic pending-Go handoff channel")) return 1;
    if (expect(monitor.consume_release_host_after_exit(),
               "Freeze no-breakpoint G must request host release after exit")) return 1;
    if (expect(!monitor.consume_reopen_after_reset(),
               "Freeze no-breakpoint G exit must not look like reset re-entry")) return 1;
    monitor.dispatch_deferred_debug_go();
    if (expect(!monitor.has_deferred_debug_go(),
               "Dispatching the deferred Freeze no-breakpoint G must clear the deferred-debug flag")) return 1;
    monitor.deinit();
    return 0;
}

static int test_direct_overlay_go_without_breakpoint_releases_context_and_exits()
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
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 1,
               "Direct Overlay G without breakpoints must unwind out of the monitor")) return 1;
    if (expect(backend.last_session && backend.last_session->go_calls == 1,
               "Direct Overlay no-breakpoint G must release through the debug session")) return 1;

    DebugContext go_ctx;
    bool has_ctx = false;
    uint16_t go_addr = 0;
    if (expect(!monitor.consume_pending_go(&go_addr, &go_ctx, &has_ctx),
               "Direct Overlay no-breakpoint G must not schedule a second handoff")) return 1;
    if (expect(monitor.consume_release_host_after_exit(),
               "Direct Overlay no-breakpoint G must request host release after exit")) return 1;
    monitor.deinit();
    return 0;
}

static int test_direct_go_exits_monitor_and_requests_host_release()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.snapshot_result = DebugSession::DBG_OK;
    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xE006;
    backend.canned_snapshot.sp = 0xF1;
    backend.canned_snapshot.sr = 0x44;
    backend.canned_snapshot.live_cpu_port_valid = true;
    backend.canned_snapshot.live_cpu_port = 0x05;
    backend.write(0xE006, 0x4C);
    backend.write(0xE007, 0x00);
    backend.write(0xE008, 0xE0);

    const int keys[] = { 'A', 'D', 'G' };
    FakeKeyboard keyboard(keys, 3);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(true);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM view ok")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug ok")) return 1;
    if (expect(monitor.poll(0) == 1,
               "Direct G must unwind out of the monitor")) return 1;
    if (expect(!monitor.consume_pending_go(NULL, NULL, NULL),
               "Direct G must not schedule a second handoff")) return 1;
    if (expect(monitor.consume_release_host_after_exit(),
               "Direct G must request host release after exit")) return 1;
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

static int test_wait_for_sentinel_aborts_immediately_on_reset_cancel()
{
    FakeFreezeMachine m(false);
    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    DebugContext ctx;

    fake_seed_nop_run(m, 0x2000);
    m.sentinel_armed = false;
    m.reset_cancel_on_delay = true;
    debug_predict(0x2000, bytes, false, &pred);
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_RESET,
               "Sentinel wait must return DBG_RESET when reset cancellation is requested")) return 1;
    if (expect(m.delay_calls == 1,
               "Reset cancellation must stop the wait immediately instead of running to timeout")) return 1;
    if (expect(m.reset_calls == 0,
               "External reset cancellation must not issue a second machine reset")) return 1;
    return 0;
}

static int test_request_reset_cancel_clears_state_and_makes_handler_pokes_during_delay()
{
    FakeFreezeMachine m(false);
    uint8_t bytes[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    DebugContext ctx;

    // Arrange: a trace installs the BRK handler. The sentinel never fires,
    // and the external-cancel signal is delivered inside delay_ms(), simulating
    // the REST task calling request_reset_cancel() while the UI task is
    // suspended in vTaskDelay on real FreeRTOS hardware.
    fake_seed_nop_run(m, 0x2000);
    m.sentinel_armed = false;
    m.reset_cancel_on_delay = true;
    debug_predict(0x2000, bytes, false, &pred);
    int pokes_before = m.freeze_restore_pokes;
    DebugSession::Result r = m.trace_at(0x2000, pred, &ctx);
    if (expect(r == DebugSession::DBG_RESET,
               "Sentinel wait must return DBG_RESET on external cancel")) return 1;
    // With synchronous teardown in request_reset_cancel(), the handler is
    // uninstalled (via poke_visible_preserving_freeze_restore) during the
    // delay_ms() call itself, before the second uninstall attempt from
    // wait_for_sentinel() (which becomes a no-op). The poke count must be
    // non-zero, proving teardown occurred.
    if (expect(m.freeze_restore_pokes > pokes_before,
               "request_reset_cancel must uninstall the handler (poke_visible calls expected)")) return 1;
    if (expect(m.pokes_at_cancel == m.freeze_restore_pokes,
               "All handler pokes must complete inside request_reset_cancel before machine reset fires")) return 1;
    if (expect(m.reset_calls == 0,
               "External reset cancellation must not issue a second machine reset")) return 1;
    if (expect(m.delay_calls == 1,
               "Wait must stop after the first delay once cancel is signalled")) return 1;
    return 0;
}

static int test_debug_ownership_blocks_second_stakeholder_until_remote_expires()
{
    FakeFreezeMachine telnet_owner(false);
    FakeFreezeMachine local_owner(false);
    FakeFreezeMachine leaked_local_owner(false);
    FakeFreezeMachine reclaiming_telnet_owner(false);

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

    set_fake_ms_timer(1000);
    if (expect(leaked_local_owner.claim_debug_ownership(false),
               "Initial local stakeholder must claim debug ownership")) return 1;
    advance_fake_ms_timer(3501);
    if (expect(reclaiming_telnet_owner.claim_debug_ownership(true),
               "A stale local owner must expire so a later telnet session can claim Debug")) return 1;
    reclaiming_telnet_owner.release_debug_ownership();
    leaked_local_owner.release_debug_ownership();
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
// In Debug footer labels, PC/A/X/Y/S and active flag letters use the primary
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

// ===========================================================================
// The central step chooser and the parked-context step contract. These drive
// the monitor (not the session directly) so the gate decisions are exercised
// end to end.
// ===========================================================================

static int alert_shape_ok(const char *s, const char *what)
{
    if (expect(s != NULL, what)) return 1;
    if (expect((int)strlen(s) <= 38, what)) return 1;
    if (expect(strchr(s, '\n') == NULL && strchr(s, '\r') == NULL, what)) return 1;
    return 0;
}

static void set_predict_bytes(StubDebugSession *s, uint8_t b0, uint8_t b1,
                              uint8_t b2)
{
    if (!s) return;
    s->predict_bytes_valid = true;
    s->predict_bytes[0] = b0;
    s->predict_bytes[1] = b1;
    s->predict_bytes[2] = b2;
}

static int test_debug_marker_shows_dbg()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    char header[40];
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "Dbg") != NULL, "Header must show Dbg")) return 1;
    if (expect(strstr(header, "DbX") == NULL, "DbX must never appear")) return 1;
    if (expect(strstr(header, "Db!") == NULL, "Db! must never appear")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_x_exits_monitor()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'X' };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 1, "X exits the monitor")) return 1;
    monitor.deinit();
    return 0;
}

static int test_x_in_edit_mode_is_edit_input()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.write(0x2000, 0xEA);
    // A: ASM, D: Debug, E: enter edit, X: must be edit input (no exit).
    const int keys[] = { 'A', 'D', 'E', 'X', KEY_BREAK, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Edit")) return 1;
    if (expect(monitor.poll(0) == 0, "X in edit must not exit")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_ctrl_x_reset_not_shadowed_by_x()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    const int keys[] = { 'A', 'D', KEY_CTRL_X, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_reset_exits_monitor(false);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    int resets_before = backend.reset_calls;
    monitor.poll(0); // C=+X
    if (expect(backend.reset_calls == resets_before + 1,
               "C=+X must still reset the machine")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_visible_rom_contextless_linear_step_over_stops()
{
    // A contextless Step Over of a non-JSR is the same immediate single step
    // as Step Into and must stop with guidance in a fetch-lagging bank.
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_debug_run_window_refreeze_enabled(true); // UI Freeze mode
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump to E000")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0xEA, 0xEA, 0xEA);
    if (expect(monitor.poll(0) == 0, "Step Over must stay in monitor")) return 1;
    if (expect(backend.last_session->over_at_breakpoint_calls == 0 &&
               backend.last_session->over_calls == 0 &&
               backend.last_session->over_at_calls == 0,
               "Contextless linear ROM Step Over must not dispatch")) return 1;
    if (expect(strcmp(monitor.debug_status_message(),
                      "Step Over: run to a breakpoint 1st") == 0,
               "Contextless linear ROM Step Over stops with guidance")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_visible_rom_contextless_jsr_step_over_runs()
{
    // Step Over of a JSR keeps its sustained breakpoint+Go completion even
    // without a context: the fall-through BRK is fetched only after the
    // callee's natural run.
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.set_debug_run_window_refreeze_enabled(true); // UI Freeze mode
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump to E000")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0x20, 0x0F, 0xBC); // JSR $BC0F
    if (expect(monitor.poll(0) == 0, "JSR Step Over must stay in monitor")) return 1;
    if (expect(backend.last_session->over_at_breakpoint_calls == 1,
               "Contextless ROM JSR Step Over dispatches")) return 1;
    if (expect(strcmp(monitor.debug_status_message(), "") == 0,
               "Contextless ROM JSR Step Over needs no alert")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_ram_under_rom_step_into_without_parked_context_stops()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xE000;
    backend.canned_snapshot.sp = 0xF8;
    backend.canned_snapshot.sr = 0x24;
    backend.canned_snapshot.live_cpu_port_valid = true;
    backend.canned_snapshot.live_cpu_port = 0x05; // KERNAL banked out: RAM under ROM

    const int keys[] = { 'A', 'D', 'T', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0xEA, 0xEA, 0xEA);
    backend.last_session->parked_context = false; // no parked CPU behind the ctx
    if (expect(monitor.poll(0) == 0, "Trace must stay in monitor")) return 1;
    int traces = backend.last_session->trace_calls + backend.last_session->trace_at_calls;
    if (expect(traces == 0,
               "RAM-under-ROM Step Into without a parked context must not run")) return 1;
    if (expect(strcmp(monitor.debug_status_message(),
                      "Step Into: run to a breakpoint 1st") == 0,
               "Stop alert must give breakpoint guidance")) return 1;
    if (alert_shape_ok(monitor.debug_status_message(), "RAM-under-ROM alert shape")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_ram_under_rom_step_into_with_parked_context_runs()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();

    backend.canned_snapshot_set = true;
    debug_context_reset(&backend.canned_snapshot);
    backend.canned_snapshot.valid = true;
    backend.canned_snapshot.pc = 0xE000;
    backend.canned_snapshot.sp = 0xF8;
    backend.canned_snapshot.sr = 0x24;
    backend.canned_snapshot.live_cpu_port_valid = true;
    backend.canned_snapshot.live_cpu_port = 0x05;

    const int keys[] = { 'A', 'D', 'T', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0xEA, 0xEA, 0xEA);
    backend.last_session->parked_context = true; // CPU parked in the debug spin
    if (expect(monitor.poll(0) == 0, "Trace runs")) return 1;
    int traces = backend.last_session->trace_calls + backend.last_session->trace_at_calls;
    if (expect(traces == 1,
               "RAM-under-ROM Step Into with a parked context must run")) return 1;
    if (expect(strcmp(monitor.debug_status_message(), "") == 0,
               "Parked RAM-under-ROM Step Into needs no alert")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_step_over_into_kernal_runs_without_alert()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("2000", 1); // RAM caller

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump to 2000")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0x20, 0x00, 0xE0); // JSR $E000 (KERNAL)
    if (expect(monitor.poll(0) == 0, "Step Over runs")) return 1;
    if (expect(backend.last_session->over_at_breakpoint_calls == 1,
               "Step Over from RAM into KERNAL must run")) return 1;
    if (expect(strcmp(monitor.debug_status_message(), "") == 0,
               "Step Over into KERNAL needs no alert")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

static int test_ram_step_over_is_direct()
{
    TestUserInterface ui;
    CaptureScreen screen;
    TrackingDebugBackend backend;
    monitor_reset_saved_state();
    backend.snapshot_result = DebugSession::DBG_NOT_SUPPORTED;

    const int keys[] = { 'J', 'A', 'D', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("2000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(monitor.poll(0) == 0, "Jump to 2000")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM switch")) return 1;
    if (expect(monitor.poll(0) == 0, "Enter Debug")) return 1;
    set_predict_bytes(backend.last_session, 0xEA, 0xEA, 0xEA);
    if (expect(monitor.poll(0) == 0, "Step Over runs")) return 1;
    if (expect(backend.last_session->over_at_breakpoint_calls == 1,
               "RAM Step Over must run directly")) return 1;
    if (expect(strcmp(monitor.debug_status_message(), "") == 0,
               "Plain RAM Step Over needs no alert")) return 1;
    monitor.poll(0);
    monitor.poll(0);
    monitor.deinit();
    return 0;
}

// ===========================================================================
// Parked-context architectural step emulation (session level). A control-flow
// step whose launch or landing sits in visible ROM or RAM-under-ROM must be
// completed while the CPU stays parked: no BRK planted, no CPU launch, real
// stack bytes written through the coherent DMA path.
// ===========================================================================

// Complete one plain-RAM step so the session considers the CPU parked in the
// debug spin loop; parked (emulated) steps are only legal in that state.
static int park_session_via_ram_step(FakeVisibleRomMachine &m)
{
    m.ram[0x2000] = 0xEA;
    m.ram[0x2001] = 0xEA;
    uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, nop, false, &pred);
    m.arm_capture_context(0x2001, 0xF8, 0x11, 0x22, 0x33, 0x24);
    DebugContext ctx;
    if (expect(m.trace_at(0x2000, pred, &ctx) == DebugSession::DBG_OK,
               "parking step must complete")) return 1;
    return 0;
}

static int test_parked_step_into_jsr_into_kernal_is_emulated()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0xFF; // JSR $FF00
    m.kernal_rom[0xFF00 - 0xE000] = 0x60; // RTS at the callee entry

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.a = 0x11;
    from.x = 0x22;
    from.y = 0x33;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0xFF };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    int brk_before = m.brk_patch_writes;
    int rom_before = m.rom_patch_writes;
    int nmi_before = m.nmi_pulses;

    DebugContext ctx;
    DebugSession::Result r = m.trace(from, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK, "emulated JSR trace completes")) return 1;
    if (expect(ctx.valid && ctx.pc == 0xFF00, "PC lands at the callee entry")) return 1;
    if (expect(ctx.sp == 0xF6, "SP drops by the JSR frame")) return 1;
    if (expect(m.ram[0x01F8] == 0x20 && m.ram[0x01F7] == 0x03,
               "the real stack holds the JSR return address")) return 1;
    if (expect(ctx.a == 0x11 && ctx.x == 0x22 && ctx.y == 0x33 && ctx.sr == 0x24,
               "registers and flags stay unchanged")) return 1;
    if (expect(m.brk_patch_writes == brk_before && m.rom_patch_writes == rom_before,
               "no BRK is planted anywhere")) return 1;
    if (expect(m.nmi_pulses == nmi_before, "the CPU is not launched")) return 1;

    // Round-trip: an RTS at the callee entry (visible-ROM launch) must come
    // back through the real stack bytes the emulated JSR wrote.
    uint8_t rts[3] = { 0x60, 0x00, 0x00 };
    DebugPredictResult pred_rts;
    debug_predict(0xFF00, rts, false, &pred_rts);
    DebugContext back;
    r = m.trace(ctx, pred_rts, &back);
    if (expect(r == DebugSession::DBG_OK, "emulated RTS trace completes")) return 1;
    if (expect(back.pc == 0x2004 && back.sp == 0xF8,
               "RTS returns to the JSR fall-through with SP restored")) return 1;
    if (expect(m.brk_patch_writes == brk_before && m.nmi_pulses == nmi_before,
               "the RTS is also completed while parked")) return 1;
    return 0;
}

static int test_parked_branch_in_ram_under_rom_is_emulated()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x05; // KERNAL banked out: $E000 is RAM under ROM
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0xE000] = 0xD0; // BNE +$10 -> $E012
    m.ram[0xE001] = 0x10;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE000;
    from.sp = 0xF8;
    from.sr = 0x20; // Z clear: branch taken
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x05;

    uint8_t bne[3] = { 0xD0, 0x10, 0x00 };
    DebugPredictResult pred;
    debug_predict(0xE000, bne, false, &pred);
    int brk_before = m.brk_patch_writes;
    int nmi_before = m.nmi_pulses;

    DebugContext ctx;
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_OK,
               "emulated taken branch completes")) return 1;
    if (expect(ctx.pc == 0xE012, "taken branch lands at the target")) return 1;

    from.sr = 0x22; // Z set: branch not taken
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_OK,
               "emulated not-taken branch completes")) return 1;
    if (expect(ctx.pc == 0xE002, "not-taken branch falls through")) return 1;
    if (expect(m.brk_patch_writes == brk_before && m.nmi_pulses == nmi_before,
               "RAM-under-ROM branches are stepped without BRK or launch")) return 1;
    return 0;
}

static int test_parked_jmp_indirect_emulation_wraps_vector_page()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x05;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0xE000] = 0x6C; // JMP ($10FF)
    m.ram[0xE001] = 0xFF;
    m.ram[0xE002] = 0x10;
    m.ram[0x10FF] = 0x34;
    m.ram[0x1000] = 0x12; // NMOS wrap: high byte from $1000, not $1100
    m.ram[0x1100] = 0x99; // decoy

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE000;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x05;

    uint8_t jmp[3] = { 0x6C, 0xFF, 0x10 };
    DebugPredictResult pred;
    debug_predict(0xE000, jmp, false, &pred);
    DebugContext ctx;
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_OK,
               "emulated indirect JMP completes")) return 1;
    if (expect(ctx.pc == 0x1234,
               "indirect JMP honours the NMOS page-wrap bug")) return 1;
    return 0;
}

static int test_parked_rts_without_frame_reports_not_in_subroutine()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x05;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0xE000] = 0x60; // RTS in RAM under ROM
    m.ram[0x01F9] = 0x33; // forged stack: $4433, caller byte is not a JSR
    m.ram[0x01FA] = 0x44;
    m.ram[0x4431] = 0xEA;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE000;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x05;

    uint8_t rts[3] = { 0x60, 0x00, 0x00 };
    DebugPredictResult pred;
    debug_predict(0xE000, rts, false, &pred);
    DebugContext ctx;
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_NOT_IN_SUBROUTINE,
               "emulated RTS keeps the active-frame guard")) return 1;
    return 0;
}

static int test_parked_plain_ram_jsr_step_into_runs_live()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0x30; // JSR $3000: launch and landing both plain RAM
    m.ram[0x3000] = 0xEA;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0x30 };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    int brk_before = m.brk_patch_writes;
    m.arm_capture_context(0x3000, 0xF6, 0x11, 0x22, 0x33, 0x24);

    DebugContext ctx;
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_OK,
               "plain-RAM JSR trace completes")) return 1;
    if (expect(m.brk_patch_writes > brk_before,
               "plain-RAM control flow still steps the live CPU")) return 1;
    if (expect(ctx.pc == 0x3000, "live step lands at the callee entry")) return 1;
    return 0;
}

static int test_unparked_unsafe_step_into_uses_live_path()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    // No parking step: a contextless trace has no authoritative register file,
    // so it must not be completed by context mutation.
    m.kernal_rom[0xE080 - 0xE000] = 0x20; // JSR $E100
    m.kernal_rom[0xE081 - 0xE000] = 0x00;
    m.kernal_rom[0xE082 - 0xE000] = 0xE1;
    m.kernal_rom[0xE100 - 0xE000] = 0xEA;

    uint8_t jsr[3] = { 0x20, 0x00, 0xE1 };
    DebugPredictResult pred;
    debug_predict(0xE080, jsr, false, &pred);
    int rom_before = m.rom_patch_writes;
    m.arm_capture_context(0xE100, 0xF6, 0, 0, 0, 0x24);

    DebugContext ctx;
    if (expect(m.trace_at(0xE080, pred, &ctx) == DebugSession::DBG_OK,
               "contextless ROM trace completes on the live path")) return 1;
    if (expect(m.rom_patch_writes > rom_before,
               "contextless ROM trace must use the live BRK machinery")) return 1;
    return 0;
}

static int test_frozen_step_over_rom_jsr_walks_callee_while_parked()
{
    FakeVisibleRomMachine m(true); // freeze mode
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0xFF; // JSR $FF00 (KERNAL callee)
    m.kernal_rom[0xFF00 - 0xE000] = 0xA9; // LDA #$5A   (interpreted)
    m.kernal_rom[0xFF01 - 0xE000] = 0x5A;
    m.kernal_rom[0xFF02 - 0xE000] = 0x8D; // STA $2345  (trampoline step)
    m.kernal_rom[0xFF03 - 0xE000] = 0x45;
    m.kernal_rom[0xFF04 - 0xE000] = 0x23;
    m.kernal_rom[0xFF05 - 0xE000] = 0x60; // RTS        (emulated)

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.a = 0x11;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0xFF };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    int rom_before = m.rom_patch_writes;
    // The STA $2345 runs from the RAM trampoline; model its capture.
    m.arm_capture_context(0x0343, 0xF6, 0x5A, 0x00, 0x00, 0x24);

    DebugContext ctx;
    DebugSession::Result r = m.over(from, pred, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "frozen ROM Step Over walk completes")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2004,
               "walked Step Over lands at the JSR fall-through")) return 1;
    if (expect(ctx.sp == 0xF8, "walked Step Over restores the frame SP")) return 1;
    if (expect(ctx.a == 0x5A, "the callee's register effects are kept")) return 1;
    if (expect(m.rom_patch_writes == rom_before,
               "the walk never plants a BRK in the ROM image")) return 1;
    if (expect(m.frozen, "the machine ends the walk re-frozen")) return 1;
    return 0;
}

static int test_frozen_step_out_of_rom_callee_walks_while_parked()
{
    FakeVisibleRomMachine m(true);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0xFF; // JSR $FF00
    m.kernal_rom[0xFF00 - 0xE000] = 0x60; // RTS

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0xFF };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    DebugContext inside;
    if (expect(m.trace(from, pred, &inside) == DebugSession::DBG_OK,
               "emulated Step Into enters the ROM callee")) return 1;
    if (expect(inside.pc == 0xFF00 && inside.sp == 0xF6,
               "traced frame is in place")) return 1;

    int rom_before = m.rom_patch_writes;
    DebugContext ctx;
    DebugSession::Result r = m.step_out(inside, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "frozen ROM Step Out walk completes")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2004 && ctx.sp == 0xF8,
               "walked Step Out returns to the caller frame")) return 1;
    if (expect(m.rom_patch_writes == rom_before,
               "Step Out walk never plants a BRK in the ROM image")) return 1;
    return 0;
}

// Running UI modes (Telnet/Overlay) must NOT route a visible-ROM Step Out
// through the parked walk: a large frame (e.g. Step Out from inside BASIC's
// EXP evaluation) would exhaust the walk budget and stop at an arbitrary
// mid-frame PC. Breakpoint+Go at the traced return target is the reliable
// primitive when the machine can free-run. (Regression: deep-trace group
// stopped at $BAA1 instead of returning to the RAM caller.)
static int test_running_step_out_of_rom_callee_uses_breakpoint_go()
{
    FakeVisibleRomMachine m(false); // running, not frozen
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    // Telnet/Overlay-host sessions run with the run-window refreeze disabled
    // (run_machine_monitor wires it only for the C64-rendered Freeze UI).
    m.set_run_window_refreeze_enabled(false);
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0xFF; // JSR $FF00
    m.ram[0x2004] = 0xEA; // return site holds a real opcode to patch
    m.kernal_rom[0xFF00 - 0xE000] = 0x60; // RTS
    m.ram[FAKE_STORE_CPU_DDR] = 0x2F;
    m.ram[FAKE_STORE_CPU_PORT] = 0x37;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0xFF };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    DebugContext inside;
    if (expect(m.trace(from, pred, &inside) == DebugSession::DBG_OK,
               "running-mode Step Into enters the ROM callee")) return 1;
    if (expect(inside.pc == 0xFF00 && inside.sp == 0xF6,
               "running-mode traced frame is in place")) return 1;

    int brk_before = m.brk_patch_writes;
    m.arm_capture_context(0x2004, 0xF8, 0, 0, 0, 0x24);
    DebugContext ctx;
    DebugSession::Result r = m.step_out(inside, &ctx);
    if (expect(r == DebugSession::DBG_OK,
               "running-mode ROM Step Out completes via breakpoint+Go")) return 1;
    if (expect(ctx.valid && ctx.pc == 0x2004 && ctx.sp == 0xF8,
               "running-mode Step Out lands at the traced return target")) return 1;
    if (expect(m.brk_patch_writes > brk_before,
               "running-mode Step Out must plant the return breakpoint and "
               "free-run, not walk the frame while parked")) return 1;
    return 0;
}

static int test_parked_freeze_step_into_rom_jsr_stays_frozen()
{
    FakeVisibleRomMachine m(true); // freeze mode
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;
    m.cpu_port = 0x07;
    if (park_session_via_ram_step(m)) return 1;

    m.ram[0x2001] = 0x20;
    m.ram[0x2002] = 0x00;
    m.ram[0x2003] = 0xFF; // JSR $FF00 (KERNAL)
    m.kernal_rom[0xFF00 - 0xE000] = 0x60;
    m.unfreeze_attempts = 0;
    m.unfreeze_calls = 0;
    m.refreeze_calls = 0;

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0x2001;
    from.sp = 0xF8;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    uint8_t jsr[3] = { 0x20, 0x00, 0xFF };
    DebugPredictResult pred;
    debug_predict(0x2001, jsr, false, &pred);
    DebugContext ctx;
    if (expect(m.trace(from, pred, &ctx) == DebugSession::DBG_OK,
               "frozen emulated JSR trace completes")) return 1;
    if (expect(ctx.pc == 0xFF00, "frozen emulated JSR lands at the callee")) return 1;
    if (expect(m.unfreeze_attempts == 0 && m.frozen,
               "an emulated step never unfreezes the machine")) return 1;
    return 0;
}

static int test_debug_classify_step_alerts_are_plain_and_fit()
{
    static const char *const banned[] = {
        "production", "capability", "unsupported", "qualified", "Db!", "enterprise",
        "certified", "uncharacterized", "DbX", "experimental"
    };
    static const DebugStepOp ops[] = {
        DEBUG_OP_OVER, DEBUG_OP_TRACE, DEBUG_OP_OUT, DEBUG_OP_GO, DEBUG_OP_CURSOR
    };
    static const DebugStepSource srcs[] = {
        DEBUG_SRC_RAM, DEBUG_SRC_VISIBLE_ROM, DEBUG_SRC_RAM_UNDER_ROM, DEBUG_SRC_IO
    };
    for (int o = 0; o < (int)(sizeof(ops) / sizeof(ops[0])); o++) {
        for (int s = 0; s < (int)(sizeof(srcs) / sizeof(srcs[0])); s++) {
            for (int freeze = 0; freeze <= 1; freeze++) {
                for (int parked = 0; parked <= 1; parked++) {
                    DebugStepDecision d = debug_classify_step(
                        ops[o], srcs[s], freeze != 0, parked != 0);
                    if (d.alert) {
                        if (alert_shape_ok(d.alert, "classifier alert shape")) return 1;
                        for (int b = 0; b < (int)(sizeof(banned) / sizeof(banned[0])); b++) {
                            if (expect(strstr(d.alert, banned[b]) == NULL,
                                       "Classifier alert must use plain monitor wording")) return 1;
                        }
                    }
                    if (expect(d.reason != NULL && strlen(d.reason) > 0,
                               "Every decision must carry a reason code")) return 1;
                }
            }
        }
    }
    return 0;
}

static int test_debug_classify_step_matrix()
{
    // With a parked context every step is a direct step - the session steps
    // fetch-lagging banks without releasing the CPU into them.
    DebugStepDecision rur_parked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_RAM_UNDER_ROM, false, true);
    if (expect(rur_parked.plan == DEBUG_PLAN_DIRECT && rur_parked.alert == 0,
               "Parked RAM-under-ROM Step Into is a direct step")) return 1;
    DebugStepDecision rom_freeze_parked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_VISIBLE_ROM, true, true);
    if (expect(rom_freeze_parked.plan == DEBUG_PLAN_DIRECT,
               "Parked visible-ROM Freeze Step Into is a direct step")) return 1;

    // Without a parked context there is no authoritative register file:
    // Step Into in a fetch-lagging bank stops with breakpoint guidance.
    DebugStepDecision rur_unparked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_RAM_UNDER_ROM, false, false);
    if (expect(rur_unparked.plan == DEBUG_PLAN_STOP &&
               strcmp(rur_unparked.alert, "Step Into: run to a breakpoint 1st") == 0,
               "Unparked RAM-under-ROM Step Into stops with guidance")) return 1;
    DebugStepDecision rom_freeze_unparked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_VISIBLE_ROM, true, false);
    if (expect(rom_freeze_unparked.plan == DEBUG_PLAN_STOP,
               "Unparked visible-ROM Freeze Step Into stops with guidance")) return 1;

    // Visible ROM without a context is gated in every UI mode: the first ROM
    // fetch after a just-committed step BRK races the FPGA fetch path on a
    // contextless launch regardless of Freeze. Step Over and the
    // breakpoint+Go primitives are never gated.
    DebugStepDecision rom_overlay_unparked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_VISIBLE_ROM, false, false);
    if (expect(rom_overlay_unparked.plan == DEBUG_PLAN_STOP &&
               strcmp(rom_overlay_unparked.alert,
                      "Step Into: run to a breakpoint 1st") == 0,
               "Overlay/Telnet unparked visible-ROM Step Into stops with "
               "guidance")) return 1;
    DebugStepDecision rom_overlay_parked = debug_classify_step(DEBUG_OP_TRACE,
        DEBUG_SRC_VISIBLE_ROM, false, true);
    if (expect(rom_overlay_parked.plan == DEBUG_PLAN_DIRECT,
               "Parked Overlay/Telnet visible-ROM Step Into is direct")) return 1;
    // Contextless non-JSR Step Over is gated only in visible ROM (ROM-image
    // fall-through patch races the post-release fetch); RAM-under-ROM keeps
    // its long-proven banked-RAM-patch clean-release path. Step Over of a
    // JSR keeps its sustained breakpoint+Go completion everywhere.
    DebugStepDecision over_rur_unparked = debug_classify_step(DEBUG_OP_OVER,
        DEBUG_SRC_RAM_UNDER_ROM, false, false, false);
    if (expect(over_rur_unparked.plan == DEBUG_PLAN_DIRECT,
               "Unparked RAM-under-ROM Step Over keeps the banked-RAM "
               "clean-release path")) return 1;
    DebugStepDecision over_rom_freeze = debug_classify_step(DEBUG_OP_OVER,
        DEBUG_SRC_VISIBLE_ROM, true, false, false);
    if (expect(over_rom_freeze.plan == DEBUG_PLAN_STOP,
               "Unparked visible-ROM non-JSR Step Over stops with "
               "guidance")) return 1;
    DebugStepDecision over_rur_jsr = debug_classify_step(DEBUG_OP_OVER,
        DEBUG_SRC_RAM_UNDER_ROM, false, false, true);
    if (expect(over_rur_jsr.plan == DEBUG_PLAN_DIRECT,
               "RAM-under-ROM JSR Step Over stays available")) return 1;
    DebugStepDecision over_rom_jsr = debug_classify_step(DEBUG_OP_OVER,
        DEBUG_SRC_VISIBLE_ROM, true, false, true);
    if (expect(over_rom_jsr.plan == DEBUG_PLAN_DIRECT,
               "Visible-ROM JSR Step Over stays available")) return 1;
    DebugStepDecision over_rom_parked = debug_classify_step(DEBUG_OP_OVER,
        DEBUG_SRC_VISIBLE_ROM, false, true, false);
    if (expect(over_rom_parked.plan == DEBUG_PLAN_DIRECT,
               "Parked visible-ROM non-JSR Step Over is direct")) return 1;
    DebugStepDecision go_rur = debug_classify_step(DEBUG_OP_GO,
        DEBUG_SRC_RAM_UNDER_ROM, true, false);
    if (expect(go_rur.plan == DEBUG_PLAN_DIRECT,
               "Go stays available everywhere")) return 1;
    DebugStepDecision out_rom = debug_classify_step(DEBUG_OP_OUT,
        DEBUG_SRC_VISIBLE_ROM, true, false);
    if (expect(out_rom.plan == DEBUG_PLAN_DIRECT,
               "Step Out stays available everywhere")) return 1;
    return 0;
}

static int test_debug_help_has_no_dbx_hint()
{
    const char *lines[24];
    int n = MonitorDebug::format_help_lines(lines, 24);
    for (int i = 0; i < n; i++) {
        if (expect((int)strlen(lines[i]) <= 38, "Help line must fit 38 columns")) return 1;
        if (expect(strstr(lines[i], "Db!") == NULL, "Help must not use Db!")) return 1;
        if (expect(strstr(lines[i], "DbX") == NULL,
                   "Debug help must not mention DbX")) return 1;
    }
    return 0;
}

// Differential sweep: the parked-step linear interpreter is the production
// stepping path for RAM under ROM and visible ROM, so it must produce exactly
// the architectural state the independent mcm6502 oracle produces for every
// documented non-control-flow instruction. Vectors are generated by
// tools/developer/machine-code-monitor/gen_interpreter_vectors.py.
static int test_parked_interpreter_matches_mcm6502_vectors()
{
    char msg[192];
    for (int vi = 0; vi < interpreter_vector_count; vi++) {
        const InterpreterVector &v = interpreter_vectors[vi];

        FakeVisibleRomMachine m(false);
        m.allow_visible_rom_patching = true;
        m.fetch_lags = true;

        // Park the CPU in the debug spin loop first; context-mutating
        // interpretation is only truthful while parked.
        m.ram[0x2000] = 0xEA;
        m.ram[0x2001] = 0xEA;
        {
            uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
            DebugPredictResult park_pred;
            debug_predict(0x2000, nop, false, &park_pred);
            m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
            DebugContext parked;
            if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                       "vector parking step must complete")) return 1;
        }

        // Sentinel over the instruction trampoline: a linear step in visible
        // ROM must be interpreted while parked, never trampolined here.
        for (int i = 0; i < 4; i++) {
            m.ram[0x0340 + i] = 0x77;
        }
        m.kernal_rom[0x0900] = v.opcode;
        m.kernal_rom[0x0901] = v.operand1;
        m.kernal_rom[0x0902] = v.operand2;
        for (int i = 0; i < v.pre_mem_count; i++) {
            m.ram[v.pre_mem[i].addr] = v.pre_mem[i].value;
        }

        DebugContext from;
        debug_context_reset(&from);
        from.valid = true;
        from.pc = interpreter_vector_pc;
        from.a = v.a;
        from.x = v.x;
        from.y = v.y;
        from.sp = v.sp;
        from.sr = v.sr;
        from.live_cpu_port_valid = true;
        from.live_cpu_port = 0x07;

        uint8_t bytes[3] = { v.opcode, v.operand1, v.operand2 };
        DebugPredictResult pred;
        debug_predict(interpreter_vector_pc, bytes, false, &pred);
        if (v.expect_linear) {
            snprintf(msg, sizeof(msg),
                     "%s: predictor must classify LINEAR len %u",
                     v.label, (unsigned)v.length);
            if (expect(pred.kind == DBG_PREDICT_LINEAR &&
                       pred.length == v.length, msg)) return 1;
        } else {
            snprintf(msg, sizeof(msg),
                     "%s: predictor must classify control flow len %u",
                     v.label, (unsigned)v.length);
            if (expect(pred.kind != DBG_PREDICT_LINEAR &&
                       pred.kind != DBG_PREDICT_UNSAFE &&
                       pred.length == v.length, msg)) return 1;
        }

        DebugContext out;
        DebugSession::Result r = m.trace(from, pred, &out);
        snprintf(msg, sizeof(msg), "%s: parked step must complete", v.label);
        if (expect(r == DebugSession::DBG_OK, msg)) return 1;

        snprintf(msg, sizeof(msg),
                 "%s: got PC %04X A %02X X %02X Y %02X SP %02X SR %02X, "
                 "oracle PC %04X A %02X X %02X Y %02X SP %02X SR %02X",
                 v.label, out.pc, out.a, out.x, out.y, out.sp, out.sr,
                 v.expect_pc, v.expect_a, v.expect_x, v.expect_y,
                 v.expect_sp, v.expect_sr);
        if (expect(out.pc == v.expect_pc && out.a == v.expect_a &&
                   out.x == v.expect_x && out.y == v.expect_y &&
                   out.sp == v.expect_sp && out.sr == v.expect_sr,
                   msg)) return 1;

        for (int i = 0; i < v.post_mem_count; i++) {
            snprintf(msg, sizeof(msg),
                     "%s: memory $%04X must be $%02X, got $%02X",
                     v.label, v.post_mem[i].addr, v.post_mem[i].value,
                     m.ram[v.post_mem[i].addr]);
            if (expect(m.ram[v.post_mem[i].addr] == v.post_mem[i].value,
                       msg)) return 1;
        }
        for (int i = 0; i < 4; i++) {
            snprintf(msg, sizeof(msg),
                     "%s: parked step must be interpreted or emulated, "
                     "not run the trampoline", v.label);
            if (expect(m.ram[0x0340 + i] == 0x77, msg)) return 1;
        }
    }
    return 0;
}

// $00/$01 are the 6510's internal port. A parked-interpretation DMA access
// reads/writes the RAM under the port and cannot produce the banking side
// effect a real access has, so the interpreter must refuse and let the
// RAM trampoline run the instruction on the live CPU. This is the canonical
// RAM-under-ROM debug scenario (stepping code that flips $01).
static int test_parked_interpreter_defers_cpu_port_access_to_trampoline()
{
    struct PortCase {
        uint8_t bytes[3];
        uint8_t length;
        const char *label;
    };
    static const PortCase cases[] = {
        { { 0x85, 0x01, 0x00 }, 2, "STA $01" },
        { { 0x85, 0x00, 0x00 }, 2, "STA $00" },
        { { 0xA5, 0x01, 0x00 }, 2, "LDA $01" },
        { { 0xE6, 0x01, 0x00 }, 2, "INC $01" },
        { { 0xB5, 0x02, 0x00 }, 2, "LDA $02,X with X=$FF wrapping to $01" },
        { { 0xA1, 0xFF, 0x00 }, 2, "LDA ($FF,X) pointer bytes at $00/$01" },
        { { 0x8D, 0x01, 0x00 }, 3, "STA $0001 absolute" },
    };
    char msg[128];
    for (unsigned ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
        const PortCase &pc = cases[ci];
        FakeVisibleRomMachine m(false);
        m.allow_visible_rom_patching = true;
        m.fetch_lags = true;

        m.ram[0x2000] = 0xEA;
        m.ram[0x2001] = 0xEA;
        {
            uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
            DebugPredictResult park_pred;
            debug_predict(0x2000, nop, false, &park_pred);
            m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
            DebugContext parked;
            if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                       "port-case parking step must complete")) return 1;
        }

        for (int i = 0; i < 4; i++) {
            m.ram[0x0340 + i] = 0x77;
        }
        m.kernal_rom[0x0900] = pc.bytes[0];
        m.kernal_rom[0x0901] = pc.bytes[1];
        m.kernal_rom[0x0902] = pc.bytes[2];
        m.ram[0x0000] = 0x2F;
        m.ram[0x0001] = 0x37;
        // The trampoline completion ends in a capture, which refreshes the
        // $00/$01 mirror from the stub-read registers; seed those with the
        // same true-port values so the mirror assertions stay meaningful.
        m.ram[FAKE_STORE_CPU_DDR] = 0x2F;
        m.ram[FAKE_STORE_CPU_PORT] = 0x37;

        DebugContext from;
        debug_context_reset(&from);
        from.valid = true;
        from.pc = 0xE900;
        from.a = 0x5A;
        from.x = (pc.bytes[0] == 0xB5) ? 0xFF : 0x00;
        from.y = 0x00;
        from.sp = 0xF0;
        from.sr = 0x24;
        from.live_cpu_port_valid = true;
        from.live_cpu_port = 0x07;

        DebugPredictResult pred;
        debug_predict(0xE900, pc.bytes, false, &pred);
        // The trampoline completes the step; arm the capture at its BRK.
        m.arm_capture_context((uint16_t)(0x0340 + pc.length), 0xF0,
                              from.a, from.x, from.y, 0x24);
        DebugContext out;
        DebugSession::Result r = m.trace(from, pred, &out);
        snprintf(msg, sizeof(msg),
                 "%s: step must complete via the trampoline", pc.label);
        if (expect(r == DebugSession::DBG_OK, msg)) return 1;
        snprintf(msg, sizeof(msg),
                 "%s: trampoline must receive the instruction "
                 "(interpreter must refuse $00/$01 access)", pc.label);
        if (expect(m.ram[0x0340] == pc.bytes[0], msg)) return 1;
        snprintf(msg, sizeof(msg),
                 "%s: interpreter must not poke RAM under the port", pc.label);
        if (expect(m.ram[0x0001] == 0x37 && m.ram[0x0000] == 0x2F,
                   msg)) return 1;
        snprintf(msg, sizeof(msg), "%s: PC must land at fall-through", pc.label);
        if (expect(out.pc == (uint16_t)(0xE900 + pc.length), msg)) return 1;
    }

    // Control: an ordinary zero-page access one byte above the port is still
    // interpreted while parked and never engages the trampoline.
    {
        FakeVisibleRomMachine m(false);
        m.allow_visible_rom_patching = true;
        m.fetch_lags = true;
        m.ram[0x2000] = 0xEA;
        m.ram[0x2001] = 0xEA;
        {
            uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
            DebugPredictResult park_pred;
            debug_predict(0x2000, nop, false, &park_pred);
            m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
            DebugContext parked;
            if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                       "port-control parking step must complete")) return 1;
        }
        for (int i = 0; i < 4; i++) {
            m.ram[0x0340 + i] = 0x77;
        }
        m.kernal_rom[0x0900] = 0x85; // STA $02
        m.kernal_rom[0x0901] = 0x02;
        DebugContext from;
        debug_context_reset(&from);
        from.valid = true;
        from.pc = 0xE900;
        from.a = 0x5A;
        from.sp = 0xF0;
        from.sr = 0x24;
        from.live_cpu_port_valid = true;
        from.live_cpu_port = 0x07;
        uint8_t bytes[3] = { 0x85, 0x02, 0x00 };
        DebugPredictResult pred;
        debug_predict(0xE900, bytes, false, &pred);
        DebugContext out;
        if (expect(m.trace(from, pred, &out) == DebugSession::DBG_OK,
                   "STA $02 must be interpreted while parked")) return 1;
        if (expect(m.ram[0x0002] == 0x5A,
                   "STA $02 must store through the DMA path")) return 1;
        if (expect(m.ram[0x0340] == 0x77,
                   "STA $02 must not engage the trampoline")) return 1;
    }
    return 0;
}

// The U64's FPGA 6510 never writes CPU stores to $00/$01 through to the RAM
// underneath, so the RAM copy is a DMA-only mirror that goes stale the moment
// a debugged program (or a DMA fixture) changes banking. Every debug capture
// must refresh that mirror from the registers the capture stub read on the
// real CPU, or DMA-side readers (live-bank display, the breakpoint
// "not mapped now" check) keep reporting a phantom bank.
static int test_capture_refreshes_cpu_port_ram_mirror()
{
    FakeVisibleRomMachine m(false);
    m.ram[0x2000] = 0xEA;
    m.ram[0x2001] = 0xEA;
    m.ram[0x0000] = 0x11; // stale DMA-era mirror bytes
    m.ram[0x0001] = 0x22;
    m.ram[FAKE_STORE_CPU_DDR] = 0x2F;  // what the capture stub read on the CPU
    m.ram[FAKE_STORE_CPU_PORT] = 0x36;

    uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
    DebugPredictResult pred;
    debug_predict(0x2000, nop, false, &pred);
    m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
    DebugContext parked;
    if (expect(m.trace_at(0x2000, pred, &parked) == DebugSession::DBG_OK,
               "mirror-refresh capture step must complete")) return 1;
    if (expect(m.ram[0x0000] == 0x2F && m.ram[0x0001] == 0x36,
               "capture must refresh the $00/$01 RAM mirror from the "
               "stub-read registers")) return 1;
    return 0;
}

// The interpreter must never decode an undocumented opcode as if it were the
// documented instruction sharing its aaa/bbb/cc bit pattern (e.g. $9E SHX
// abs,Y aliasing to "STX abs,X"). The UI refuses illegal opcodes before
// predicting and internal walks predict with illegal_enabled=false, so this
// pins the invariant at its last line of defense.
static int test_parked_interpreter_refuses_undocumented_opcodes()
{
    FakeVisibleRomMachine m(false);
    m.allow_visible_rom_patching = true;
    m.fetch_lags = true;

    m.ram[0x2000] = 0xEA;
    m.ram[0x2001] = 0xEA;
    {
        uint8_t nop[3] = { 0xEA, 0xEA, 0xEA };
        DebugPredictResult park_pred;
        debug_predict(0x2000, nop, false, &park_pred);
        m.arm_capture_context(0x2001, 0xCB, 0, 0, 0, 0x24);
        DebugContext parked;
        if (expect(m.trace_at(0x2000, park_pred, &parked) == DebugSession::DBG_OK,
                   "illegal-op parking step must complete")) return 1;
    }

    // $9E with a forced LINEAR prediction: the interpreter must refuse it
    // rather than execute a bogus "STX abs,X"; the write target must stay
    // untouched.
    m.kernal_rom[0x0900] = 0x9E;
    m.kernal_rom[0x0901] = 0x00;
    m.kernal_rom[0x0902] = 0x60;
    m.ram[0x6000 + 0x22] = 0x5A; // would-be bogus STX abs,X target

    DebugContext from;
    debug_context_reset(&from);
    from.valid = true;
    from.pc = 0xE900;
    from.a = 0x11;
    from.x = 0x22;
    from.y = 0x33;
    from.sp = 0xF0;
    from.sr = 0x24;
    from.live_cpu_port_valid = true;
    from.live_cpu_port = 0x07;

    DebugPredictResult pred;
    pred.kind = DBG_PREDICT_LINEAR;
    pred.length = 3;
    pred.fall_through = 0xE903;
    pred.has_target = false;
    pred.branch_target = 0;

    DebugContext out;
    (void)m.trace(from, pred, &out);
    if (expect(m.ram[0x6022] == 0x5A,
               "undocumented $9E must not be interpreted as STX abs,X")) return 1;
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
    RUN(test_debug_pc_disassembly_uses_session_live_bytes_over_view);
    RUN(test_t_traces_and_o_outs_inside_debug);
    RUN(test_debug_o_does_not_step_out);
    RUN(test_o_without_traced_jsr_reports_not_in_subroutine);
    RUN(test_o_without_context_reports_not_in_subroutine);
    RUN(test_t_inside_debug_without_context_traces_from_cursor);
    RUN(test_t_after_goto_ignores_successful_stale_snapshot);
    RUN(test_g_after_goto_ignores_successful_stale_snapshot);
    RUN(test_g_after_cursor_stop_uses_captured_context);
    RUN(test_g_after_goto_with_active_context_uses_cursor);
    RUN(test_g_invalidates_context);
    RUN(test_return_remains_non_executing_navigation);
    RUN(test_ctrl_d_leaves_debug_only);
    RUN(test_ctrl_d_from_edit_clears_edit_for_redebug);
    RUN(test_escape_leaves_edit_before_debug);
    RUN(test_runstop_leaves_edit_before_debug);
    RUN(test_step_from_memory_view_recenters_asm_on_debug_pc);
    RUN(test_ctrl_x_resets_and_keeps_debug_open);
    RUN(test_ctrl_x_local_reset_exits_monitor);
    RUN(test_external_reset_during_debug_wait_exits_without_reopen);
    RUN(test_external_reset_cancel_exits_direct_monitor_without_next_key);
    RUN(test_ctrl_x_reset_reentry_anchors_asm_at_source_boundary);
    RUN(test_ctrl_x_reenters_monitor_without_debug_when_not_debugging);
    RUN(test_breakpoints_survive_ctrl_x_reset_reentry);
    RUN(test_breakpoints_survive_normal_close_reopen);
    RUN(test_monitor_reset_saved_state_clears_breakpoints);
    RUN(test_breakpoint_toggle_via_r);
    RUN(test_breakpoint_mismatch_message_uses_view_target_and_live_cpu);
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
    RUN(test_reset_reentry_view_follows_live_cpu_bank);
    RUN(test_reopen_without_reset_preserves_manual_cpu_view);
    RUN(test_jump_rejects_non_hex_input_and_uppercases);
    RUN(test_edit_and_debug_compose_in_header);
    RUN(test_freeze_step_over_refreezes_and_preserves_screen);
    RUN(test_freeze_step_over_refreezes_when_start_predicate_is_stale);
    RUN(test_freeze_nmi_step_ignores_stale_sentinel);
    RUN(test_freeze_step_over_with_context_refreezes);
    RUN(test_cleanup_to_context_without_entry_context_never_resets);
    RUN(test_freeze_step_into_refreezes);
    RUN(test_freeze_step_out_refreezes);
    RUN(test_step_out_uses_traced_jsr_target_even_when_target_is_above_pc);
    RUN(test_step_out_ignores_nearby_rts_and_patches_after_traced_jsr);
    RUN(test_step_out_refuses_stale_far_stack_frame);
    RUN(test_step_out_unwinds_past_eight_nested_traces);
    RUN(test_visible_rom_simple_linear_interprets_without_breakpoint);
    RUN(test_visible_rom_basic_loop_interprets_index_register_and_flags);
    RUN(test_over_rts_refuses_non_jsr_stack_target);
    RUN(test_traced_rts_uses_recorded_return_target_when_stack_is_unreliable);
    RUN(test_traced_kernal_to_basic_step_out_patches_kernal_return);
    RUN(test_freeze_go_breakpoint_refreezes);
    RUN(test_go_from_current_breakpoint_stops_at_callee_breakpoint);
    RUN(test_go_from_current_visible_rom_breakpoint_uses_patch_aware_bytes);
    RUN(test_step_over_stops_at_callee_breakpoint);
    RUN(test_visible_basic_step_uses_rom_patch_support);
    RUN(test_visible_rom_step_bytes_use_cpu_visible_mapping);
    RUN(test_u64_debug_cpu_port_uses_live_cpu_bank);
    RUN(test_brk_debug_cassette_layout_is_compact_and_top_aligned);
    RUN(test_brk_handler_bytes_preserve_stack_and_port_capture_semantics);
    RUN(test_brk_trampoline_bytes_preserve_rti_restore_semantics);
    RUN(test_hard_brk_stub_bytes_preserve_irq_and_brk_paths);
    RUN(test_brk_capture_records_live_cpu_port);
    RUN(test_parked_step_resume_restores_captured_cpu_port_registers);
    RUN(test_no_breakpoint_continue_restores_captured_cpu_port_registers);
    RUN(test_no_breakpoint_continue_after_step_without_passed_context);
    RUN(test_cursor_visible_rom_step_sets_monitor_cpu_port_before_jump);
    RUN(test_visible_kernal_step_into_uses_rom_patch_support);
    RUN(test_visible_kernal_step_over_jsr_patches_fallthrough_rom);
    RUN(test_visible_rom_sustained_settle_does_not_launch_before_sentinel_clear);
    RUN(test_contextless_visible_rom_relaunch_resettles_fetch_path);
    RUN(test_visible_kernal_step_without_rom_patch_support_refuses_cleanly);
    RUN(test_visible_rom_breakpoint_go_uses_same_capability_gate);
    RUN(test_visible_rom_breakpoint_go_patches_rom_not_underlying_ram);
    RUN(test_mixed_kernal_and_ram_breakpoints_patch_distinct_backing_stores);
    RUN(test_patch_restore_uses_recorded_destination_after_cpu_bank_changes);
    RUN(test_kernal_out_hard_vector_installs_and_restores_on_cleanup);
    RUN(test_kernal_out_hard_vector_restores_on_timeout);
    RUN(test_visible_kernal_hard_vector_installs_and_restores);
    RUN(test_stale_visible_kernal_hard_vector_is_recovered);
    RUN(test_run_to_current_visible_kernal_jsr_enters_callee_before_rearming_cursor_breakpoint);
    RUN(test_run_to_lagging_visible_rom_uses_step_bytes_for_trampoline);
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
    RUN(test_cleanup_resume_preserves_interrupts_when_kernal_banked_out);
    RUN(test_nmi_launch_trampoline_balances_stack);
    RUN(test_kernal_out_cold_nmi_launch_installs_hard_nmi_vector);
    RUN(test_kernal_out_cold_nmi_launch_restores_hard_nmi_vector_on_timeout);
    RUN(test_freeze_cleanup_after_step_allows_later_step);
    RUN(test_freeze_cleanup_preserves_resume_bytes_across_unfreeze_restore);
    RUN(test_freeze_go_without_breakpoint_defers_handoff_until_after_exit);
    RUN(test_direct_overlay_go_without_breakpoint_releases_context_and_exits);
    RUN(test_direct_go_exits_monitor_and_requests_host_release);
    RUN(test_freeze_go_with_breakpoint_still_uses_session_go);
    RUN(test_wait_for_sentinel_drops_execution_keys);
    RUN(test_wait_for_sentinel_aborts_immediately_on_reset_cancel);
    RUN(test_request_reset_cancel_clears_state_and_makes_handler_pokes_during_delay);
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

    // Step chooser and parked-context step emulation.
    RUN(test_debug_marker_shows_dbg);
    RUN(test_x_exits_monitor);
    RUN(test_x_in_edit_mode_is_edit_input);
    RUN(test_ctrl_x_reset_not_shadowed_by_x);
    RUN(test_visible_rom_contextless_linear_step_over_stops);
    RUN(test_visible_rom_contextless_jsr_step_over_runs);
    RUN(test_ram_under_rom_step_into_without_parked_context_stops);
    RUN(test_ram_under_rom_step_into_with_parked_context_runs);
    RUN(test_step_over_into_kernal_runs_without_alert);
    RUN(test_ram_step_over_is_direct);
    RUN(test_parked_step_into_jsr_into_kernal_is_emulated);
    RUN(test_parked_branch_in_ram_under_rom_is_emulated);
    RUN(test_parked_jmp_indirect_emulation_wraps_vector_page);
    RUN(test_parked_rts_without_frame_reports_not_in_subroutine);
    RUN(test_parked_plain_ram_jsr_step_into_runs_live);
    RUN(test_unparked_unsafe_step_into_uses_live_path);
    RUN(test_frozen_step_over_rom_jsr_walks_callee_while_parked);
    RUN(test_frozen_step_out_of_rom_callee_walks_while_parked);
    RUN(test_running_step_out_of_rom_callee_uses_breakpoint_go);
    RUN(test_parked_freeze_step_into_rom_jsr_stays_frozen);
    RUN(test_debug_classify_step_alerts_are_plain_and_fit);
    RUN(test_debug_classify_step_matrix);
    RUN(test_debug_help_has_no_dbx_hint);
    RUN(test_parked_interpreter_matches_mcm6502_vectors);
    RUN(test_parked_interpreter_defers_cpu_port_access_to_trampoline);
    RUN(test_parked_interpreter_refuses_undocumented_opcodes);
    RUN(test_capture_refreshes_cpu_port_ram_mirror);

    puts("machine_monitor_debug_test: OK");
    return 0;
}
