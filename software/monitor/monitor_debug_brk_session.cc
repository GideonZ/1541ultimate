#include "monitor_debug_brk_session.h"

#include "keyboard.h"
#include "monitor_file_io.h"
#include "itu.h"

#include <string.h>

namespace {

// Cassette-buffer debug scratch layout. All debug stubs/stores occupy a single
// contiguous, top-aligned block ending at $03FB (the top of the C64 cassette
// buffer $033C-$03FB). The 4 "unused" bytes $03FC-$03FF are deliberately
// left free so programs that rely on them are not disturbed. The
// literal-referenced register stores ($03F0-$03FB) stay put; the code stubs are
// packed contiguously up to them, and the two constant-referenced hard-vector
// scratch bytes that previously spilled into $03FC-$03FE are relocated just
// below the store block. Layout:
//   $0363-$0389  HANDLER       (39 bytes; ends with the spin JMP at $0387)
//   $038A-$03AF  TRAMPOLINE    (38 bytes)
//   $03B0-$03C7  NMI_TRAMPOLINE(24 bytes reserved)
//   $03C8-$03EC  HARD_BRK_STUB (37 bytes; 35 bytes code + 2 bytes padding)
//   $03ED        STORE_HARD_CPU_PORT
//   $03EE-$03EF  HARD_BRK_ORIG_VECTOR (lo/hi)
//   $03F0-$03FB  STORE_Y .. STORE_HARD_CPU_DDR
static const uint16_t HANDLER_ADDR    = 0x0363;
static const uint16_t STORE_Y         = 0x03F0;
static const uint16_t STORE_X         = 0x03F1;
static const uint16_t STORE_A         = 0x03F2;
static const uint16_t STORE_SR        = 0x03F3;
static const uint16_t STORE_PCLO      = 0x03F4;
static const uint16_t STORE_PCHI      = 0x03F5;
static const uint16_t STORE_SP        = 0x03F6;
static const uint16_t SENTINEL_ADDR   = 0x03F7;
static const uint16_t STORE_CPU_DDR   = 0x03F8;
static const uint16_t STORE_CPU_PORT  = 0x03F9;
static const uint16_t STORE_TRAP_MODE = 0x03FA;
static const uint16_t STORE_HARD_CPU_DDR = 0x03FB;
static const uint16_t STORE_HARD_CPU_PORT = 0x03ED;
static const uint16_t SPIN_JMP        = 0x0387;
static const uint16_t SPIN_OPERAND_LO = 0x0388;
static const uint16_t SPIN_OPERAND_HI = 0x0389;
static const uint16_t TRAMPOLINE_ADDR = 0x038A;
static const uint16_t NMI_TRAMPOLINE_ADDR = 0x03B0;
static const uint16_t HARD_BRK_STUB_ADDR = 0x03C8;
static const uint16_t HARD_BRK_ORIG_VECTOR_LO = 0x03EE;
static const uint16_t BRK_VECTOR_LO   = 0x0316;
static const uint16_t BRK_VECTOR_HI   = 0x0317;
static const uint16_t IRQ_VECTOR_LO   = 0x0314;
static const uint16_t NMI_VECTOR_LO   = 0x0318;
static const uint16_t NMI_VECTOR_HI   = 0x0319;
static const uint16_t HARD_VECTOR_LO  = 0xFFFE;
static const uint16_t HARD_VECTOR_HI  = 0xFFFF;
static const uint8_t HARD_VECTOR_RAM_CPU_PORT = 0x05;
static const uint8_t HARD_VECTOR_ROM_CPU_PORT = 0x07;
static const uint8_t HARD_VECTOR_DEFAULT_LO = 0x48;
static const uint8_t HARD_VECTOR_DEFAULT_HI = 0xFF;
static const uint16_t DEBUG_AREA_END  = 0x03FB;

static const uint8_t HANDLER_BYTES[] = {
    0xBA,
    0xA0, 0x00,
    // Copy $0101,X..$0106,X to STORE_Y..STORE_PCHI; X then equals saved SP.
    0xBD, 0x01, 0x01,
    0x99, (uint8_t)(STORE_Y & 0xFF), (uint8_t)(STORE_Y >> 8),
    0xE8,
    0xC8,
    0xC0, 0x06,
    0xD0, 0xF4,
    0x8E, (uint8_t)(STORE_SP & 0xFF), (uint8_t)(STORE_SP >> 8),
    0xA5, 0x00,
    0x8D, 0xF8, 0x03,
    0xA5, 0x01,
    0x8D, 0xF9, 0x03,
    0xA9, (uint8_t)(SPIN_JMP & 0xFF),
    0x8D, (uint8_t)(SPIN_OPERAND_LO & 0xFF),
          (uint8_t)(SPIN_OPERAND_LO >> 8),
    0x8D, (uint8_t)(SENTINEL_ADDR & 0xFF),
          (uint8_t)(SENTINEL_ADDR >> 8),
    0x4C, (uint8_t)(SPIN_JMP & 0xFF), (uint8_t)(SPIN_JMP >> 8)
};

static const uint8_t TRAMPOLINE_BYTES[] = {
    0xAD, (uint8_t)(STORE_CPU_DDR & 0xFF),
          (uint8_t)(STORE_CPU_DDR >> 8),
    0x85, 0x00,
    0xAD, (uint8_t)(STORE_CPU_PORT & 0xFF),
          (uint8_t)(STORE_CPU_PORT >> 8),
    0x85, 0x01,
    0x68,
    0x68,
    0x68,
    0xBA,
    0xA0, 0x00,
    // Copy STORE_SR..STORE_PCHI to $0101,X..$0103,X for the final RTI.
    0xB9, (uint8_t)(STORE_SR & 0xFF), (uint8_t)(STORE_SR >> 8),
    0x9D, 0x01, 0x01,
    0xE8,
    0xC8,
    0xC0, 0x03,
    0xD0, 0xF4,
    0xAE, 0xF1, 0x03,
    0xAC, 0xF0, 0x03,
    0xAD, 0xF2, 0x03,
    0x40
};

static const uint8_t HARD_BRK_STUB_BYTES[] = {
    0x48,
    0x8A,
    0x48,
    0xBA,
    0xBD, 0x03, 0x01,
    0x29, 0x10,
    0xD0, 0x06,
    0x68,
    0xAA,
    0x68,
    0x6C, (uint8_t)(HARD_BRK_ORIG_VECTOR_LO & 0xFF),
          (uint8_t)(HARD_BRK_ORIG_VECTOR_LO >> 8),
    0x98,
    0x48,
    0xA5, 0x00,
    0x8D, (uint8_t)(STORE_HARD_CPU_DDR & 0xFF),
          (uint8_t)(STORE_HARD_CPU_DDR >> 8),
    0xA5, 0x01,
    0x8D, (uint8_t)(STORE_HARD_CPU_PORT & 0xFF),
          (uint8_t)(STORE_HARD_CPU_PORT >> 8),
    0xEE, (uint8_t)(STORE_TRAP_MODE & 0xFF),
          (uint8_t)(STORE_TRAP_MODE >> 8),
    0x4C, (uint8_t)(HANDLER_ADDR & 0xFF),
          (uint8_t)(HANDLER_ADDR >> 8),
    0xEA,
    0xEA
};

static const int HANDLER_BYTES_LEN = (int)sizeof(HANDLER_BYTES);
static const int TRAMPOLINE_BYTES_LEN = (int)sizeof(TRAMPOLINE_BYTES);
static const int NMI_TRAMPOLINE_BYTES_LEN = 24;
static const int HARD_BRK_STUB_BYTES_LEN = (int)sizeof(HARD_BRK_STUB_BYTES);
static const uint16_t DEBUG_OWNER_STALE_REMOTE_MS = 3000;

static bool hard_vector_points_to_stub(uint8_t lo, uint8_t hi)
{
    return lo == (uint8_t)(HARD_BRK_STUB_ADDR & 0xFF) &&
        hi == (uint8_t)(HARD_BRK_STUB_ADDR >> 8);
}

static bool patch_requires_visible_rom(MonitorBackingStore target)
{
    return monitor_backing_store_is_visible_rom(target);
}

static bool patch_verify_now(uint16_t addr, uint8_t live_cpu_port,
                             MonitorBackingStore target)
{
    if (!monitor_backing_store_is_visible_rom(target)) {
        return true;
    }
    return monitor_backing_store_for_cpu_port(addr, live_cpu_port) == target;
}

struct DebugOwnerState {
    BrkDebugSession *owner;
    bool remote;
    uint16_t last_seen_ms;
};

static DebugOwnerState debug_owner = { 0, false, 0 };

}

BrkDebugSession :: BrkDebugSession()
    : cancel_keyboard(0), handler_installed(false), cpu_parked_in_spin(false),
      run_window_depth(0), run_window_refreeze_enabled(true),
      run_window_unfroze(false), screen_was_clobbered(false),
      nmi_trampoline_installed(false), hard_vector_installed(false),
      hard_rom_vector_installed(false), has_last_context(false),
      return_target_count(0)
{
    memset(patches, 0, sizeof(patches));
    memset(return_targets, 0, sizeof(return_targets));
    memset(saved_handler_bytes, 0, sizeof(saved_handler_bytes));
    memset(saved_nmi_trampoline_bytes, 0, sizeof(saved_nmi_trampoline_bytes));
    memset(saved_nmi_vector, 0, sizeof(saved_nmi_vector));
    memset(saved_brk_vector, 0, sizeof(saved_brk_vector));
    memset(saved_hard_vector, 0, sizeof(saved_hard_vector));
    memset(saved_hard_rom_vector, 0, sizeof(saved_hard_rom_vector));
    memset(saved_hard_brk_stub_bytes, 0, sizeof(saved_hard_brk_stub_bytes));
    memset(saved_hard_brk_vector_ptr, 0, sizeof(saved_hard_brk_vector_ptr));
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

bool BrkDebugSession :: claim_debug_ownership(bool remote)
{
    uint16_t now = getMsTimer();
    if (debug_owner.owner == this) {
        debug_owner.remote = remote;
        debug_owner.last_seen_ms = now;
        return true;
    }
    if (debug_owner.owner) {
        bool owner_is_stale =
            (uint16_t)(now - debug_owner.last_seen_ms) >= DEBUG_OWNER_STALE_REMOTE_MS;
        if (!owner_is_stale) {
            return false;
        }
        BrkDebugSession *stale_owner = debug_owner.owner;
        debug_owner.owner = 0;
        debug_owner.remote = false;
        debug_owner.last_seen_ms = now;
        stale_owner->cleanup();
        stale_owner->forget_context();
    }
    debug_owner.owner = this;
    debug_owner.remote = remote;
    debug_owner.last_seen_ms = now;
    return true;
}

void BrkDebugSession :: refresh_debug_ownership(void)
{
    if (debug_owner.owner == this) {
        debug_owner.last_seen_ms = getMsTimer();
    }
}

void BrkDebugSession :: release_debug_ownership(void)
{
    if (debug_owner.owner == this) {
        debug_owner.owner = 0;
        debug_owner.remote = false;
        debug_owner.last_seen_ms = getMsTimer();
    }
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
        if (!hard_vector_installed) {
            save_and_install_hard_vector();
        }
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
    poke_visible(STORE_TRAP_MODE, 0x00);
    poke_visible(STORE_HARD_CPU_DDR, 0x00);
    poke_visible(STORE_HARD_CPU_PORT, 0x00);
    poke_visible(BRK_VECTOR_LO, (uint8_t)(HANDLER_ADDR & 0xFF));
    poke_visible(BRK_VECTOR_HI, (uint8_t)(HANDLER_ADDR >> 8));

    end_stopped_session(stopped_it);
    handler_installed = true;
    save_and_install_hard_vector();
}

void BrkDebugSession :: save_and_install_hard_vector(void)
{
    restore_stale_visible_hard_vector();
    if (hard_vector_installed) {
        return;
    }
    bool stopped_it = begin_stopped_session();

    saved_hard_vector[0] = peek_cpu(HARD_VECTOR_LO, HARD_VECTOR_RAM_CPU_PORT);
    saved_hard_vector[1] = peek_cpu(HARD_VECTOR_HI, HARD_VECTOR_RAM_CPU_PORT);
    for (int i = 0; i < HARD_BRK_STUB_BYTES_LEN; i++) {
        saved_hard_brk_stub_bytes[i] = peek_visible((uint16_t)(HARD_BRK_STUB_ADDR + i));
    }
    saved_hard_brk_vector_ptr[0] = peek_visible(HARD_BRK_ORIG_VECTOR_LO);
    saved_hard_brk_vector_ptr[1] = peek_visible((uint16_t)(HARD_BRK_ORIG_VECTOR_LO + 1));

    for (int i = 0; i < HARD_BRK_STUB_BYTES_LEN; i++) {
        poke_visible((uint16_t)(HARD_BRK_STUB_ADDR + i), HARD_BRK_STUB_BYTES[i]);
    }
    poke_visible(HARD_BRK_ORIG_VECTOR_LO, saved_hard_vector[0]);
    poke_visible((uint16_t)(HARD_BRK_ORIG_VECTOR_LO + 1), saved_hard_vector[1]);
    poke_cpu(HARD_VECTOR_LO, (uint8_t)(HARD_BRK_STUB_ADDR & 0xFF),
             HARD_VECTOR_RAM_CPU_PORT);
    poke_cpu(HARD_VECTOR_HI, (uint8_t)(HARD_BRK_STUB_ADDR >> 8),
             HARD_VECTOR_RAM_CPU_PORT);

    end_stopped_session(stopped_it);
    hard_vector_installed = true;
}

void BrkDebugSession :: restore_stale_visible_hard_vector(void)
{
    if (hard_rom_vector_installed || !supports_visible_rom_patching()) {
        return;
    }
    bool stopped_it = begin_stopped_session();
    uint8_t lo = peek_cpu(HARD_VECTOR_LO, HARD_VECTOR_ROM_CPU_PORT);
    uint8_t hi = peek_cpu(HARD_VECTOR_HI, HARD_VECTOR_ROM_CPU_PORT);
    if (hard_vector_points_to_stub(lo, hi)) {
        poke_cpu(HARD_VECTOR_LO, HARD_VECTOR_DEFAULT_LO,
                 HARD_VECTOR_ROM_CPU_PORT);
        poke_cpu(HARD_VECTOR_HI, HARD_VECTOR_DEFAULT_HI,
                 HARD_VECTOR_ROM_CPU_PORT);
    }
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: save_and_install_visible_hard_vector(void)
{
    if (hard_rom_vector_installed || !supports_visible_rom_patching()) {
        return;
    }
    if (!hard_vector_installed) {
        save_and_install_hard_vector();
    }
    bool stopped_it = begin_stopped_session();
    saved_hard_rom_vector[0] = peek_cpu(HARD_VECTOR_LO,
                                        HARD_VECTOR_ROM_CPU_PORT);
    saved_hard_rom_vector[1] = peek_cpu(HARD_VECTOR_HI,
                                        HARD_VECTOR_ROM_CPU_PORT);
    if (hard_vector_points_to_stub(saved_hard_rom_vector[0],
                                   saved_hard_rom_vector[1])) {
        saved_hard_rom_vector[0] = HARD_VECTOR_DEFAULT_LO;
        saved_hard_rom_vector[1] = HARD_VECTOR_DEFAULT_HI;
    }
    poke_cpu(HARD_VECTOR_LO, (uint8_t)(HARD_BRK_STUB_ADDR & 0xFF),
             HARD_VECTOR_ROM_CPU_PORT);
    poke_cpu(HARD_VECTOR_HI, (uint8_t)(HARD_BRK_STUB_ADDR >> 8),
             HARD_VECTOR_ROM_CPU_PORT);
    end_stopped_session(stopped_it);
    hard_rom_vector_installed = true;
}

void BrkDebugSession :: uninstall_hard_vector(void)
{
    if (!hard_vector_installed) {
        return;
    }
    poke_cpu(HARD_VECTOR_LO, saved_hard_vector[0], HARD_VECTOR_RAM_CPU_PORT);
    poke_cpu(HARD_VECTOR_HI, saved_hard_vector[1], HARD_VECTOR_RAM_CPU_PORT);
    if (hard_rom_vector_installed) {
        poke_cpu(HARD_VECTOR_LO, saved_hard_rom_vector[0],
                 HARD_VECTOR_ROM_CPU_PORT);
        poke_cpu(HARD_VECTOR_HI, saved_hard_rom_vector[1],
                 HARD_VECTOR_ROM_CPU_PORT);
        hard_rom_vector_installed = false;
    }
    for (int i = 0; i < HARD_BRK_STUB_BYTES_LEN; i++) {
        poke_visible((uint16_t)(HARD_BRK_STUB_ADDR + i),
                     saved_hard_brk_stub_bytes[i]);
    }
    poke_visible(HARD_BRK_ORIG_VECTOR_LO, saved_hard_brk_vector_ptr[0]);
    poke_visible((uint16_t)(HARD_BRK_ORIG_VECTOR_LO + 1),
                 saved_hard_brk_vector_ptr[1]);
    hard_vector_installed = false;
}

void BrkDebugSession :: uninstall_handler(void)
{
    if (!handler_installed && !nmi_trampoline_installed && !hard_vector_installed) {
        return;
    }
    bool stopped_it = begin_stopped_session();
    uninstall_hard_vector();
    if (handler_installed) {
        poke_visible(BRK_VECTOR_LO, saved_brk_vector[0]);
        poke_visible(BRK_VECTOR_HI, saved_brk_vector[1]);
        for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
            poke_visible((uint16_t)(HANDLER_ADDR + i), saved_handler_bytes[i]);
        }
        for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
            poke_visible((uint16_t)(TRAMPOLINE_ADDR + i),
                         saved_handler_bytes[HANDLER_BYTES_LEN + i]);
        }
    }
    if (nmi_trampoline_installed) {
        poke_visible(NMI_VECTOR_LO, saved_nmi_vector[0]);
        poke_visible(NMI_VECTOR_HI, saved_nmi_vector[1]);
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
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

bool BrkDebugSession :: already_patched(uint16_t addr, MonitorBackingStore target)
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used && patches[i].address == addr &&
                patches[i].target == target) {
            return true;
        }
    }
    return false;
}

BrkDebugSession::PatchInstallResult BrkDebugSession :: install_brk_at(
    uint16_t addr, uint8_t cpu_port)
{
    return install_brk_at(addr, cpu_port,
                          monitor_backing_store_for_cpu_port(addr, cpu_port));
}

BrkDebugSession::PatchInstallResult BrkDebugSession :: install_brk_at(
    uint16_t addr, uint8_t cpu_port, MonitorBackingStore target)
{
    cpu_port &= 0x07;
    if (reserved_patch_address(addr)) {
        return PATCH_INSTALL_FAILED;
    }
    if (already_patched(addr, target)) {
        return PATCH_INSTALL_OK;
    }
    int slot = find_free_patch();
    if (slot < 0) {
        return PATCH_INSTALL_FAILED;
    }
    bool visible_rom_patch = patch_requires_visible_rom(target);
    if (visible_rom_patch && !supports_visible_rom_patching()) {
        return PATCH_INSTALL_NOT_SUPPORTED;
    }
    if (visible_rom_patch) {
        save_and_install_visible_hard_vector();
    }
    bool stopped_it = begin_stopped_session();
    uint8_t original = read_patch_byte(addr, cpu_port);
    // A target may already contain BRK. Treat that as a valid trap location
    // instead of failing the step command.
    if (original != 0x00) {
        write_patch_byte(addr, 0x00, cpu_port);
    }
    bool verify = patch_verify_now(addr, current_cpu_port(), target);
    if (verify && read_patch_byte(addr, cpu_port) != 0x00) {
        end_stopped_session(stopped_it);
        return PATCH_INSTALL_FAILED;
    }
    end_stopped_session(stopped_it);
    patches[slot].used = true;
    patches[slot].address = addr;
    patches[slot].original = original;
    patches[slot].cpu_port = cpu_port;
    patches[slot].target = target;
    return PATCH_INSTALL_OK;
}

BrkDebugSession::PatchInstallResult BrkDebugSession :: install_breakpoints(
    const MonitorBreakpoints *bps, uint16_t skip_address,
    MonitorBackingStore skip_target, bool skip_address_valid)
{
    if (!bps) {
        return PATCH_INSTALL_OK;
    }
    for (int i = 0; i < bps->slot_count(); i++) {
        const MonitorBreakpointSlot *bp = bps->get(i);
        if (!bp || !bp->used || !bp->enabled) {
            continue;
        }
        if (skip_address_valid && bp->address == skip_address &&
                bp->target == skip_target) {
            continue;
        }
        PatchInstallResult patched = install_brk_at(bp->address, bp->view_cpu_port,
                                                    bp->target);
        if (patched != PATCH_INSTALL_OK) {
            return patched;
        }
    }
    return PATCH_INSTALL_OK;
}

bool BrkDebugSession :: context_at_breakpoint(
    const DebugContext &ctx, const MonitorBreakpoints *bps,
    uint16_t skip_address, MonitorBackingStore skip_target,
    bool skip_address_valid) const
{
    if (!ctx.valid || !bps) {
        return false;
    }
    for (int i = 0; i < bps->slot_count(); i++) {
        const MonitorBreakpointSlot *bp = bps->get(i);
        if (!bp || !bp->used || !bp->enabled) {
            continue;
        }
        if (skip_address_valid && bp->address == skip_address &&
                bp->target == skip_target) {
            continue;
        }
        if (bp->address == ctx.pc &&
                bp->target == monitor_backing_store_for_cpu_port(
                    ctx.pc, execution_cpu_port(&ctx))) {
            return true;
        }
    }
    return false;
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

void BrkDebugSession :: clear_return_targets(void)
{
    return_target_count = 0;
}

void BrkDebugSession :: push_return_target(uint16_t target)
{
    if (return_target_count >= MAX_RETURN_TARGETS) {
        for (int i = 1; i < MAX_RETURN_TARGETS; i++) {
            return_targets[i - 1] = return_targets[i];
        }
        return_target_count = MAX_RETURN_TARGETS - 1;
    }
    return_targets[return_target_count++] = target;
}

bool BrkDebugSession :: peek_return_target(uint16_t *target) const
{
    if (!target || return_target_count == 0) {
        return false;
    }
    *target = return_targets[return_target_count - 1];
    return true;
}

void BrkDebugSession :: pop_return_target(uint16_t target)
{
    if (return_target_count > 0 &&
            return_targets[return_target_count - 1] == target) {
        return_target_count--;
    }
}

uint8_t BrkDebugSession :: execution_cpu_port(const DebugContext *ctx) const
{
    if (ctx && ctx->valid && ctx->live_cpu_port_valid) {
        return (uint8_t)(ctx->live_cpu_port & 0x07);
    }
    return current_cpu_port();
}

void BrkDebugSession :: drop_queued_execution_keys(void)
{
    if (!cancel_keyboard) {
        return;
    }
    while (1) {
        int key = cancel_keyboard->getch();
        if (key < 0) {
            return;
        }
        if (key == 'd' || key == 'D' ||
            key == 't' || key == 'T' ||
            key == 'o' || key == 'O' ||
            key == 'g' || key == 'G' ||
            key == 'k' || key == 'K') {
            continue;
        }
        cancel_keyboard->push_head(key);
        return;
    }
}

DebugSession::Result BrkDebugSession :: wait_for_sentinel(int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        refresh_debug_ownership();
        if (peek_visible(SENTINEL_ADDR) != 0x00) {
            drop_queued_execution_keys();
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
    uint8_t cpu_ddr_register = peek_visible(STORE_CPU_DDR);
    uint8_t cpu_port_register = peek_visible(STORE_CPU_PORT);
    uint8_t cpu_ddr = cpu_ddr_register & 0x07;
    uint8_t cpu_port_latch = cpu_port_register & 0x07;
    uint8_t trap_mode = peek_visible(STORE_TRAP_MODE);
    uint8_t hard_cpu_ddr_register = peek_visible(STORE_HARD_CPU_DDR);
    uint8_t hard_cpu_port_register = peek_visible(STORE_HARD_CPU_PORT);
    uint8_t hard_cpu_ddr = hard_cpu_ddr_register & 0x07;
    uint8_t hard_cpu_port_latch = hard_cpu_port_register & 0x07;
    uint8_t live_cpu_port;
    bool hard_trap = trap_mode != 0x00;
    if (hard_trap && hard_cpu_ddr != 0x00) {
        live_cpu_port = (uint8_t)(((hard_cpu_port_latch & hard_cpu_ddr) |
            ((uint8_t)(~hard_cpu_ddr) & 0x07)) & 0x07);
    } else if (hard_trap && cpu_ddr == 0x00) {
        live_cpu_port = (uint8_t)(cpu_port & 0x07);
    } else {
        live_cpu_port =
            (uint8_t)(((cpu_port_latch & cpu_ddr) | ((uint8_t)(~cpu_ddr) & 0x07)) & 0x07);
    }

    debug_context_reset(ctx);
    ctx->valid = true;
    ctx->y = y_val;
    ctx->x = x_val;
    ctx->a = a_val;
    ctx->sr = sr_val;
    ctx->sp = sp_val;
    ctx->live_cpu_port_valid = true;
    ctx->live_cpu_port = live_cpu_port;
    ctx->cpu_port_registers_valid = true;
    if (hard_trap && hard_cpu_ddr != 0x00) {
        ctx->cpu_ddr = hard_cpu_ddr_register;
        ctx->cpu_port_latch = hard_cpu_port_register;
    } else if (hard_trap && cpu_ddr == 0x00) {
        ctx->cpu_ddr = (uint8_t)((hard_cpu_ddr_register & 0xF8) | 0x07);
        ctx->cpu_port_latch =
            (uint8_t)((hard_cpu_port_register & 0xF8) | (live_cpu_port & 0x07));
    } else {
        ctx->cpu_ddr = cpu_ddr_register;
        ctx->cpu_port_latch = cpu_port_register;
    }
    uint16_t captured_pc = (uint16_t)(pc_lo | (pc_hi << 8));
    ctx->pc = (uint16_t)(captured_pc - 2);
    fill_vectors(ctx, live_cpu_port);
    note_captured_cpu_port(live_cpu_port);

    poke_visible(SENTINEL_ADDR, 0x00);
    poke_visible(STORE_TRAP_MODE, 0x00);
    poke_visible(STORE_HARD_CPU_DDR, 0x00);
    poke_visible(STORE_HARD_CPU_PORT, 0x00);
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: restore_cpu_port_registers(const DebugContext &from)
{
    if (!from.valid || !from.cpu_port_registers_valid) {
        return;
    }
    poke_visible(STORE_CPU_DDR, from.cpu_ddr);
    poke_visible(STORE_CPU_PORT, from.cpu_port_latch);
}

void BrkDebugSession :: release_to_run(const DebugContext *from)
{
    bool stopped_it = begin_stopped_session();
    if (from && from->valid) {
        restore_cpu_port_registers(*from);
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
    poke_visible(STORE_TRAP_MODE, 0x00);
    poke_visible(STORE_HARD_CPU_DDR, 0x00);
    poke_visible(STORE_HARD_CPU_PORT, 0x00);
    end_stopped_session(stopped_it);
    if (from && from->valid) {
        cpu_parked_in_spin = false;
    }
}

void BrkDebugSession :: resume_from_parked_context(const DebugContext &from)
{
    bool stopped_it = begin_stopped_session();
    restore_cpu_port_registers(from);
    uint8_t resume_ddr = from.cpu_port_registers_valid ? from.cpu_ddr : 0x37;
    uint8_t resume_port = from.cpu_port_registers_valid ?
        from.cpu_port_latch : (uint8_t)(from.live_cpu_port & 0x07);
    const uint8_t bytes[] = {
        0xA9, resume_ddr,
        0x85, 0x00,
        0xA9, resume_port,
        0x85, 0x01,
        0xA2, from.sp,
        0x9A,
        0xA9, (uint8_t)(from.pc >> 8),
        0x48,
        0xA9, (uint8_t)(from.pc & 0xFF),
        0x48,
        0xA9, from.sr,
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
    uninstall_hard_vector();
    if (nmi_trampoline_installed) {
        poke_visible(NMI_VECTOR_LO, saved_nmi_vector[0]);
        poke_visible(NMI_VECTOR_HI, saved_nmi_vector[1]);
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
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

void BrkDebugSession :: nmi_redirect_to(uint16_t target, uint8_t cpu_port,
                                        bool force_cpu_port)
{
    bool stopped_it = begin_stopped_session();
    uint8_t old_nmi_lo = peek_visible(NMI_VECTOR_LO);
    uint8_t old_nmi_hi = peek_visible(NMI_VECTOR_HI);
    uint8_t bytes[24];
    int len = 0;

    // Taking the NMI pushes a 3-byte frame (PCH, PCL, SR) onto the stack.
    // We resume with a JMP (not an RTI), so without discarding that frame
    // the program's stack pointer would be left 3 bytes low for the rest of
    // the run - every later push (e.g. a JSR return address) would then land
    // 3 bytes below where an undebugged run puts it. Stepping stays
    // self-consistent (a matching RTS still works) but the stack contents no
    // longer match real execution. Pull the 3 frame bytes back off first so
    // SP is exactly what it was before the NMI, then redirect to the target.
    bytes[len++] = 0x68;
    bytes[len++] = 0x68;
    bytes[len++] = 0x68;
    bytes[len++] = 0xA9;
    bytes[len++] = old_nmi_lo;
    bytes[len++] = 0x8D;
    bytes[len++] = (uint8_t)(NMI_VECTOR_LO & 0xFF);
    bytes[len++] = (uint8_t)(NMI_VECTOR_LO >> 8);
    bytes[len++] = 0xA9;
    bytes[len++] = old_nmi_hi;
    bytes[len++] = 0x8D;
    bytes[len++] = (uint8_t)(NMI_VECTOR_HI & 0xFF);
    bytes[len++] = (uint8_t)(NMI_VECTOR_HI >> 8);
    if (force_cpu_port) {
        bytes[len++] = 0xA9;
        bytes[len++] = 0x37;
        bytes[len++] = 0x85;
        bytes[len++] = 0x00;
        bytes[len++] = 0xA9;
        bytes[len++] = (uint8_t)(cpu_port & 0x07);
        bytes[len++] = 0x85;
        bytes[len++] = 0x01;
    }
    bytes[len++] = 0x4C;
    bytes[len++] = (uint8_t)(target & 0xFF);
    bytes[len++] = (uint8_t)(target >> 8);

    if (!nmi_trampoline_installed) {
        saved_nmi_vector[0] = old_nmi_lo;
        saved_nmi_vector[1] = old_nmi_hi;
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
            saved_nmi_trampoline_bytes[i] =
                peek_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i));
        }
        nmi_trampoline_installed = true;
    }
    for (int i = 0; i < len; i++) {
        poke_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i), bytes[i]);
    }
    poke_visible(NMI_VECTOR_LO, (uint8_t)(NMI_TRAMPOLINE_ADDR & 0xFF));
    poke_visible(NMI_VECTOR_HI, (uint8_t)(NMI_TRAMPOLINE_ADDR >> 8));
    // Clear the sentinel as the last act before releasing the CPU, while it is
    // still stopped and therefore cannot set it. In freeze mode the run window
    // unfreezes the whole machine, so between that unfreeze and this stopped
    // session the live CPU free-runs from its frozen PC and - in a hot loop
    // such as the BASIC FAC multiply at $B9A6 - can reach the BRK we already
    // installed at the step target, fire the handler, and leave the sentinel
    // set. Launching the controlled NMI run without clearing it first lets
    // wait_for_sentinel() observe that stale $FF and return immediately with a
    // context the engine never controlled, desyncing cpu_parked_in_spin from
    // the real CPU state and producing the sporadic runaway-stepping loop.
    // Clearing here guarantees the next sentinel comes only from this run.
    poke_visible(SENTINEL_ADDR, 0x00);
    poke_visible(STORE_TRAP_MODE, 0x00);
    poke_visible(STORE_HARD_CPU_DDR, 0x00);
    poke_visible(STORE_HARD_CPU_PORT, 0x00);
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
    refresh_debug_ownership();
    begin_run_window();
    save_and_install_handler();
    if (cpu_parked_in_spin && from && from->valid) {
        release_to_run(from);
    } else if (cpu_parked_in_spin && use_start_pc && has_last_context) {
        DebugContext start_context = last_context;
        start_context.pc = start_pc;
        release_to_run(&start_context);
    } else if (from && from->valid) {
        nmi_redirect_to(from->pc, cpu_port, false);
    } else if (use_start_pc) {
        nmi_redirect_to(start_pc, cpu_port,
                        patch_requires_visible_rom(
                            monitor_backing_store_for_cpu_port(start_pc, cpu_port)));
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
    DebugContext *out, uint8_t cpu_port,
    const MonitorBreakpoints *bps,
    uint16_t skip_breakpoint_address,
    bool skip_breakpoint_address_valid)
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
            uint16_t op = (uint16_t)(read_patch_byte((uint16_t)(start_pc + 1), cpu_port) |
                                     (read_patch_byte((uint16_t)(start_pc + 2), cpu_port) << 8));
            uint16_t op_hi = (uint16_t)((op & 0xFF00) | ((op + 1) & 0x00FF));
            uint16_t target = (uint16_t)(read_patch_byte(op, cpu_port) |
                                         (read_patch_byte(op_hi, cpu_port) << 8));
            addrs[n++] = target;
            break;
        }
        case DBG_PREDICT_RTS: {
            uint16_t traced_target;
            if (peek_return_target(&traced_target)) {
                addrs[n++] = traced_target;
                break;
            }
            if (!from || !from->valid) return DBG_REFUSED;
            uint16_t sp1 = (uint16_t)(0x0100 + ((from->sp + 1) & 0xFF));
            uint16_t sp2 = (uint16_t)(0x0100 + ((from->sp + 2) & 0xFF));
            uint16_t ret = (uint16_t)(peek_cpu(sp1, cpu_port) |
                                      (peek_cpu(sp2, cpu_port) << 8));
            // RTS is only meaningful for an active subroutine frame. Reject
            // stale/forged stack targets early so Over/Trace report a clear
            // "not in subroutine" outcome instead of a generic patch failure.
            uint16_t caller = (uint16_t)(ret - 2);
            if (read_patch_byte(caller, cpu_port) != 0x20) {
                return DBG_NOT_IN_SUBROUTINE;
            }
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
    MonitorBackingStore skip_target =
        monitor_backing_store_for_cpu_port(skip_breakpoint_address, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(
        bps, skip_breakpoint_address, skip_target, skip_breakpoint_address_valid);
    if (bp_patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (bp_patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    for (int i = 0; i < n; i++) {
        PatchInstallResult patched = install_brk_at(addrs[i], cpu_port);
        if (patched != PATCH_INSTALL_OK) {
            restore_patches();
            return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
                DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
        }
    }
    Result result = perform_run(from, start_pc, (!from || !from->valid), out, cpu_port);
    if (result == DBG_OK && out && out->valid) {
        if (pred.kind == DBG_PREDICT_JSR && prefer_jsr_target &&
                pred.has_target && out->pc == pred.branch_target) {
            push_return_target(pred.fall_through);
        } else if ((pred.kind == DBG_PREDICT_RTS || pred.kind == DBG_PREDICT_RTI)) {
            pop_return_target(out->pc);
        }
    }
    return result;
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
    return over(from, pred, 0, ctx);
}

DebugSession::Result BrkDebugSession :: over(const DebugContext &from,
                                             const DebugPredictResult &pred,
                                             const MonitorBreakpoints *bps,
                                             DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = execution_cpu_port(&from);
    return step_with_predict(&from, from.pc, pred, false, ctx, cpu_port,
                             bps, from.pc, true);
}

DebugSession::Result BrkDebugSession :: over_at(uint16_t start_pc,
                                                const DebugPredictResult &pred,
                                                DebugContext *ctx)
{
    return over_at(start_pc, pred, 0, ctx);
}

DebugSession::Result BrkDebugSession :: over_at(uint16_t start_pc,
                                                const DebugPredictResult &pred,
                                                const MonitorBreakpoints *bps,
                                                DebugContext *ctx)
{
    if (!backend_ready() || !ctx) return DBG_REFUSED;
    uint8_t cpu_port = current_cpu_port();
    return step_with_predict(0, start_pc, pred, false, ctx, cpu_port,
                             bps, start_pc, true);
}

DebugSession::Result BrkDebugSession :: trace(const DebugContext &from,
                                              const DebugPredictResult &pred,
                                              DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = execution_cpu_port(&from);
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
    return step_out(from, 0, ctx);
}

DebugSession::Result BrkDebugSession :: step_out(const DebugContext &from,
                                                 const MonitorBreakpoints *bps,
                                                 DebugContext *ctx)
{
    if (!backend_ready() || !ctx || !from.valid) return DBG_REFUSED;
    uint8_t cpu_port = execution_cpu_port(&from);
    uint16_t target;
    if (!peek_return_target(&target)) {
        return DBG_NOT_IN_SUBROUTINE;
    }
    MonitorBackingStore from_target =
        monitor_backing_store_for_cpu_port(from.pc, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(bps, from.pc,
                                                        from_target, true);
    if (bp_patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (bp_patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    PatchInstallResult patched = install_brk_at(target, cpu_port);
    if (patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    Result result = perform_run(&from, from.pc, false, ctx, cpu_port);
    if (result == DBG_OK && ctx->valid && ctx->pc == target) {
        pop_return_target(target);
    }
    if (result == DBG_OK && ctx->valid &&
            context_at_breakpoint(*ctx, bps, from.pc, from_target, true)) {
        return DBG_OK;
    }
    if (result == DBG_OK && (!ctx->valid || ctx->pc != target)) {
        return DBG_RETURN_NOT_REACHED;
    }
    return result;
}

DebugSession::Result BrkDebugSession :: go(const DebugContext &from,
                                           const MonitorBreakpoints *bps,
                                           uint16_t start_pc)
{
    if (!backend_ready()) return DBG_REFUSED;
    uint8_t cpu_port = execution_cpu_port(&from);
    bool skip_current_bp = false;
    bool has_other_bp = false;
    if (from.valid && bps) {
        for (int i = 0; i < bps->slot_count(); i++) {
            const MonitorBreakpointSlot *bp = bps->get(i);
            if (!bp || !bp->used || !bp->enabled) {
                continue;
            }
            if (bp->address == from.pc &&
                    bp->target == monitor_backing_store_for_cpu_port(from.pc, cpu_port)) {
                skip_current_bp = true;
            } else {
                has_other_bp = true;
            }
        }
    }

    const DebugContext *resume_from = &from;
    DebugContext step_ctx;
    if (skip_current_bp && !has_other_bp) {
        uint8_t bytes[3];
        for (int i = 0; i < 3; i++) {
            bytes[i] = read_patch_byte((uint16_t)(from.pc + i), cpu_port);
        }
        DebugPredictResult pred;
        debug_predict(from.pc, bytes, false, &pred);
        Result skip = step_with_predict(&from, from.pc, pred, false, &step_ctx, cpu_port,
                                        bps, from.pc, true);
        if (skip != DBG_OK) {
            return skip;
        }
        if (step_ctx.valid && step_ctx.pc == from.pc) {
            return DBG_OK;
        }
        MonitorBackingStore from_target =
            monitor_backing_store_for_cpu_port(from.pc, cpu_port);
        if (context_at_breakpoint(step_ctx, bps, from.pc, from_target, true)) {
            return DBG_OK;
        }
        DebugContext out;
        return run_to(step_ctx, from.pc, 0, step_ctx.pc, &out);
    }

    MonitorBackingStore from_target =
        monitor_backing_store_for_cpu_port(from.pc, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(
        bps, from.pc, from_target, skip_current_bp);
    if (bp_patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (bp_patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }

    bool any_bp = false;
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) { any_bp = true; break; }
    }
    if (!any_bp) {
        if (cpu_parked_in_spin && resume_from->valid) {
            // No breakpoints remain: hand the interrupted program back to free-
            // running execution from its captured context. resume_from_parked
            // _context() rebuilds a register-restore stub that the parked spin
            // loop falls into and RTIs through, preserving the program's $0001
            // banking, and restores the soft BRK vector, the direct hard BRK
            // vector (the KERNAL-out path), and the NMI trampoline in the same
            // stopped session. This must be used instead of the NMI jump_to()
            // fallback below: that fallback vectors through the KERNAL NMI
            // handler, which is absent when the program runs with KERNAL banked
            // out ($01 != $07), so it would hang the machine.
            restore_patches();
            resume_from_parked_context(*resume_from);
            cpu_parked_in_spin = false;
            has_last_context = false;
            debug_context_reset(&last_context);
            return DBG_OK;
        }
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
    } else if (cpu_parked_in_spin && has_last_context) {
        DebugContext start_context = last_context;
        start_context.pc = resume_from->valid ? resume_from->pc : start_pc;
        release_to_run(&start_context);
    } else if (resume_from->valid) {
        nmi_redirect_to(resume_from->pc, cpu_port, false);
    } else if (start_pc != 0) {
        nmi_redirect_to(start_pc, cpu_port,
                        patch_requires_visible_rom(
                            monitor_backing_store_for_cpu_port(start_pc, cpu_port)));
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

DebugSession::Result BrkDebugSession :: run_to(const DebugContext &from,
                                              uint16_t target_pc,
                                              const MonitorBreakpoints *bps,
                                              uint16_t start_pc,
                                              DebugContext *ctx)
{
    if (!backend_ready() || !ctx) {
        return DBG_REFUSED;
    }

    DebugContext resume_from = from;
    bool have_context = from.valid;
    uint16_t run_pc = have_context ? from.pc : start_pc;
    uint8_t cpu_port = execution_cpu_port(have_context ? &from : 0);

    if (run_pc == target_pc) {
        uint8_t bytes[3];
        for (int i = 0; i < 3; i++) {
            bytes[i] = read_patch_byte((uint16_t)(run_pc + i), cpu_port);
        }
        DebugPredictResult pred;
        debug_predict(run_pc, bytes, false, &pred);
        DebugContext stepped;
        // Escaping an immediate self-hit must execute the current instruction
        // exactly once using real control flow. In particular, K Cursor on a
        // current-row JSR must enter the callee before re-arming the transient
        // cursor breakpoint, not step over to the fall-through address.
        Result skip = step_with_predict(have_context ? &from : 0, run_pc, pred, true,
                                        &stepped, cpu_port, bps, run_pc, true);
        if (skip != DBG_OK) {
            return skip;
        }
        resume_from = stepped;
        have_context = stepped.valid;
        run_pc = have_context ? stepped.pc : run_pc;
        if (have_context && run_pc == target_pc) {
            *ctx = stepped;
            return DBG_OK;
        }
    }

    MonitorBackingStore target_store =
        monitor_backing_store_for_cpu_port(target_pc, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(
        bps, target_pc, target_store, true);
    if (bp_patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (bp_patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    PatchInstallResult patched = install_brk_at(target_pc, cpu_port);
    if (patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    return perform_run(have_context ? &resume_from : 0,
                       have_context ? resume_from.pc : start_pc,
                       !have_context, ctx, cpu_port);
}

void BrkDebugSession :: cleanup(void)
{
    if (!backend_ready()) return;
    clear_return_targets();
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
    release_debug_ownership();
}

void BrkDebugSession :: cleanup_to_context(const DebugContext *ctx)
{
    if (!backend_ready()) return;
    clear_return_targets();
    bool resume_pending = cpu_parked_in_spin && has_last_context;
    restore_patches();
    if (resume_pending && ctx && ctx->valid) {
        resume_from_parked_context(*ctx);
    } else if (resume_pending) {
        resume_from_parked_context(last_context);
    } else {
        uninstall_handler();
    }
    cpu_parked_in_spin = false;
    release_debug_ownership();
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
    clear_return_targets();
}
