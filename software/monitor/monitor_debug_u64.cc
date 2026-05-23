// U64 Debug session.
//
// Stepping over the live C64 requires patching RAM with BRK, installing a
// BRK catcher through `$0316/$0317`, releasing ownership, polling for a
// capture sentinel, and restoring every patched byte and vector. The
// trampoline is conceptually analogous to the NMI trampoline used by the
// `G` handoff in `monitor_file_io.cc`.
//
// For the first release this file implements the SAFE PARTS only:
//
//   - `go` honours the existing live-machine handoff via monitor_io::jump_to,
//     so `G` from Debug mode behaves identically to today's monitor `G`.
//   - `snapshot`, `over`, `trace`, and `step_out` refuse cleanly with
//     DBG_NOT_SUPPORTED. The monitor surfaces this refusal as a popup.
//
// The full BRK-based stepping orchestrator is deliberately deferred: it
// requires hardware iteration and the spec explicitly allows refusal over
// fabrication. Cleanup is a no-op until the orchestrator lands.
//
// Adding real Over/Trace/Out should layer on top of this class without
// reshaping the interface: `cleanup` is already where the new trampoline
// would restore patched bytes.

#include "monitor_debug_u64.h"

#if !defined(RUNS_ON_PC)

#include "u64_memory_backend.h"
#include "monitor_file_io.h"

namespace {

class U64DebugSession : public DebugSession
{
    U64MemoryBackend *backend;
public:
    explicit U64DebugSession(U64MemoryBackend *b) : backend(b) { }

    virtual Result snapshot(DebugContext *ctx) {
        // The FPGA does not expose live PC/A/X/Y/SR/SP. Without a BRK
        // catcher in place we have no truthful context to report, so we
        // refuse rather than fabricate. Callers will then surface
        // `DEBUG NOT SUPPORTED` to the user.
        (void)ctx;
        return DBG_NOT_SUPPORTED;
    }
    virtual Result over(const DebugContext &, const DebugPredictResult &, DebugContext *) {
        return DBG_NOT_SUPPORTED;
    }
    virtual Result trace(const DebugContext &, const DebugPredictResult &, DebugContext *) {
        return DBG_NOT_SUPPORTED;
    }
    virtual Result step_out(const DebugContext &, DebugContext *) {
        return DBG_NOT_SUPPORTED;
    }
    virtual Result go(const DebugContext &from, const MonitorBreakpoints *) {
        // Re-use the existing release/jump handoff. The MachineMonitor sets
        // a pending go on receipt of DBG_OK; the actual jump executes after
        // the monitor's main loop deinit drops machine ownership.
        (void)from;
        return DBG_OK;
    }
    virtual void cleanup(void) {
        // No patches are currently installed (snapshot/over/trace/step_out
        // all refuse), so there is nothing to restore. When the BRK-based
        // orchestrator lands this hook will undo every BRK patch / vector
        // edit / trampoline state by reading from a saved-state table.
    }
};

}

DebugSession *create_u64_debug_session(U64MemoryBackend *backend)
{
    return new U64DebugSession(backend);
}

#else

DebugSession *create_u64_debug_session(class U64MemoryBackend *)
{
    return 0;
}

#endif
