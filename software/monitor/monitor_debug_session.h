#ifndef MONITOR_DEBUG_SESSION_H
#define MONITOR_DEBUG_SESSION_H

#include "integer.h"
#include "monitor_debug.h"
#include "monitor_breakpoints.h"

// Backend-provided executing layer for Debug mode. Implementations own the
// BRK trampoline / vector hook lifecycle and are responsible for restoring
// every patched byte and vector on success, failure, cancel, and destruction.
//
// MachineMonitor creates a session lazily on first Debug execution command
// and destroys it on monitor close or debug-off. The session must remain a
// thin orchestrator on top of the existing MemoryBackend; it does not own
// keyboard or UI state.
class DebugSession
{
public:
    enum Result {
        DBG_OK = 0,             // Stopped cleanly, *ctx populated.
        DBG_NOT_SUPPORTED,      // Backend cannot perform this operation.
        DBG_REFUSED,            // Unsafe target (BRK, unsupported insn, ...).
        DBG_TIMEOUT,            // Trap did not fire within the wait window.
        DBG_CANCELLED,          // User pressed RUN/STOP / Telnet ESC during wait.
        DBG_RESET,              // User forced a machine reset; context is no longer truthful.
        DBG_PATCH_FAILED        // Could not safely install or restore a patch.
    };

    virtual ~DebugSession() { }

    // Optional non-owning cancel source for implementations that block while
    // waiting for a trap. Default keeps host tests and unsupported backends
    // independent of UI/input plumbing.
    virtual void set_cancel_keyboard(class Keyboard *) { }

    // Best-effort initial context. May return DBG_NOT_SUPPORTED on backends
    // that cannot snapshot CPU state without first performing a step. The
    // context is also responsible for filling irq_valid / nmi_valid honestly:
    // never fabricate vector contents.
    virtual Result snapshot(DebugContext *ctx) = 0;

    // Step over: install BRK at fall_through, resume, wait, restore. JSR is
    // handled identically (BRK at PC + 3).
    virtual Result over(const DebugContext &from,
                        const struct DebugPredictResult &pred,
                        DebugContext *ctx) = 0;
    virtual Result over_at(uint16_t start_pc,
                           const struct DebugPredictResult &pred,
                           DebugContext *ctx) {
        (void)start_pc; (void)pred; (void)ctx;
        return DBG_NOT_SUPPORTED;
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

    // Resume execution honouring active breakpoints. Invalidates the context
    // on the calling side. When `from.valid` is false (no prior capture),
    // `start_pc` provides the address the CPU should jump to in order to
    // start hitting breakpoints. Backends that can perform the start may
    // honour it; a zero `start_pc` means "do not redirect" (the CPU keeps
    // running wherever it is).
    virtual Result go(const DebugContext &from,
                      const MonitorBreakpoints *breakpoints,
                      uint16_t start_pc) = 0;

    // Restore every patched byte / vector / trampoline state. MUST be safe
    // to call at any time (success, failure, mode change, destructor) and
    // must be idempotent.
    virtual void cleanup(void) = 0;
};

#endif
