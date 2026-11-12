#ifndef MONITOR_DEBUG_SESSION_H
#define MONITOR_DEBUG_SESSION_H

#include "integer.h"
#include "monitor_debug.h"
#include "monitor_breakpoints.h"

// Backend-provided executing layer for Debug mode: owns the BRK trampoline
// /vector hook lifecycle, restoring every patched byte/vector on success,
// failure, cancel, and destruction. Created lazily, destroyed on monitor
// close/debug-off; a thin MemoryBackend orchestrator owning no keyboard/UI state.
class DebugSession
{
public:
    enum Result {
        DBG_OK = 0,             // Stopped cleanly, *ctx populated.
        DBG_NOT_SUPPORTED,      // Backend cannot perform this operation.
        DBG_REFUSED,            // Unsafe target (BRK, unsupported insn, ...).
        DBG_UNSUPPORTED_OPCODE, // Opcode can be decoded but not debug-stepped.
        DBG_NOT_IN_SUBROUTINE,  // Step Out requested without a traced JSR frame.
        DBG_RETURN_NOT_REACHED, // Step Out ran but did not trap at its return target.
        DBG_TIMEOUT,            // Trap did not fire within the wait window.
        DBG_CANCELLED,          // User pressed RUN/STOP / Telnet ESC during wait.
        DBG_RESET,              // User forced a machine reset; context is no longer truthful.
        DBG_PATCH_FAILED,       // Could not safely install or restore a patch.
        DBG_BREAKPOINT_NOT_INSTALLABLE // An armed breakpoint could not be
                                // placed, so the run was not started.
                                // DBG_NOT_SUPPORTED is about the operation's
                                // own target; this one is about an entry in
                                // the breakpoint table.
    };

    virtual ~DebugSession() { }

    // Optional non-owning cancel source for implementations that block while
    // waiting for a trap. Default keeps host tests and unsupported backends
    // independent of UI/input plumbing.
    virtual void set_cancel_keyboard(class Keyboard *) { }

    // True only when the active monitor UI renders through the C64 freezer
    // screen. Overlay and remote monitor sessions must leave this disabled so
    // stepping cannot enter the C64 freeze/refreeze ownership path.
    virtual void set_run_window_refreeze_enabled(bool) { }

    // External machine reset is a hard cancellation boundary for any blocking
    // debug wait. Implementations that poll for traps should return DBG_RESET
    // promptly after this is requested.
    virtual void request_reset_cancel(void) { }

    // Best-effort initial context. May return DBG_NOT_SUPPORTED on backends
    // that cannot snapshot CPU state without first performing a step. The
    // context is also responsible for filling irq_valid / nmi_valid honestly:
    // never fabricate vector contents.
    virtual Result snapshot(DebugContext *ctx) = 0;

    // Step over: install BRK at fall_through, resume, wait, restore. JSR is
    // handled identically (BRK at PC + 3). Backends that support active
    // breakpoints during the run may override the breakpoint-table overload.
    virtual Result over(const DebugContext &from,
                        const struct DebugPredictResult &pred,
                        DebugContext *ctx) = 0;
    virtual Result over(const DebugContext &from,
                        const struct DebugPredictResult &pred,
                        const MonitorBreakpoints *breakpoints,
                        DebugContext *ctx) {
        (void)breakpoints;
        return over(from, pred, ctx);
    }
    virtual Result over_at(uint16_t start_pc,
                           const struct DebugPredictResult &pred,
                           DebugContext *ctx) {
        (void)start_pc; (void)pred; (void)ctx;
        return DBG_NOT_SUPPORTED;
    }
    virtual Result over_at(uint16_t start_pc,
                           const struct DebugPredictResult &pred,
                           const MonitorBreakpoints *breakpoints,
                           DebugContext *ctx) {
        (void)breakpoints;
        return over_at(start_pc, pred, ctx);
    }

    // Step into: BRK at the predicted next PC. For JSR this enters the
    // subroutine. For RTS / RTI the session must read the stack itself.
    virtual Result trace(const DebugContext &from,
                         const struct DebugPredictResult &pred,
                         DebugContext *ctx) = 0;
    virtual Result trace_at(uint16_t start_pc,
                            const struct DebugPredictResult &pred,
                            DebugContext *ctx) {
        (void)start_pc; (void)pred; (void)ctx;
        return DBG_NOT_SUPPORTED;
    }

    // Step out: install BRK at the caller-side resume point inferred from
    // the captured SP. Returns DBG_REFUSED when stack context is unsafe.
    virtual Result step_out(const DebugContext &from,
                            DebugContext *ctx) = 0;
    virtual Result step_out(const DebugContext &from,
                            const MonitorBreakpoints *breakpoints,
                            DebugContext *ctx) {
        (void)breakpoints;
        return step_out(from, ctx);
    }

    // Resume execution honouring active breakpoints; invalidates the caller's
    // context. When `from.valid` is false, `start_pc` gives the address to
    // jump to before hitting breakpoints; zero means don't redirect.
    virtual Result go(const DebugContext &from,
                      const MonitorBreakpoints *breakpoints,
                      uint16_t start_pc) = 0;

    // Run until `target_pc` is hit, using a temporary non-visible breakpoint
    // alongside the visible breakpoint table. If `from.valid` is false, the
    // backend should start from `start_pc`.
    virtual Result run_to(const DebugContext &from,
                          uint16_t target_pc,
                          const MonitorBreakpoints *breakpoints,
                          uint16_t start_pc,
                          DebugContext *ctx)
    {
        (void)from; (void)target_pc; (void)breakpoints; (void)start_pc; (void)ctx;
        return DBG_NOT_SUPPORTED;
    }

    // Restore every patched byte / vector / trampoline state. MUST be safe
    // to call at any time (success, failure, mode change, destructor) and
    // must be idempotent.
    virtual void cleanup(void) = 0;

    // Stop Debugging without continuing the debug target. Backends with a
    // parked CPU may use the supplied pre-debug context as the live resume
    // context instead of resuming the most recent target debug PC.
    virtual void cleanup_to_context(const DebugContext *ctx)
    {
        (void)ctx;
        cleanup();
    }

    // True only when the backend owns a parked debug context handoff-able via
    // cleanup_to_context(); lets the monitor defer a no-breakpoint Freeze-mode
    // G handoff until after UI teardown, leaving overlay/non-parked backends
    // on the existing immediate path.
    virtual bool has_parked_context_handoff(void) const { return false; }

    // True while the live CPU sits parked in the backend's debug spin loop, so
    // every resume path rebuilds the full register file (including SP) from the
    // captured context. Only then may a step be completed by mutating the
    // context instead of running the CPU. Non-parking backends stay false.
    virtual bool has_parked_context(void) const { return false; }

    // True only when the backend can patch bytes in currently visible ROM
    // windows (e.g. U64 volatile BASIC/KERNAL/CHAR images). Backends that
    // cannot must leave this false so the shared BRK engine refuses visible
    // ROM breakpoints before scribbling into the RAM underneath the ROM.
    virtual bool supports_visible_rom_patching(void) const { return false; }

    // Address of the armed breakpoint the last operation could not place, valid
    // only after that operation returned DBG_BREAKPOINT_NOT_INSTALLABLE. It is
    // the one fact a user needs in order to clear it.
    virtual bool blocking_breakpoint(uint16_t *address) const
    {
        (void)address;
        return false;
    }

    // Fetch instruction bytes for step prediction from the debug session's
    // live execution domain, letting ROM-capable backends decode BASIC/KERNAL
    // /CHAR without changing normal memory-view semantics. False means no
    // special source; the caller falls back to regular backend reads.
    virtual bool read_step_bytes(uint16_t address, uint8_t *dst, uint8_t len)
    {
        (void)address; (void)dst; (void)len;
        return false;
    }

    // Forget any cached CPU context so the next snapshot() reports "no context".
    // Called when the user leaves Debug mode so re-entering starts the debugger
    // from the current cursor position instead of the previous session's PC.
    // Does not patch or run the machine; cleanup() still owns patch teardown.
    virtual void forget_context(void) { }

    // True when the last CPU-run window unfroze then refroze a frozen machine,
    // so the firmware chrome (title/border rows) was overwritten by the live
    // BASIC screen and callers must restore it before redrawing. Cleared at
    // the next run window; always false in overlay mode (no freeze windows).
    virtual bool screen_render_target_invalidated(void) const { return false; }

    virtual bool claim_debug_ownership(bool remote) { (void)remote; return true; }
    virtual void refresh_debug_ownership(void) { }
    virtual void release_debug_ownership(void) { }

    virtual bool is_debug_session_active(void) const { return false; }
};

#endif
