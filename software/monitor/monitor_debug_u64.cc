// U64 Debug session.
//
// Implements live stepping on the C64 hardware by patching code with BRK and
// hooking the KERNAL BRK vector at $0316/$0317. The capture handler lives in
// the cassette buffer at $033C; a separate resume trampoline at $0380 puts
// the previously-captured register state back on the stack and RTIs into the
// user's code. Every patched byte, vector, and trampoline byte is tracked so
// `cleanup` restores the machine to a consistent state on success, refusal,
// timeout, debug-off, or monitor close.
//
// Memory layout in the cassette buffer ($033C..$03FB):
//   $033C..$0379: BRK capture stub (ends with self-targeting `JMP $0377`)
//   $0380..$0395: resume trampoline
//   $03F0..$03F7: register storage + sentinel byte
//
// Storage layout:
//   $03F0: Y                $03F1: X         $03F2: A
//   $03F3: SR (with B set)  $03F4: PC-lo     $03F5: PC-hi
//   $03F6: SP (original)    $03F7: sentinel (0xFF when capture done)
//
// The capture stub re-arms its own spin target ($0378/$0379) before storing
// the sentinel. That matters because between BRK trips the firmware
// repoints the spin JMP at the resume trampoline; without the re-arm a
// subsequent BRK would walk straight into the trampoline instead of
// halting in spin while we read state and restore patches.

#include "monitor_debug_u64.h"

#if !defined(RUNS_ON_PC)

#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "monitor_debug_predictor.h"
#include "monitor_breakpoints.h"
#include "monitor_file_io.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

namespace {

static const uint16_t HANDLER_ADDR    = 0x033C;
static const uint16_t STORE_Y         = 0x03F0;
static const uint16_t STORE_X         = 0x03F1;
static const uint16_t STORE_A         = 0x03F2;
static const uint16_t STORE_SR        = 0x03F3;
static const uint16_t STORE_PCLO      = 0x03F4;
static const uint16_t STORE_PCHI      = 0x03F5;
static const uint16_t STORE_SP        = 0x03F6;
static const uint16_t SENTINEL_ADDR   = 0x03F7;
static const uint16_t SPIN_JMP        = 0x0377; // JMP opcode in handler
static const uint16_t SPIN_OPERAND_LO = 0x0378;
static const uint16_t SPIN_OPERAND_HI = 0x0379;
static const uint16_t TRAMPOLINE_ADDR = 0x0380;
static const uint16_t BRK_VECTOR_LO   = 0x0316;
static const uint16_t BRK_VECTOR_HI   = 0x0317;
static const uint16_t IRQ_VECTOR_LO   = 0x0314;
static const uint16_t NMI_VECTOR_LO   = 0x0318;

// Capture handler. KERNAL's `$FF48` BRK preamble has already pushed A, X, Y
// when this stub runs, so we recover them via stack-relative loads. After
// capture the stub re-arms its own spin target so the firmware's
// `release_to_run` redirect (which points the operand at the trampoline)
// does not cause subsequent BRKs to bypass the spin.
static const uint8_t HANDLER_BYTES[] = {
    /* $033C */ 0xBA,                   // TSX
    /* $033D */ 0xBD, 0x01, 0x01,       // LDA $0101,X     ; Y
    /* $0340 */ 0x8D, 0xF0, 0x03,       // STA $03F0
    /* $0343 */ 0xBD, 0x02, 0x01,       // LDA $0102,X     ; X
    /* $0346 */ 0x8D, 0xF1, 0x03,       // STA $03F1
    /* $0349 */ 0xBD, 0x03, 0x01,       // LDA $0103,X     ; A
    /* $034C */ 0x8D, 0xF2, 0x03,       // STA $03F2
    /* $034F */ 0xBD, 0x04, 0x01,       // LDA $0104,X     ; SR
    /* $0352 */ 0x8D, 0xF3, 0x03,       // STA $03F3
    /* $0355 */ 0xBD, 0x05, 0x01,       // LDA $0105,X     ; PC-lo
    /* $0358 */ 0x8D, 0xF4, 0x03,       // STA $03F4
    /* $035B */ 0xBD, 0x06, 0x01,       // LDA $0106,X     ; PC-hi
    /* $035E */ 0x8D, 0xF5, 0x03,       // STA $03F5
    /* $0361 */ 0x8A,                   // TXA              ; SP after KERNAL pushes
    /* $0362 */ 0x18,                   // CLC
    /* $0363 */ 0x69, 0x06,             // ADC #$06         ; original SP = SP + 6
    /* $0365 */ 0x8D, 0xF6, 0x03,       // STA $03F6
    /* $0368 */ 0xA9, 0x77,             // LDA #$77         ; re-arm spin target lo
    /* $036A */ 0x8D, 0x78, 0x03,       // STA $0378
    /* $036D */ 0xA9, 0x03,             // LDA #$03         ; re-arm spin target hi
    /* $036F */ 0x8D, 0x79, 0x03,       // STA $0379
    /* $0372 */ 0xA9, 0xFF,             // LDA #$FF         ; sentinel
    /* $0374 */ 0x8D, 0xF7, 0x03,       // STA $03F7
    /* $0377 */ 0x4C, 0x77, 0x03        // JMP $0377        ; spin
};
static const int HANDLER_BYTES_LEN = (int)sizeof(HANDLER_BYTES);

// Resume trampoline. Restores the saved register file and RTIs back to the
// stored PC. The CPU is redirected here by overwriting the spin JMP target.
static const uint8_t TRAMPOLINE_BYTES[] = {
    /* $0380 */ 0xAD, 0xF5, 0x03,   // LDA STORE_PCHI
    /* $0383 */ 0x48,               // PHA
    /* $0384 */ 0xAD, 0xF4, 0x03,   // LDA STORE_PCLO
    /* $0387 */ 0x48,               // PHA
    /* $0388 */ 0xAD, 0xF3, 0x03,   // LDA STORE_SR
    /* $038B */ 0x48,               // PHA
    /* $038C */ 0xAE, 0xF1, 0x03,   // LDX STORE_X
    /* $038F */ 0xAC, 0xF0, 0x03,   // LDY STORE_Y
    /* $0392 */ 0xAD, 0xF2, 0x03,   // LDA STORE_A
    /* $0395 */ 0x40                // RTI
};
static const int TRAMPOLINE_BYTES_LEN = (int)sizeof(TRAMPOLINE_BYTES);

// Largest reasonable simultaneous patch set: 10 user breakpoints + 2 stepping
// one-shots (over a branch's two destinations) + 1 skip/rearm one-shot.
enum { MAX_PATCHES = 16 };

class U64DebugSession : public DebugSession
{
    U64MemoryBackend *backend;
    U64Machine *machine;

    struct Patch {
        bool used;
        uint16_t address;
        uint8_t original;
        uint8_t cpu_port;
    };
    Patch patches[MAX_PATCHES];

    bool handler_installed;
    uint8_t saved_handler_bytes[HANDLER_BYTES_LEN + TRAMPOLINE_BYTES_LEN];
    uint8_t saved_brk_vector[2];
    bool has_last_context;
    DebugContext last_context;

    void save_and_install_handler(void)
    {
        if (handler_installed) {
            return;
        }
        bool stopped_it = machine->begin_stopped_session();

        // Snapshot the cassette buffer area we are about to overwrite so we
        // can restore it byte-for-byte on cleanup. The save buffer holds the
        // handler region followed by the trampoline region.
        for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
            saved_handler_bytes[i] = machine->peek_visible((uint16_t)(HANDLER_ADDR + i));
        }
        for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
            saved_handler_bytes[HANDLER_BYTES_LEN + i] =
                machine->peek_visible((uint16_t)(TRAMPOLINE_ADDR + i));
        }

        saved_brk_vector[0] = machine->peek_visible(BRK_VECTOR_LO);
        saved_brk_vector[1] = machine->peek_visible(BRK_VECTOR_HI);

        for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
            machine->poke_visible((uint16_t)(HANDLER_ADDR + i), HANDLER_BYTES[i]);
        }
        for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
            machine->poke_visible((uint16_t)(TRAMPOLINE_ADDR + i), TRAMPOLINE_BYTES[i]);
        }
        machine->poke_visible(SENTINEL_ADDR, 0x00);
        machine->poke_visible(BRK_VECTOR_LO, (uint8_t)(HANDLER_ADDR & 0xFF));
        machine->poke_visible(BRK_VECTOR_HI, (uint8_t)(HANDLER_ADDR >> 8));

        machine->end_stopped_session(stopped_it);
        handler_installed = true;
    }

    void uninstall_handler(void)
    {
        if (!handler_installed) {
            return;
        }
        bool stopped_it = machine->begin_stopped_session();
        machine->poke_visible(BRK_VECTOR_LO, saved_brk_vector[0]);
        machine->poke_visible(BRK_VECTOR_HI, saved_brk_vector[1]);
        for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
            machine->poke_visible((uint16_t)(HANDLER_ADDR + i), saved_handler_bytes[i]);
        }
        for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
            machine->poke_visible((uint16_t)(TRAMPOLINE_ADDR + i),
                                  saved_handler_bytes[HANDLER_BYTES_LEN + i]);
        }
        machine->end_stopped_session(stopped_it);
        handler_installed = false;
    }

    int find_free_patch(void)
    {
        for (int i = 0; i < MAX_PATCHES; i++) {
            if (!patches[i].used) return i;
        }
        return -1;
    }

    bool already_patched(uint16_t addr)
    {
        for (int i = 0; i < MAX_PATCHES; i++) {
            if (patches[i].used && patches[i].address == addr) return true;
        }
        return false;
    }

    bool install_brk_at(uint16_t addr, uint8_t cpu_port)
    {
        if (already_patched(addr)) {
            return true;
        }
        int slot = find_free_patch();
        if (slot < 0) {
            return false;
        }
        bool stopped_it = machine->begin_stopped_session();
        uint8_t original = machine->peek_cpu(addr, cpu_port);
        // Refuse to patch over an existing BRK; the user's code may rely on
        // it as a trap point and we would have no original byte to restore.
        if (original == 0x00) {
            machine->end_stopped_session(stopped_it);
            return false;
        }
        machine->poke_cpu(addr, 0x00, cpu_port);
        machine->end_stopped_session(stopped_it);
        patches[slot].used = true;
        patches[slot].address = addr;
        patches[slot].original = original;
        patches[slot].cpu_port = cpu_port;
        return true;
    }

    void restore_patches(void)
    {
        bool any = false;
        for (int i = 0; i < MAX_PATCHES; i++) {
            if (patches[i].used) { any = true; break; }
        }
        if (!any) return;

        bool stopped_it = machine->begin_stopped_session();
        for (int i = 0; i < MAX_PATCHES; i++) {
            if (patches[i].used) {
                machine->poke_cpu(patches[i].address, patches[i].original,
                                  patches[i].cpu_port);
                patches[i].used = false;
            }
        }
        machine->end_stopped_session(stopped_it);
    }

    // Read the IRQ / NMI vectors from RAM honouring the current CPU bank.
    // KERNAL ROM rarely visible at these addresses (they live at $0314+ which
    // is RAM), so this peek is safe.
    void fill_vectors(DebugContext *ctx, uint8_t cpu_port)
    {
        uint8_t lo, hi;
        lo = machine->peek_cpu(IRQ_VECTOR_LO, cpu_port);
        hi = machine->peek_cpu((uint16_t)(IRQ_VECTOR_LO + 1), cpu_port);
        ctx->irq_vec = (uint16_t)(lo | (hi << 8));
        ctx->irq_valid = true;
        lo = machine->peek_cpu(NMI_VECTOR_LO, cpu_port);
        hi = machine->peek_cpu((uint16_t)(NMI_VECTOR_LO + 1), cpu_port);
        ctx->nmi_vec = (uint16_t)(lo | (hi << 8));
        ctx->nmi_valid = true;
    }

    // Poll the sentinel byte while the CPU is running. Returns true on capture
    // within the timeout window. The poll cadence is generous (vTaskDelay 5ms)
    // so we do not heat up the bus stopping the CPU thousands of times per
    // second; capture latency is at most a single frame.
    bool wait_for_sentinel(int timeout_ms)
    {
        int waited = 0;
        while (waited < timeout_ms) {
            if (machine->peek_visible(SENTINEL_ADDR) != 0x00) {
                return true;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
            waited += 5;
        }
        return false;
    }

    // Read the captured register file and assemble a DebugContext. The
    // captured PC is BRK_addr + 2; we adjust by 2 so the returned PC points
    // at the original instruction (whose byte we restored before resuming).
    void read_captured_context(DebugContext *ctx, uint8_t cpu_port)
    {
        bool stopped_it = machine->begin_stopped_session();
        uint8_t y_val = machine->peek_visible(STORE_Y);
        uint8_t x_val = machine->peek_visible(STORE_X);
        uint8_t a_val = machine->peek_visible(STORE_A);
        uint8_t sr_val = machine->peek_visible(STORE_SR);
        uint8_t pc_lo = machine->peek_visible(STORE_PCLO);
        uint8_t pc_hi = machine->peek_visible(STORE_PCHI);
        uint8_t sp_val = machine->peek_visible(STORE_SP);

        debug_context_reset(ctx);
        ctx->valid = true;
        ctx->y = y_val;
        ctx->x = x_val;
        ctx->a = a_val;
        ctx->sr = sr_val;
        ctx->sp = sp_val;
        uint16_t captured_pc = (uint16_t)(pc_lo | (pc_hi << 8));
        ctx->pc = (uint16_t)(captured_pc - 2);
        fill_vectors(ctx, cpu_port);

        // Clear the sentinel so a subsequent run sees a fresh signal.
        machine->poke_visible(SENTINEL_ADDR, 0x00);
        machine->end_stopped_session(stopped_it);
    }

    // Resume execution. If `from.valid` is true, the CPU is currently parked
    // in our spin loop after a previous capture: redirect the JMP operand to
    // the resume trampoline and overwrite the stored register file with the
    // caller's `from` (in particular the PC, which the caller may have
    // adjusted for skip-and-rearm). When `from.valid` is false the CPU is
    // running freely and we just rely on the BRK vector hook to fire.
    void release_to_run(const DebugContext *from)
    {
        bool stopped_it = machine->begin_stopped_session();

        if (from && from->valid) {
            // Overwrite the stored register file with the caller's snapshot.
            machine->poke_visible(STORE_Y, from->y);
            machine->poke_visible(STORE_X, from->x);
            machine->poke_visible(STORE_A, from->a);
            machine->poke_visible(STORE_SR, from->sr);
            machine->poke_visible(STORE_PCLO, (uint8_t)(from->pc & 0xFF));
            machine->poke_visible(STORE_PCHI, (uint8_t)(from->pc >> 8));
            // Point the spin JMP at the trampoline so when execution
            // resumes, the CPU walks into the resume sequence.
            machine->poke_visible(SPIN_OPERAND_LO, (uint8_t)(TRAMPOLINE_ADDR & 0xFF));
            machine->poke_visible(SPIN_OPERAND_HI, (uint8_t)(TRAMPOLINE_ADDR >> 8));
        }
        // Clear sentinel before resume so we observe a fresh capture only.
        machine->poke_visible(SENTINEL_ADDR, 0x00);
        machine->end_stopped_session(stopped_it);

        if (from && from->valid) {
            // The spin JMP target was rewritten while stopped; the CPU now
            // continues in the spin loop and on the next JMP fetch will
            // follow the new target into the trampoline. Restore the
            // operand for the next capture by writing it back AFTER the
            // CPU has moved on. Practically we simply rewrite once on the
            // next install_handler / cleanup cycle, so leave it alone here.
        }
    }

    // After capture, the CPU is parked in the spin loop. Refresh the spin
    // target so a subsequent BRK lands back in the loop instead of skipping
    // straight into the resume trampoline.
    void reset_spin_target(void)
    {
        bool stopped_it = machine->begin_stopped_session();
        machine->poke_visible(SPIN_OPERAND_LO, (uint8_t)(SPIN_JMP & 0xFF));
        machine->poke_visible(SPIN_OPERAND_HI, (uint8_t)(SPIN_JMP >> 8));
        machine->end_stopped_session(stopped_it);
    }

    Result perform_run(const DebugContext *from, DebugContext *out, uint8_t cpu_port)
    {
        save_and_install_handler();
        release_to_run(from);
        // 5 second ceiling: the user can always cancel by closing the
        // monitor. Long-running programs without breakpoints should rely on
        // the user-set BPs hitting; a stall here means no BRK ever fired.
        if (!wait_for_sentinel(5000)) {
            restore_patches();
            reset_spin_target();
            return DBG_TIMEOUT;
        }
        read_captured_context(out, cpu_port);
        restore_patches();
        reset_spin_target();
        has_last_context = true;
        last_context = *out;
        return DBG_OK;
    }

    // For Trace / Over: install BRKs on every reachable next-PC and run.
    // Returns the patches installed so the caller can refuse cleanly if
    // none of the candidate addresses can be patched.
    Result step_with_predict(const DebugContext &from,
                             const DebugPredictResult &pred,
                             bool prefer_jsr_target,
                             DebugContext *out, uint8_t cpu_port)
    {
        if (pred.kind == DBG_PREDICT_UNSAFE || pred.kind == DBG_PREDICT_BRK) {
            return DBG_REFUSED;
        }
        // Resolve the candidate addresses we need to plant BRKs on.
        uint16_t addrs[2];
        int n = 0;
        switch (pred.kind) {
            case DBG_PREDICT_LINEAR:
                addrs[n++] = pred.fall_through;
                break;
            case DBG_PREDICT_JSR:
                if (prefer_jsr_target && pred.has_target) {
                    // Trace: enter the subroutine. Install at the JSR target
                    // so the BRK fires on the first instruction inside.
                    addrs[n++] = pred.branch_target;
                } else {
                    // Over: stop after the JSR returns.
                    addrs[n++] = pred.fall_through;
                }
                break;
            case DBG_PREDICT_JMP_ABS:
                if (pred.has_target) addrs[n++] = pred.branch_target;
                break;
            case DBG_PREDICT_BRANCH:
                addrs[n++] = pred.fall_through;
                if (pred.has_target) addrs[n++] = pred.branch_target;
                break;
            case DBG_PREDICT_JMP_IND: {
                // Resolve via memory: JMP ($XXXX) reads target from the
                // operand word in the current CPU-visible context.
                uint16_t op = (uint16_t)(machine->peek_cpu((uint16_t)(from.pc + 1), cpu_port) |
                                         (machine->peek_cpu((uint16_t)(from.pc + 2), cpu_port) << 8));
                uint16_t target = (uint16_t)(machine->peek_cpu(op, cpu_port) |
                                             (machine->peek_cpu((uint16_t)(op + 1), cpu_port) << 8));
                addrs[n++] = target;
                break;
            }
            case DBG_PREDICT_RTS: {
                // Post-RTS PC = (stack[SP+1] | stack[SP+2] << 8) + 1
                uint16_t sp1 = (uint16_t)(0x0100 + ((from.sp + 1) & 0xFF));
                uint16_t sp2 = (uint16_t)(0x0100 + ((from.sp + 2) & 0xFF));
                uint16_t target = (uint16_t)(machine->peek_cpu(sp1, cpu_port) |
                                             (machine->peek_cpu(sp2, cpu_port) << 8));
                addrs[n++] = (uint16_t)(target + 1);
                break;
            }
            case DBG_PREDICT_RTI: {
                // Post-RTI PC = stack[SP+2] | stack[SP+3] << 8 (RTI does not +1)
                uint16_t sp2 = (uint16_t)(0x0100 + ((from.sp + 2) & 0xFF));
                uint16_t sp3 = (uint16_t)(0x0100 + ((from.sp + 3) & 0xFF));
                uint16_t target = (uint16_t)(machine->peek_cpu(sp2, cpu_port) |
                                             (machine->peek_cpu(sp3, cpu_port) << 8));
                addrs[n++] = target;
                break;
            }
            default:
                return DBG_REFUSED;
        }
        if (n == 0) {
            return DBG_REFUSED;
        }
        for (int i = 0; i < n; i++) {
            if (!install_brk_at(addrs[i], cpu_port)) {
                restore_patches();
                return DBG_PATCH_FAILED;
            }
        }
        return perform_run(&from, out, cpu_port);
    }

public:
    explicit U64DebugSession(U64MemoryBackend *b)
        : backend(b), machine(0), handler_installed(false),
          has_last_context(false)
    {
        memset(patches, 0, sizeof(patches));
        memset(saved_handler_bytes, 0, sizeof(saved_handler_bytes));
        memset(saved_brk_vector, 0, sizeof(saved_brk_vector));
        debug_context_reset(&last_context);
        machine = (U64Machine *)C64::getMachine();
    }

    virtual ~U64DebugSession()
    {
        cleanup();
    }

    virtual Result snapshot(DebugContext *ctx)
    {
        if (!ctx) return DBG_REFUSED;
        if (has_last_context) {
            *ctx = last_context;
            return DBG_OK;
        }
        // No prior capture; the CPU is running freely and we cannot read
        // the live PC/registers without first trapping. The user is expected
        // to set a breakpoint and `G` to acquire a context.
        return DBG_NOT_SUPPORTED;
    }

    virtual Result over(const DebugContext &from, const DebugPredictResult &pred,
                        DebugContext *ctx)
    {
        if (!machine || !ctx) return DBG_REFUSED;
        if (!from.valid) return DBG_REFUSED;
        uint8_t cpu_port = backend ? backend->get_monitor_cpu_port() : (uint8_t)0x07;
        return step_with_predict(from, pred, /*prefer_jsr_target*/false, ctx, cpu_port);
    }

    virtual Result trace(const DebugContext &from, const DebugPredictResult &pred,
                         DebugContext *ctx)
    {
        if (!machine || !ctx) return DBG_REFUSED;
        if (!from.valid) return DBG_REFUSED;
        uint8_t cpu_port = backend ? backend->get_monitor_cpu_port() : (uint8_t)0x07;
        return step_with_predict(from, pred, /*prefer_jsr_target*/true, ctx, cpu_port);
    }

    virtual Result step_out(const DebugContext &from, DebugContext *ctx)
    {
        if (!machine || !ctx) return DBG_REFUSED;
        if (!from.valid) return DBG_REFUSED;
        // Out reads the return address from the stack. If SP looks degenerate
        // (no caller frame to inspect) refuse rather than chase ghosts.
        uint8_t cpu_port = backend ? backend->get_monitor_cpu_port() : (uint8_t)0x07;
        uint16_t sp1 = (uint16_t)(0x0100 + ((from.sp + 1) & 0xFF));
        uint16_t sp2 = (uint16_t)(0x0100 + ((from.sp + 2) & 0xFF));
        uint16_t ret = (uint16_t)(machine->peek_cpu(sp1, cpu_port) |
                                  (machine->peek_cpu(sp2, cpu_port) << 8));
        uint16_t target = (uint16_t)(ret + 1);
        if (!install_brk_at(target, cpu_port)) {
            return DBG_PATCH_FAILED;
        }
        return perform_run(&from, ctx, cpu_port);
    }

    virtual Result go(const DebugContext &from, const MonitorBreakpoints *bps)
    {
        if (!machine) return DBG_REFUSED;
        uint8_t cpu_port = backend ? backend->get_monitor_cpu_port() : (uint8_t)0x07;

        // Skip-and-rearm: if the captured PC sits on an enabled breakpoint
        // we must step past it first, otherwise the BRK we re-install would
        // immediately re-trigger forever.
        bool current_on_bp = false;
        if (from.valid && bps) {
            for (int i = 0; i < bps->slot_count(); i++) {
                const MonitorBreakpointSlot *bp = bps->get(i);
                if (bp && bp->used && bp->enabled && bp->address == from.pc) {
                    current_on_bp = true;
                    break;
                }
            }
        }

        DebugContext step_ctx;
        const DebugContext *resume_from = &from;
        if (current_on_bp) {
            uint8_t bytes[3];
            for (int i = 0; i < 3; i++) {
                bytes[i] = machine->peek_cpu((uint16_t)(from.pc + i), cpu_port);
            }
            DebugPredictResult pred;
            debug_predict(from.pc, bytes, /*illegal_enabled*/false, &pred);
            Result skip = step_with_predict(from, pred, /*prefer_jsr_target*/false,
                                            &step_ctx, cpu_port);
            if (skip != DBG_OK) {
                return skip;
            }
            resume_from = &step_ctx;
        }

        // Install BRKs at every enabled user breakpoint.
        if (bps) {
            for (int i = 0; i < bps->slot_count(); i++) {
                const MonitorBreakpointSlot *bp = bps->get(i);
                if (!bp || !bp->used || !bp->enabled) continue;
                if (!install_brk_at(bp->address, bp->cpu_port)) {
                    restore_patches();
                    return DBG_PATCH_FAILED;
                }
            }
        }

        // If no breakpoints exist this Go simply resumes execution until the
        // user re-enters the monitor; the spec accepts that as "equivalent
        // monitor re-entry condition". Use the existing live-machine handoff.
        bool any_bp = false;
        for (int i = 0; i < MAX_PATCHES; i++) {
            if (patches[i].used) { any_bp = true; break; }
        }
        if (!any_bp) {
            uint16_t go_pc = resume_from->valid ? resume_from->pc : (uint16_t)0;
            uninstall_handler();
            // Defer to the existing release+jump trampoline.
            monitor_io::jump_to(go_pc);
            return DBG_OK;
        }

        DebugContext captured;
        Result r = perform_run(resume_from->valid ? resume_from : 0, &captured, cpu_port);
        if (r == DBG_OK) {
            last_context = captured;
            has_last_context = true;
        }
        return r;
    }

    virtual void cleanup(void)
    {
        if (!machine) return;
        restore_patches();
        uninstall_handler();
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
