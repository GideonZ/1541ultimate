#include "monitor_debug_brk_session.h"

#include "keyboard.h"
#include "monitor_file_io.h"

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
static const uint16_t SPIN_JMP        = 0x0377;
static const uint16_t SPIN_OPERAND_LO = 0x0378;
static const uint16_t SPIN_OPERAND_HI = 0x0379;
static const uint16_t TRAMPOLINE_ADDR = 0x0380;
static const uint16_t NMI_TRAMPOLINE_ADDR = 0x03A0;
static const uint16_t BRK_VECTOR_LO   = 0x0316;
static const uint16_t BRK_VECTOR_HI   = 0x0317;
static const uint16_t IRQ_VECTOR_LO   = 0x0314;
static const uint16_t NMI_VECTOR_LO   = 0x0318;
static const uint16_t NMI_VECTOR_HI   = 0x0319;
static const uint16_t DEBUG_AREA_END  = 0x03FB;

static const uint8_t HANDLER_BYTES[] = {
    0xBA,
    0xBD, 0x01, 0x01,
    0x8D, 0xF0, 0x03,
    0xBD, 0x02, 0x01,
    0x8D, 0xF1, 0x03,
    0xBD, 0x03, 0x01,
    0x8D, 0xF2, 0x03,
    0xBD, 0x04, 0x01,
    0x8D, 0xF3, 0x03,
    0xBD, 0x05, 0x01,
    0x8D, 0xF4, 0x03,
    0xBD, 0x06, 0x01,
    0x8D, 0xF5, 0x03,
    0x8A,
    0x18,
    0x69, 0x06,
    0x8D, 0xF6, 0x03,
    0xA9, 0x77,
    0x8D, 0x78, 0x03,
    0xA9, 0x03,
    0x8D, 0x79, 0x03,
    0xA9, 0xFF,
    0x8D, 0xF7, 0x03,
    0x4C, 0x77, 0x03
};

static const uint8_t TRAMPOLINE_BYTES[] = {
    0x68,
    0x68,
    0x68,
    0xBA,
    0xAD, 0xF3, 0x03,
    0x9D, 0x01, 0x01,
    0xAD, 0xF4, 0x03,
    0x9D, 0x02, 0x01,
    0xAD, 0xF5, 0x03,
    0x9D, 0x03, 0x01,
    0xAE, 0xF1, 0x03,
    0xAC, 0xF0, 0x03,
    0xAD, 0xF2, 0x03,
    0x40
};

static const int HANDLER_BYTES_LEN = (int)sizeof(HANDLER_BYTES);
static const int TRAMPOLINE_BYTES_LEN = (int)sizeof(TRAMPOLINE_BYTES);

static bool patch_requires_visible_rom(uint16_t addr, uint8_t cpu_port)
{
    cpu_port &= 0x07;
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        return (cpu_port & 0x03) == 0x03;
    }
    if (addr >= 0xD000 && addr <= 0xDFFF) {
        return ((cpu_port & 0x03) != 0x00) && ((cpu_port & 0x04) == 0x00);
    }
    if (addr >= 0xE000) {
        return (cpu_port & 0x02) != 0;
    }
    return false;
}

}

BrkDebugSession :: BrkDebugSession()
    : cancel_keyboard(0), handler_installed(false), cpu_parked_in_spin(false),
      run_window_depth(0), run_window_refreeze_enabled(true),
      run_window_unfroze(false), screen_was_clobbered(false),
      nmi_trampoline_installed(false), has_last_context(false)
{
    memset(patches, 0, sizeof(patches));
    memset(saved_handler_bytes, 0, sizeof(saved_handler_bytes));
    memset(saved_nmi_trampoline_bytes, 0, sizeof(saved_nmi_trampoline_bytes));
    memset(saved_nmi_vector, 0, sizeof(saved_nmi_vector));
    memset(saved_brk_vector, 0, sizeof(saved_brk_vector));
    debug_context_reset(&last_context);
}

BrkDebugSession :: ~BrkDebugSession()
{
    // Do NOT call cleanup() here: by the time the abstract base destructor
    // runs, the concrete subclass (and its hardware hooks) are already gone, so
    // a virtual cleanup() would dispatch to pure-virtual hooks and abort. The
    // monitor always calls cleanup() explicitly before deleting the session,
    // and each concrete leaf calls cleanup() from its own destructor while its
    // vtable is still valid.
}

bool BrkDebugSession :: free_run_no_breakpoint(uint16_t address)
{
    monitor_io::jump_to(address);
    return true;
}

uint8_t BrkDebugSession :: read_patch_byte(uint16_t address, uint8_t cpu_port)
{
    return peek_cpu(address, cpu_port);
}

void BrkDebugSession :: write_patch_byte(uint16_t address, uint8_t byte, uint8_t cpu_port)
{
    poke_cpu(address, byte, cpu_port);
}

void BrkDebugSession :: set_cancel_keyboard(Keyboard *keyboard)
{
    cancel_keyboard = keyboard;
}

void BrkDebugSession :: set_run_window_refreeze_enabled(bool enabled)
{
    run_window_refreeze_enabled = enabled;
}

bool BrkDebugSession :: reserved_patch_address(uint16_t addr) const
{
    if (addr >= IRQ_VECTOR_LO && addr <= NMI_VECTOR_HI) {
        return true;
    }
    return addr >= HANDLER_ADDR && addr <= DEBUG_AREA_END;
}

void BrkDebugSession :: begin_run_window(void)
{
    // Outermost entry unfreezes only if this run window really started from
    // freeze mode. Nested entries (e.g. go() -> step_with_predict -> perform_run)
    // share the single unfreeze/refreeze pair.
    if (run_window_depth++ == 0) {
        screen_was_clobbered = false;
        run_window_unfroze = run_window_refreeze_enabled && machine_is_frozen();
        if (run_window_unfroze) {
            unfreeze_if_accessible();
        }
    }
}

void BrkDebugSession :: end_run_window(void)
{
    if (run_window_depth <= 0) {
        return;
    }
    if (--run_window_depth == 0) {
        // Re-freeze only if this window unfroze a frozen machine. This restores
        // the freezer VIC/charset environment so the monitor renders into the
        // firmware menu screen rather than the live C64 screen RAM. Overlay
        // mode never unfreezes, so this is a no-op there.
        if (run_window_unfroze) {
            refreeze_machine();
            // Signal that the firmware chrome rows (UI title, border lines)
            // were overwritten by the live BASIC screen during the unfreeze and
            // must be redrawn by the caller before the user sees the result.
            screen_was_clobbered = true;
        }
        run_window_unfroze = false;
    }
}

void BrkDebugSession :: save_and_install_handler(void)
{
    if (handler_installed) {
        return;
    }
    bool stopped_it = begin_stopped_session();

    for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
        saved_handler_bytes[i] = peek_visible((uint16_t)(HANDLER_ADDR + i));
    }
    for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
        saved_handler_bytes[HANDLER_BYTES_LEN + i] =
            peek_visible((uint16_t)(TRAMPOLINE_ADDR + i));
    }

    saved_brk_vector[0] = peek_visible(BRK_VECTOR_LO);
    saved_brk_vector[1] = peek_visible(BRK_VECTOR_HI);

    for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
        poke_visible((uint16_t)(HANDLER_ADDR + i), HANDLER_BYTES[i]);
    }
    for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
        poke_visible((uint16_t)(TRAMPOLINE_ADDR + i), TRAMPOLINE_BYTES[i]);
    }
    poke_visible(SENTINEL_ADDR, 0x00);
    poke_visible(BRK_VECTOR_LO, (uint8_t)(HANDLER_ADDR & 0xFF));
    poke_visible(BRK_VECTOR_HI, (uint8_t)(HANDLER_ADDR >> 8));

    end_stopped_session(stopped_it);
    handler_installed = true;
}

void BrkDebugSession :: uninstall_handler(void)
{
    if (!handler_installed) {
        return;
    }
    bool stopped_it = begin_stopped_session();
    poke_visible(BRK_VECTOR_LO, saved_brk_vector[0]);
    poke_visible(BRK_VECTOR_HI, saved_brk_vector[1]);
    for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
        poke_visible((uint16_t)(HANDLER_ADDR + i), saved_handler_bytes[i]);
    }
    for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
        poke_visible((uint16_t)(TRAMPOLINE_ADDR + i),
                     saved_handler_bytes[HANDLER_BYTES_LEN + i]);
    }
    if (nmi_trampoline_installed) {
        poke_visible(NMI_VECTOR_LO, saved_nmi_vector[0]);
        poke_visible(NMI_VECTOR_HI, saved_nmi_vector[1]);
        for (int i = 0; i < (int)sizeof(saved_nmi_trampoline_bytes); i++) {
            poke_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i),
                         saved_nmi_trampoline_bytes[i]);
        }
        nmi_trampoline_installed = false;
    }
    end_stopped_session(stopped_it);
    handler_installed = false;
}

int BrkDebugSession :: find_free_patch(void)
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (!patches[i].used) return i;
    }
    return -1;
}

bool BrkDebugSession :: already_patched(uint16_t addr)
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used && patches[i].address == addr) return true;
    }
    return false;
}

BrkDebugSession::PatchInstallResult BrkDebugSession :: install_brk_at(
    uint16_t addr, uint8_t cpu_port)
{
    if (reserved_patch_address(addr)) {
        return PATCH_INSTALL_FAILED;
    }
    if (already_patched(addr)) {
        return PATCH_INSTALL_OK;
    }
    int slot = find_free_patch();
    if (slot < 0) {
        return PATCH_INSTALL_FAILED;
    }
    if (patch_requires_visible_rom(addr, cpu_port) &&
            !supports_visible_rom_patching()) {
        return PATCH_INSTALL_NOT_SUPPORTED;
    }
    bool stopped_it = begin_stopped_session();
    uint8_t original = read_patch_byte(addr, cpu_port);
    if (original == 0x00) {
        end_stopped_session(stopped_it);
        return PATCH_INSTALL_FAILED;
    }
    write_patch_byte(addr, 0x00, cpu_port);
    if (read_patch_byte(addr, cpu_port) != 0x00) {
        end_stopped_session(stopped_it);
        return PATCH_INSTALL_FAILED;
    }
    end_stopped_session(stopped_it);
    patches[slot].used = true;
    patches[slot].address = addr;
    patches[slot].original = original;
    patches[slot].cpu_port = cpu_port;
    return PATCH_INSTALL_OK;
}

void BrkDebugSession :: restore_patches(void)
{
    bool any = false;
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) { any = true; break; }
    }
    if (!any) return;

    bool stopped_it = begin_stopped_session();
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) {
            write_patch_byte(patches[i].address, patches[i].original,
                             patches[i].cpu_port);
            patches[i].used = false;
        }
    }
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: fill_vectors(DebugContext *ctx, uint8_t cpu_port)
{
    uint8_t lo, hi;
    lo = peek_cpu(IRQ_VECTOR_LO, cpu_port);
    hi = peek_cpu((uint16_t)(IRQ_VECTOR_LO + 1), cpu_port);
    ctx->irq_vec = (uint16_t)(lo | (hi << 8));
    ctx->irq_valid = true;
    lo = peek_cpu(NMI_VECTOR_LO, cpu_port);
    hi = peek_cpu((uint16_t)(NMI_VECTOR_LO + 1), cpu_port);
    ctx->nmi_vec = (uint16_t)(lo | (hi << 8));
    ctx->nmi_valid = true;
}

bool BrkDebugSession :: find_stack_return_target(const DebugContext &from,
                                                 uint8_t cpu_port,
                                                 uint16_t *target)
{
    if (!target) {
        return false;
    }
    for (int off = 1; off < 0xFF; off++) {
        uint16_t sp_lo = (uint16_t)(0x0100 + ((from.sp + off) & 0xFF));
        uint16_t sp_hi = (uint16_t)(0x0100 + ((from.sp + off + 1) & 0xFF));
        uint16_t ret = (uint16_t)(peek_cpu(sp_lo, cpu_port) |
                                  (peek_cpu(sp_hi, cpu_port) << 8));
        uint16_t call = (uint16_t)(ret - 2);
        if (peek_cpu(call, cpu_port) != 0x20) {
            continue;
        }
        uint16_t jsr_target = (uint16_t)(
            peek_cpu((uint16_t)(call + 1), cpu_port) |
            (peek_cpu((uint16_t)(call + 2), cpu_port) << 8));
        if (jsr_target <= from.pc) {
            *target = (uint16_t)(ret + 1);
            return true;
        }
    }
    return false;
}

DebugSession::Result BrkDebugSession :: wait_for_sentinel(int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        if (peek_visible(SENTINEL_ADDR) != 0x00) {
            return DBG_OK;
        }
        if (cancel_keyboard) {
            int key = cancel_keyboard->getch();
            if (key == KEY_ESCAPE || key == KEY_BREAK ||
                key == KEY_CTRL_D || key == KEY_CTRL_O) {
                return DBG_CANCELLED;
            }
            if (key == KEY_CTRL_X) {
                restore_patches();
                uninstall_handler();
                cpu_parked_in_spin = false;
                has_last_context = false;
                debug_context_reset(&last_context);
                reset_machine();
                return DBG_RESET;
            }
            if (key >= 0) {
                cancel_keyboard->push_head(key);
            }
        }
        delay_ms(5);
        waited += 5;
    }
    return DBG_TIMEOUT;
}

void BrkDebugSession :: read_captured_context(DebugContext *ctx, uint8_t cpu_port)
{
    bool stopped_it = begin_stopped_session();
    uint8_t y_val = peek_visible(STORE_Y);
    uint8_t x_val = peek_visible(STORE_X);
    uint8_t a_val = peek_visible(STORE_A);
    uint8_t sr_val = peek_visible(STORE_SR);
    uint8_t pc_lo = peek_visible(STORE_PCLO);
    uint8_t pc_hi = peek_visible(STORE_PCHI);
    uint8_t sp_val = peek_visible(STORE_SP);

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

    poke_visible(SENTINEL_ADDR, 0x00);
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: release_to_run(const DebugContext *from)
{
    bool stopped_it = begin_stopped_session();
    if (from && from->valid) {
        poke_visible(STORE_Y, from->y);
        poke_visible(STORE_X, from->x);
        poke_visible(STORE_A, from->a);
        poke_visible(STORE_SR, from->sr);
        poke_visible(STORE_PCLO, (uint8_t)(from->pc & 0xFF));
        poke_visible(STORE_PCHI, (uint8_t)(from->pc >> 8));
        poke_visible(SPIN_OPERAND_LO, (uint8_t)(TRAMPOLINE_ADDR & 0xFF));
        poke_visible(SPIN_OPERAND_HI, (uint8_t)(TRAMPOLINE_ADDR >> 8));
    }
    poke_visible(SENTINEL_ADDR, 0x00);
    end_stopped_session(stopped_it);
    if (from && from->valid) {
        cpu_parked_in_spin = false;
    }
}

void BrkDebugSession :: resume_from_parked_context(const DebugContext &from)
{
    bool stopped_it = begin_stopped_session();
    const uint8_t bytes[] = {
        0xA2, from.sp,
        0x9A,
        0xA9, from.sr,
        0x48,
        0xA9, (uint8_t)(from.pc & 0xFF),
        0x48,
        0xA9, (uint8_t)(from.pc >> 8),
        0x48,
        0xA0, from.y,
        0xA2, from.x,
        0xA9, from.a,
        0x40
    };
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        poke_visible((uint16_t)(HANDLER_ADDR + i), bytes[i]);
    }
    poke_visible(SPIN_JMP, 0x4C);
    poke_visible(SPIN_OPERAND_LO, (uint8_t)(HANDLER_ADDR & 0xFF));
    poke_visible(SPIN_OPERAND_HI, (uint8_t)(HANDLER_ADDR >> 8));
    // Restore the soft vectors the interrupted program needs once it resumes,
    // inside this same stopped session so a live (overlay-mode) CPU sees them
    // before it leaves the spin loop. The handler/trampoline scratch bytes in
    // the cassette buffer are deliberately left in place: the CPU is about to
    // execute the restore stub that lives there. Clearing handler_installed
    // makes any later uninstall_handler() a no-op so it can never overwrite the
    // stub the CPU may still be running.
    if (handler_installed) {
        poke_visible(BRK_VECTOR_LO, saved_brk_vector[0]);
        poke_visible(BRK_VECTOR_HI, saved_brk_vector[1]);
        handler_installed = false;
    }
    if (nmi_trampoline_installed) {
        poke_visible(NMI_VECTOR_LO, saved_nmi_vector[0]);
        poke_visible(NMI_VECTOR_HI, saved_nmi_vector[1]);
        for (int i = 0; i < (int)sizeof(saved_nmi_trampoline_bytes); i++) {
            poke_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i),
                         saved_nmi_trampoline_bytes[i]);
        }
        nmi_trampoline_installed = false;
    }
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: reset_spin_target(void)
{
    bool stopped_it = begin_stopped_session();
    poke_visible(SPIN_OPERAND_LO, (uint8_t)(SPIN_JMP & 0xFF));
    poke_visible(SPIN_OPERAND_HI, (uint8_t)(SPIN_JMP >> 8));
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: nmi_redirect_to(uint16_t target)
{
    bool stopped_it = begin_stopped_session();
    uint8_t old_nmi_lo = peek_visible(NMI_VECTOR_LO);
    uint8_t old_nmi_hi = peek_visible(NMI_VECTOR_HI);
    const uint8_t bytes[] = {
        0xA9, old_nmi_lo,
        0x8D, (uint8_t)(NMI_VECTOR_LO & 0xFF), (uint8_t)(NMI_VECTOR_LO >> 8),
        0xA9, old_nmi_hi,
        0x8D, (uint8_t)(NMI_VECTOR_HI & 0xFF), (uint8_t)(NMI_VECTOR_HI >> 8),
        0x4C, (uint8_t)(target & 0xFF), (uint8_t)(target >> 8)
    };
    if (!nmi_trampoline_installed) {
        saved_nmi_vector[0] = old_nmi_lo;
        saved_nmi_vector[1] = old_nmi_hi;
        for (unsigned i = 0; i < sizeof(bytes); i++) {
            saved_nmi_trampoline_bytes[i] =
                peek_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i));
        }
        nmi_trampoline_installed = true;
    }
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        poke_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i), bytes[i]);
    }
    poke_visible(NMI_VECTOR_LO, (uint8_t)(NMI_TRAMPOLINE_ADDR & 0xFF));
    poke_visible(NMI_VECTOR_HI, (uint8_t)(NMI_TRAMPOLINE_ADDR >> 8));
    // The NMI request must be raised while the CPU is still stopped, then
    // released during resume so the CPU observes the pending edge. The
    // backend hook handles request+release+clear in one atomic operation.
    pulse_nmi_and_release(stopped_it);
    cpu_parked_in_spin = false;
}

DebugSession::Result BrkDebugSession :: perform_run(const DebugContext *from,
                                                    uint16_t start_pc,
                                                    bool use_start_pc,
                                                    DebugContext *out,
                                                    uint8_t cpu_port)
{
    begin_run_window();
    save_and_install_handler();
    if (cpu_parked_in_spin && from && from->valid) {
        release_to_run(from);
    } else if (from && from->valid) {
        nmi_redirect_to(from->pc);
    } else if (use_start_pc) {
        nmi_redirect_to(start_pc);
    } else {
        release_to_run(0);
    }
    Result waited = wait_for_sentinel(5000);
    if (waited != DBG_OK) {
        restore_patches();
        uninstall_handler();
        cpu_parked_in_spin = false;
        end_run_window();
        return waited;
    }
    read_captured_context(out, cpu_port);
    restore_patches();
    reset_spin_target();
    cpu_parked_in_spin = true;
    has_last_context = true;
    last_context = *out;
    end_run_window();
    return DBG_OK;
}

DebugSession::Result BrkDebugSession :: step_with_predict(
    const DebugContext *from, uint16_t start_pc,
    const DebugPredictResult &pred, bool prefer_jsr_target,
    DebugContext *out, uint8_t cpu_port)
{
    if (pred.kind == DBG_PREDICT_UNSAFE || pred.kind == DBG_PREDICT_BRK) {
        return DBG_REFUSED;
    }
    uint16_t addrs[2];
    int n = 0;
    switch (pred.kind) {
        case DBG_PREDICT_LINEAR:
            addrs[n++] = pred.fall_through;
            break;
        case DBG_PREDICT_JSR:
            addrs[n++] = (prefer_jsr_target && pred.has_target) ?
                pred.branch_target : pred.fall_through;
            break;
        case DBG_PREDICT_JMP_ABS:
            if (pred.has_target) addrs[n++] = pred.branch_target;
            break;
        case DBG_PREDICT_BRANCH:
            addrs[n++] = pred.fall_through;
            if (pred.has_target) addrs[n++] = pred.branch_target;
            break;
        case DBG_PREDICT_JMP_IND: {
            uint16_t op = (uint16_t)(peek_cpu((uint16_t)(start_pc + 1), cpu_port) |
                                     (peek_cpu((uint16_t)(start_pc + 2), cpu_port) << 8));
            uint16_t op_hi = (uint16_t)((op & 0xFF00) | ((op + 1) & 0x00FF));
            uint16_t target = (uint16_t)(peek_cpu(op, cpu_port) |
                                         (peek_cpu(op_hi, cpu_port) << 8));
            addrs[n++] = target;
            break;
        }
        case DBG_PREDICT_RTS: {
            if (!from || !from->valid) return DBG_REFUSED;
            uint16_t sp1 = (uint16_t)(0x0100 + ((from->sp + 1) & 0xFF));
            uint16_t sp2 = (uint16_t)(0x0100 + ((from->sp + 2) & 0xFF));
            uint16_t ret = (uint16_t)(peek_cpu(sp1, cpu_port) |
                                      (peek_cpu(sp2, cpu_port) << 8));
            addrs[n++] = (uint16_t)(ret + 1);
            break;
        }
        case DBG_PREDICT_RTI: {
            if (!from || !from->valid) return DBG_REFUSED;
            uint16_t sp2 = (uint16_t)(0x0100 + ((from->sp + 2) & 0xFF));
            uint16_t sp3 = (uint16_t)(0x0100 + ((from->sp + 3) & 0xFF));
            addrs[n++] = (uint16_t)(peek_cpu(sp2, cpu_port) |
                                    (peek_cpu(sp3, cpu_port) << 8));
            break;
        }
        case DBG_PREDICT_BRK:
        case DBG_PREDICT_UNSAFE:
        default:
            return DBG_REFUSED;
    }
    if (n <= 0) {
        return DBG_REFUSED;
    }
    for (int i = 0; i < n; i++) {
        PatchInstallResult patched = install_brk_at(addrs[i], cpu_port);
        if (patched != PATCH_INSTALL_OK) {
            restore_patches();
            return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
                DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
        }
    }
    return perform_run(from, start_pc, (!from || !from->valid), out, cpu_port);
}

DebugSession::Result BrkDebugSession :: snapshot(DebugContext *ctx)
{
    if (!ctx) return DBG_REFUSED;
    if (has_last_context) {
        *ctx = last_context;
        return DBG_OK;
    }
    return DBG_NOT_SUPPORTED;
}

DebugSession::Result BrkDebugSession :: over(const DebugContext &from,
                                             const DebugPredictResult &pred,
                                             DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    return step_with_predict(&from, from.pc, pred, false, ctx, cpu_port);
}

DebugSession::Result BrkDebugSession :: over_at(uint16_t start_pc,
                                                const DebugPredictResult &pred,
                                                DebugContext *ctx)
{
    if (!backend_ready() || !ctx) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    return step_with_predict(0, start_pc, pred, false, ctx, cpu_port);
}

DebugSession::Result BrkDebugSession :: trace(const DebugContext &from,
                                              const DebugPredictResult &pred,
                                              DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    return step_with_predict(&from, from.pc, pred, true, ctx, cpu_port);
}

DebugSession::Result BrkDebugSession :: trace_at(uint16_t start_pc,
                                                 const DebugPredictResult &pred,
                                                 DebugContext *ctx)
{
    if (!backend_ready() || !ctx) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    return step_with_predict(0, start_pc, pred, true, ctx, cpu_port);
}

DebugSession::Result BrkDebugSession :: step_out(const DebugContext &from,
                                                 DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    uint16_t target;
    if (!find_stack_return_target(from, cpu_port, &target)) {
        return DBG_REFUSED;
    }
    PatchInstallResult patched = install_brk_at(target, cpu_port);
    if (patched != PATCH_INSTALL_OK) {
        return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    return perform_run(&from, from.pc, false, ctx, cpu_port);
}

DebugSession::Result BrkDebugSession :: go(const DebugContext &from,
                                           const MonitorBreakpoints *bps,
                                           uint16_t start_pc)
{
    if (!backend_ready()) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
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
            bytes[i] = peek_cpu((uint16_t)(from.pc + i), cpu_port);
        }
        DebugPredictResult pred;
        debug_predict(from.pc, bytes, false, &pred);
        Result skip = step_with_predict(&from, from.pc, pred, false, &step_ctx, cpu_port);
        if (skip != DBG_OK) {
            return skip;
        }
        resume_from = &step_ctx;
    }

    if (bps) {
        for (int i = 0; i < bps->slot_count(); i++) {
            const MonitorBreakpointSlot *bp = bps->get(i);
            if (!bp || !bp->used || !bp->enabled) continue;
            PatchInstallResult patched = install_brk_at(bp->address, bp->cpu_port);
            if (patched != PATCH_INSTALL_OK) {
                restore_patches();
                return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
                    DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
            }
        }
    }

    bool any_bp = false;
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) { any_bp = true; break; }
    }
    if (!any_bp) {
        uint16_t go_pc = resume_from->valid ? resume_from->pc : start_pc;
        uninstall_handler();
        has_last_context = false;
        debug_context_reset(&last_context);
        free_run_no_breakpoint(go_pc);
        return DBG_OK;
    }

    begin_run_window();
    save_and_install_handler();
    if (resume_from->valid && cpu_parked_in_spin) {
        release_to_run(resume_from);
    } else if (resume_from->valid) {
        poke_visible(SENTINEL_ADDR, 0x00);
        nmi_redirect_to(resume_from->pc);
    } else if (start_pc != 0) {
        poke_visible(SENTINEL_ADDR, 0x00);
        nmi_redirect_to(start_pc);
    } else {
        release_to_run(0);
    }

    Result waited = wait_for_sentinel(5000);
    if (waited != DBG_OK) {
        restore_patches();
        uninstall_handler();
        cpu_parked_in_spin = false;
        end_run_window();
        return waited;
    }
    DebugContext captured;
    read_captured_context(&captured, cpu_port);
    restore_patches();
    reset_spin_target();
    cpu_parked_in_spin = true;
    last_context = captured;
    has_last_context = true;
    end_run_window();
    return DBG_OK;
}

void BrkDebugSession :: cleanup(void)
{
    if (!backend_ready()) return;
    bool resume_pending = cpu_parked_in_spin && has_last_context;
    restore_patches();
    if (resume_pending) {
        // The CPU is parked in the spin loop: live and looping at SPIN_JMP in
        // overlay mode, halted there in freeze mode. Hand it back to the
        // interrupted program by redirecting the spin loop into the
        // register-restore stub (and restoring the soft vectors) in a single
        // stopped session. We must NOT run uninstall_handler() first: it would
        // restore the original bytes under the spin loop and, in overlay mode,
        // drop the live CPU straight into them before the resume stub is staged.
        resume_from_parked_context(last_context);
    } else {
        uninstall_handler();
    }
    cpu_parked_in_spin = false;
}

bool BrkDebugSession :: read_step_bytes(uint16_t address, uint8_t *dst, uint8_t len)
{
    if (!backend_ready() || !dst) {
        return false;
    }
    uint8_t cpu_port = current_cpu_port();
    for (uint8_t i = 0; i < len; i++) {
        dst[i] = read_patch_byte((uint16_t)(address + i), cpu_port);
    }
    return true;
}

void BrkDebugSession :: forget_context(void)
{
    // Drop the cached CPU context so the next snapshot() reports "no context".
    // The next execution command then starts from the monitor cursor rather
    // than the previous session's PC. Patch/handler teardown is cleanup()'s job.
    has_last_context = false;
    debug_context_reset(&last_context);
}
