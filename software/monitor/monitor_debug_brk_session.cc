#include "monitor_debug_brk_session.h"

#include "disassembler_6502.h"
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
// packed contiguously up to them, with the constant-referenced hard-vector
// scratch bytes placed just below the store block. Layout:
//   $035D-$0389  HANDLER       (45 bytes; ends with the spin JMP at $0387)
//   $038A-$03AF  TRAMPOLINE    (38 bytes)
//   $03B0-$03C7  NMI_TRAMPOLINE(24 bytes reserved)
//   $03C8-$03EC  HARD_BRK_STUB (37 bytes; 35 bytes code + 2 bytes padding)
//   $03ED        STORE_HARD_CPU_PORT
//   $03EE-$03EF  HARD_BRK_ORIG_VECTOR (lo/hi)
//   $03F0-$03FB  STORE_Y .. STORE_HARD_CPU_DDR
static const uint16_t HANDLER_ADDR    = 0x035D;
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
// Low-RAM scratch for a copied instruction plus trailing trap.
static const uint16_t INSN_TRAMPOLINE_ADDR = 0x0340;
static const uint16_t HARD_BRK_STUB_ADDR = 0x03C8;
static const uint16_t HARD_BRK_ORIG_VECTOR_LO = 0x03EE;
static const uint16_t BRK_VECTOR_LO   = 0x0316;
static const uint16_t BRK_VECTOR_HI   = 0x0317;
static const uint16_t IRQ_VECTOR_LO   = 0x0314;
static const uint16_t NMI_VECTOR_LO   = 0x0318;
static const uint16_t NMI_VECTOR_HI   = 0x0319;
static const uint16_t HARD_NMI_VECTOR_LO = 0xFFFA;
static const uint16_t HARD_NMI_VECTOR_HI = 0xFFFB;
static const uint16_t HARD_VECTOR_LO  = 0xFFFE;
static const uint16_t HARD_VECTOR_HI  = 0xFFFF;
static const uint8_t HARD_VECTOR_RAM_CPU_PORT = 0x05;
static const uint8_t HARD_VECTOR_ROM_CPU_PORT = 0x07;
static const uint8_t HARD_VECTOR_DEFAULT_LO = 0x48;
static const uint8_t HARD_VECTOR_DEFAULT_HI = 0xFF;
static const uint16_t DEBUG_AREA_END  = 0x03FB;

// Max extra attempts to re-issue a patched launch when the live 6510 fetched past
// an installed BRK and ran away. The race is rare per launch, so keep it bounded.
static const int MAX_BREAKPOINT_RELAUNCH = 2;
static const int BREAKPOINT_WAIT_MS = 5000;
static const int HIGH_MEMORY_BREAKPOINT_WAIT_MS = 900;

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
    // Six NOP slots delay spin entry a few 6510 cycles after the SENTINEL store
    // to settle the visible-ROM fetch path; HANDLER_ADDR is lowered by 6 so this
    // spin JMP stays at the fixed SPIN_JMP ($0387), whose operand is self-modified.
    0xEA,
    0xEA,
    0xEA,
    0xEA,
    0xEA,
    0xEA,
    0x4C, (uint8_t)(SPIN_JMP & 0xFF), (uint8_t)(SPIN_JMP >> 8)
};

static const uint8_t TRAMPOLINE_BYTES[] = {
    0xAD, (uint8_t)(STORE_CPU_DDR & 0xFF),
          (uint8_t)(STORE_CPU_DDR >> 8),
    0x85, 0x00,
    0xAD, (uint8_t)(STORE_CPU_PORT & 0xFF),
          (uint8_t)(STORE_CPU_PORT >> 8),
    0x85, 0x01,
    0xAE, (uint8_t)(STORE_SP & 0xFF), (uint8_t)(STORE_SP >> 8),
    0x9A,
    0xAD, (uint8_t)(STORE_PCHI & 0xFF), (uint8_t)(STORE_PCHI >> 8),
    0x48,
    0xAD, (uint8_t)(STORE_PCLO & 0xFF), (uint8_t)(STORE_PCLO >> 8),
    0x48,
    0xAD, (uint8_t)(STORE_SR & 0xFF), (uint8_t)(STORE_SR >> 8),
    0x48,
    0xAC, (uint8_t)(STORE_Y & 0xFF), (uint8_t)(STORE_Y >> 8),
    0xAE, (uint8_t)(STORE_X & 0xFF), (uint8_t)(STORE_X >> 8),
    0xAD, (uint8_t)(STORE_A & 0xFF), (uint8_t)(STORE_A >> 8),
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

static bool address_is_banked_high_memory(uint16_t addr)
{
    return (addr >= 0xA000 && addr <= 0xBFFF) || addr >= 0xE000;
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
      reset_cancel_requested(false),
      nmi_trampoline_installed(false), hard_nmi_vector_installed(false),
      hard_vector_installed(false),
      hard_rom_vector_installed(false), has_last_context(false),
      has_resume_context(false),
      rom_bp_hit_pc_valid(false), rom_bp_hit_pc(0),
      return_target_count(0)
{
    memset(patches, 0, sizeof(patches));
    memset(return_targets, 0, sizeof(return_targets));
    memset(saved_handler_bytes, 0, sizeof(saved_handler_bytes));
    memset(saved_nmi_trampoline_bytes, 0, sizeof(saved_nmi_trampoline_bytes));
    memset(saved_nmi_vector, 0, sizeof(saved_nmi_vector));
    memset(saved_brk_vector, 0, sizeof(saved_brk_vector));
    memset(saved_hard_nmi_vector, 0, sizeof(saved_hard_nmi_vector));
    memset(saved_hard_vector, 0, sizeof(saved_hard_vector));
    memset(saved_hard_rom_vector, 0, sizeof(saved_hard_rom_vector));
    memset(saved_hard_brk_stub_bytes, 0, sizeof(saved_hard_brk_stub_bytes));
    memset(saved_hard_brk_vector_ptr, 0, sizeof(saved_hard_brk_vector_ptr));
    debug_context_reset(&last_context);
    debug_context_reset(&resume_context);
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

void BrkDebugSession :: request_reset_cancel(void)
{
    // If we are inside an active run-window the CPU is running freely and
    // wait_for_sentinel() is blocking. On FreeRTOS single-core the calling
    // task owns the CPU while the UI task is in vTaskDelay, so it is safe to
    // tear down patches and handler here before the machine reset pulse fires.
    // Do NOT call these when the CPU is parked in the spin loop
    // (run_window_depth == 0): uninstall_handler() would overwrite the spin
    // loop code the CPU is executing, corrupting the parked debug state. Any BRK
    // left in the FPGA ROM image while parked is healed by C64::reset() (it
    // restores the pristine ROM image whenever the monitor flagged it dirty), so
    // a leaked ROM-image trap can no longer wedge the next boot.
    if (run_window_depth > 0) {
        restore_patches();
        uninstall_handler();
        cpu_parked_in_spin = false;
        has_last_context = false;
        debug_context_reset(&last_context);
        has_resume_context = false;
        debug_context_reset(&resume_context);
        rom_bp_hit_pc_valid = false;
    }
    reset_cancel_requested = true;
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
        // run_window_refreeze_enabled is set (in run_machine_monitor) only when
        // the monitor renders through the C64 freeze screen -- host == the C64
        // and frozen at launch -- never on the telnet/overlay path. So re-taking
        // freeze ownership at end_run_window() only ever re-freezes a machine
        // that belongs frozen (even if isFrozen went stale/false mid-session);
        // C64::refreeze() is idempotent, so it never double-freezes a live one.
        // This keeps menu_screen/input attached across the run window.
        run_window_unfroze = run_window_refreeze_enabled;
        if (run_window_refreeze_enabled && machine_is_frozen()) {
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
    uint8_t lo = read_patch_byte(HARD_VECTOR_LO, HARD_VECTOR_ROM_CPU_PORT);
    uint8_t hi = read_patch_byte(HARD_VECTOR_HI, HARD_VECTOR_ROM_CPU_PORT);
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
    // read_patch_byte, not peek_cpu: the saved bytes get RESTORED into the
    // ROM image later, and under the freezer a raw aperture read returns the
    // freezer cart's garbage, which would trash the IRQ vector on uninstall.
    saved_hard_rom_vector[0] = read_patch_byte(HARD_VECTOR_LO,
                                               HARD_VECTOR_ROM_CPU_PORT);
    saved_hard_rom_vector[1] = read_patch_byte(HARD_VECTOR_HI,
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

void BrkDebugSession :: save_and_install_hard_nmi_vector(uint8_t cpu_port)
{
    if (monitor_backing_store_for_cpu_port(HARD_NMI_VECTOR_LO, cpu_port) !=
            MONITOR_BACKING_RAM) {
        if (hard_nmi_vector_installed) {
            uninstall_hard_nmi_vector();
        }
        return;
    }
    if (!hard_nmi_vector_installed) {
        saved_hard_nmi_vector[0] = peek_cpu(HARD_NMI_VECTOR_LO,
                                            HARD_VECTOR_RAM_CPU_PORT);
        saved_hard_nmi_vector[1] = peek_cpu(HARD_NMI_VECTOR_HI,
                                            HARD_VECTOR_RAM_CPU_PORT);
        hard_nmi_vector_installed = true;
    }
    poke_cpu(HARD_NMI_VECTOR_LO, (uint8_t)(NMI_TRAMPOLINE_ADDR & 0xFF),
             HARD_VECTOR_RAM_CPU_PORT);
    poke_cpu(HARD_NMI_VECTOR_HI, (uint8_t)(NMI_TRAMPOLINE_ADDR >> 8),
             HARD_VECTOR_RAM_CPU_PORT);
}

void BrkDebugSession :: uninstall_hard_nmi_vector(void)
{
    if (!hard_nmi_vector_installed) {
        return;
    }
    poke_cpu(HARD_NMI_VECTOR_LO, saved_hard_nmi_vector[0],
             HARD_VECTOR_RAM_CPU_PORT);
    poke_cpu(HARD_NMI_VECTOR_HI, saved_hard_nmi_vector[1],
             HARD_VECTOR_RAM_CPU_PORT);
    hard_nmi_vector_installed = false;
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
        poke_visible_preserving_freeze_restore(
            (uint16_t)(HARD_BRK_STUB_ADDR + i),
            saved_hard_brk_stub_bytes[i]);
    }
    poke_visible_preserving_freeze_restore(HARD_BRK_ORIG_VECTOR_LO,
                                           saved_hard_brk_vector_ptr[0]);
    poke_visible_preserving_freeze_restore(
        (uint16_t)(HARD_BRK_ORIG_VECTOR_LO + 1),
        saved_hard_brk_vector_ptr[1]);
    hard_vector_installed = false;
}

void BrkDebugSession :: uninstall_handler(void)
{
    if (!handler_installed && !nmi_trampoline_installed &&
            !hard_nmi_vector_installed && !hard_vector_installed) {
        return;
    }
    bool stopped_it = begin_stopped_session();
    uninstall_hard_nmi_vector();
    uninstall_hard_vector();
    if (handler_installed) {
        poke_visible_preserving_freeze_restore(BRK_VECTOR_LO,
                                               saved_brk_vector[0]);
        poke_visible_preserving_freeze_restore(BRK_VECTOR_HI,
                                               saved_brk_vector[1]);
        for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
            poke_visible_preserving_freeze_restore(
                (uint16_t)(HANDLER_ADDR + i), saved_handler_bytes[i]);
        }
        for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
            poke_visible_preserving_freeze_restore(
                (uint16_t)(TRAMPOLINE_ADDR + i),
                saved_handler_bytes[HANDLER_BYTES_LEN + i]);
        }
    }
    if (nmi_trampoline_installed) {
        poke_visible_preserving_freeze_restore(NMI_VECTOR_LO,
                                               saved_nmi_vector[0]);
        poke_visible_preserving_freeze_restore(NMI_VECTOR_HI,
                                               saved_nmi_vector[1]);
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
            poke_visible_preserving_freeze_restore(
                (uint16_t)(NMI_TRAMPOLINE_ADDR + i),
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
    if (verify && visible_rom_patch && machine_is_frozen()) {
        // Frozen read-back sees the backup, not the served ROM image.
        verify = false;
    }
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
    MonitorBackingStore skip_target, bool skip_address_valid,
    bool skip_all_at_address)
{
    if (!bps) {
        return PATCH_INSTALL_OK;
    }
    for (int i = 0; i < bps->slot_count(); i++) {
        const MonitorBreakpointSlot *bp = bps->get(i);
        if (!bp || !bp->used || !bp->enabled) {
            continue;
        }
        bool skipped = skip_address_valid && bp->address == skip_address &&
                (skip_all_at_address || bp->target == skip_target);
        if (skipped) {
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
    bool skip_address_valid, bool skip_all_at_address) const
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
                (skip_all_at_address || bp->target == skip_target)) {
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
    bool restored_visible_rom = false;
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) {
            if (monitor_backing_store_is_visible_rom(patches[i].target)) {
                restored_visible_rom = true;
            }
            write_patch_byte(patches[i].address, patches[i].original,
                             patches[i].cpu_port);
            patches[i].used = false;
        }
    }
    if (restored_visible_rom) {
        settle_visible_rom_for_live_fetch();
    }
    end_stopped_session(stopped_it);
}

bool BrkDebugSession :: recommit_visible_rom_patches(void)
{
    // Re-write each installed visible-ROM BRK ($00) as the final memory write
    // before the CPU is released to run. On U64 the FPGA ROM-image write made by
    // install_brk_at is occasionally not yet observed by the LIVE 6510 fetch when
    // the CPU executes the freshly-patched ROM byte immediately after the launch
    // (overlay/Telnet, where the machine is DMA-stopped/resumed several times and
    // the CPU runs between patch and launch). Committing the byte again here -
    // inside the single stopped session that releases the CPU, with the backend's
    // CPU-visible read-back barrier - closes that window. RAM patches are
    // unaffected (RAM writes are always coherent), so only visible-ROM patches are
    // re-committed to keep this cheap.
    bool any = false;
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used &&
                monitor_backing_store_is_visible_rom(patches[i].target)) {
            write_patch_byte(patches[i].address, 0x00, patches[i].cpu_port);
            any = true;
        }
    }
    return any;
}

bool BrkDebugSession :: patch_installed_at(uint16_t addr,
                                           MonitorBackingStore target) const
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used && patches[i].address == addr &&
                patches[i].target == target) {
            return true;
        }
    }
    return false;
}

bool BrkDebugSession :: recommit_visible_rom_fetch_byte(uint16_t addr,
                                                        uint8_t cpu_port)
{
    MonitorBackingStore target = monitor_backing_store_for_cpu_port(addr,
                                                                    cpu_port);
    if (!monitor_backing_store_is_visible_rom(target) ||
            patch_installed_at(addr, target)) {
        return false;
    }
    write_patch_byte(addr, read_patch_byte(addr, cpu_port), cpu_port);
    return true;
}

bool BrkDebugSession :: has_any_patch(void) const
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used) {
            return true;
        }
    }
    return false;
}

bool BrkDebugSession :: has_banked_ram_patch(void) const
{
    // True for banked high-memory patches that do not use the visible-ROM image
    // path. Visible ROM has its own live-fetch settle; RAM under BASIC/KERNAL
    // needs the raster-synced clean stop/release.
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used &&
                address_is_banked_high_memory(patches[i].address) &&
                !monitor_backing_store_is_visible_rom(patches[i].target)) {
            return true;
        }
    }
    return false;
}

bool BrkDebugSession :: has_high_memory_patch(void) const
{
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used &&
                address_is_banked_high_memory(patches[i].address)) {
            return true;
        }
    }
    return false;
}

bool BrkDebugSession :: has_visible_rom_patch(void) const
{
    // True when a BRK patch sits in a visible-ROM window (BASIC/KERNAL/CHAR
    // image), whose live 6510 instruction-fetch path can lag a DMA commit. Used
    // to gate the sustained fetch-path settle on a contextless launch toward
    // such a patch, independent of the launch/bootstrap PC's own bank.
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used &&
                monitor_backing_store_is_visible_rom(patches[i].target)) {
            return true;
        }
    }
    return false;
}


bool BrkDebugSession :: begin_clean_stopped_session(void)
{
    // Default: same as a normal stopped session. Backends whose plain stop/resume
    // is not reliable for immediate patched high-memory fetches override this with
    // a raster-synced stop that mirrors the freeze path.
    return begin_stopped_session();
}

bool BrkDebugSession :: captured_at_installed_patch(uint16_t *captured_brk_pc)
{
    // The BRK handler stores the trap return address (BRK location + 2) at
    // STORE_PCLO/HI. A controlled launch is expected to trap at one of our
    // installed BRK patches; a captured PC that matches no installed patch means
    // the live 6510 ran past the expected target and tripped some unrelated $00.
    bool stopped_it = begin_stopped_session();
    uint8_t lo = peek_visible(STORE_PCLO);
    uint8_t hi = peek_visible(STORE_PCHI);
    end_stopped_session(stopped_it);
    uint16_t brk_pc = (uint16_t)((uint16_t)(lo | (hi << 8)) - 2);
    if (captured_brk_pc) {
        *captured_brk_pc = brk_pc;
    }
    for (int i = 0; i < MAX_PATCHES; i++) {
        if (patches[i].used && patches[i].address == brk_pc) {
            return true;
        }
    }
    return false;
}

void BrkDebugSession :: reinstall_handler_bytes(void)
{
    // Re-write the BRK handler + restore trampoline scratch and the BRK soft
    // vector WITHOUT re-saving the program's originals (handler_installed stays
    // true; saved_* are preserved). Used only on the runaway-relaunch path to
    // repair any cassette-buffer scratch a runaway may have overwritten before we
    // re-park the CPU. A no-op-equivalent rewrite when the scratch is intact.
    bool stopped_it = begin_stopped_session();
    for (int i = 0; i < HANDLER_BYTES_LEN; i++) {
        poke_visible((uint16_t)(HANDLER_ADDR + i), HANDLER_BYTES[i]);
    }
    for (int i = 0; i < TRAMPOLINE_BYTES_LEN; i++) {
        poke_visible((uint16_t)(TRAMPOLINE_ADDR + i), TRAMPOLINE_BYTES[i]);
    }
    poke_visible(BRK_VECTOR_LO, (uint8_t)(HANDLER_ADDR & 0xFF));
    poke_visible(BRK_VECTOR_HI, (uint8_t)(HANDLER_ADDR >> 8));
    end_stopped_session(stopped_it);
}

void BrkDebugSession :: repark_running_cpu(uint8_t cpu_port)
{
    // Bring a CPU that is either free-running (launch timed out) or parked at the
    // wrong PC (stray-$00 runaway capture) back into the controlled spin loop, so
    // a clean parked relaunch can be retried. The NMI redirect target is the RAM
    // self-loop (SPIN_JMP), so there is no ROM-fetch coherency race on the redirect
    // itself. nmi_redirect_to() rewrites the NMI trampoline + vector, so it is
    // self-repairing if a runaway clobbered them.
    reinstall_handler_bytes();
    // The spin JMP opcode itself is otherwise only written by
    // resume_from_parked_context: a contextless first launch (or a runaway
    // that scribbled the scratch) has no guarantee $0387 holds the JMP, and
    // parking into a garbage byte free-runs the CPU instead. Install the
    // complete self-loop before redirecting into it.
    bool stopped_it = begin_stopped_session();
    poke_visible(SPIN_JMP, 0x4C);
    end_stopped_session(stopped_it);
    reset_spin_target();
    nmi_redirect_to(SPIN_JMP, cpu_port, false, false);
    delay_ms(1);
    cpu_parked_in_spin = true;
}


DebugSession::Result BrkDebugSession :: relaunch_on_breakpoint_runaway(
    DebugSession::Result waited, const DebugContext *launch_ctx,
    bool nmi_launch_valid, uint16_t nmi_target, bool nmi_force_cpu_port,
    uint8_t cpu_port, int wait_ms)
{
    // Self-heal a launch that missed an installed BRK patch. A miss either never
    // traps (timeout) or traps at an unrelated $00 (captured at the wrong PC).
    // On detection, re-park the CPU and re-issue the launch. Context launches use
    // the parked restore path so registers are preserved. No-context monitor
    // starts cannot synthesize registers, so they repeat the NMI redirect.
    // Normal launches return immediately because captured_at_installed_patch() is
    // true, so RAM single-step precision is untouched.
    if ((!launch_ctx && !nmi_launch_valid) || !has_any_patch()) {
        return waited;
    }
    for (int attempt = 0; attempt < MAX_BREAKPOINT_RELAUNCH; attempt++) {
        bool runaway;
        bool launch_byte_last = false;
        if (waited == DBG_TIMEOUT) {
            runaway = true;
        } else if (waited == DBG_OK) {
            uint16_t captured_brk_pc;
            if (captured_at_installed_patch(&captured_brk_pc)) {
                return waited;   // legitimate trap at one of our BRKs
            }
            if (launch_ctx && launch_ctx->valid &&
                    captured_brk_pc == launch_ctx->pc) {
                launch_byte_last = true;
            }
            runaway = true;
        } else {
            return waited;       // cancelled / reset / refused - never retry
        }
        if (!runaway) {
            return waited;
        }
        repark_running_cpu(cpu_port);
        if (launch_ctx && launch_byte_last) {
            bool force_cpu_port = patch_requires_visible_rom(
                monitor_backing_store_for_cpu_port(launch_ctx->pc, cpu_port));
            nmi_redirect_to(launch_ctx->pc, cpu_port, force_cpu_port, false);
        } else if (launch_ctx) {
            release_to_run(launch_ctx, launch_byte_last);
        } else {
            // No-context relaunch after a visible-ROM launch missed its BRK: the
            // CPU has been reparked in the RAM spin loop, far from the target, so
            // settle the fetch path before re-redirecting (same rationale as the
            // initial contextless visible-ROM launch). Gate the settle on the
            // installed breakpoint bank, not nmi_force_cpu_port: that flag is
            // derived from the RAM-bootstrap launch PC and is false even when the
            // target BRK sits in a fetch-lagging ROM window, so using it would
            // skip the settle in exactly the case this branch exists to heal.
            // This else covers only launch_ctx == 0, so has_visible_rom_patch()
            // alone matches perform_run/go's rom_entry_settle gate.
            nmi_redirect_to(nmi_target, cpu_port, nmi_force_cpu_port, false,
                            has_visible_rom_patch());
        }
        waited = wait_for_sentinel(wait_ms);
    }
    return waited;
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
        if (reset_cancel_requested) {
            restore_patches();
            uninstall_handler();
            cpu_parked_in_spin = false;
            has_last_context = false;
            debug_context_reset(&last_context);
            has_resume_context = false;
            debug_context_reset(&resume_context);
            return DBG_RESET;
        }
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
                has_resume_context = false;
                debug_context_reset(&resume_context);
                reset_cancel_requested = true;
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

    // Refresh the $00/$01 RAM mirror with the registers the capture stub read
    // on the real CPU. The U64's FPGA 6510 never writes the port through to
    // the RAM underneath, so DMA-side readers (the live-bank display and the
    // breakpoint "not mapped now" check) would otherwise keep seeing whatever
    // a DMA write last left there. On a real 6510 these bytes already match.
    poke_cpu(0x0000, ctx->cpu_ddr, live_cpu_port);
    poke_cpu(0x0001, ctx->cpu_port_latch, live_cpu_port);

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
    poke_visible_preserving_freeze_restore(STORE_CPU_DDR, from.cpu_ddr);
    poke_visible_preserving_freeze_restore(STORE_CPU_PORT,
                                           from.cpu_port_latch);
}

void BrkDebugSession :: release_to_run(const DebugContext *from,
                                       bool launch_byte_last)
{
    // Banked RAM-under-ROM launches use a raster-synced ("clean") stopped session
    // so the BRK commit + CPU release happen the way the reliable freeze path does
    // it. Visible-ROM patches use the ROM-image settle below instead; ordinary RAM
    // single-steps keep the low-latency forced-stop path.
    bool clean = has_banked_ram_patch();
    bool stopped_it = clean ? begin_clean_stopped_session() : begin_stopped_session();
    if (from && from->valid) {
        restore_cpu_port_registers(*from);
        poke_visible(STORE_Y, from->y);
        poke_visible(STORE_X, from->x);
        poke_visible(STORE_A, from->a);
        poke_visible(STORE_SR, from->sr);
        poke_visible(STORE_PCLO, (uint8_t)(from->pc & 0xFF));
        poke_visible(STORE_PCHI, (uint8_t)(from->pc >> 8));
        poke_visible(STORE_SP, from->sp);
    }
    // Final ROM-image commits before the parked CPU is released back into the
    // restore stub. Besides installed BRKs, re-commit the launch opcode itself
    // when it lives in visible ROM: clearing a visible-ROM breakpoint restores the
    // byte in the ROM image, but the live fetch path can still see the stale BRK
    // unless the restored opcode is rewritten immediately before release.
    bool visible_rom_recommitted = false;
    if (!launch_byte_last && from && from->valid) {
        visible_rom_recommitted =
            recommit_visible_rom_fetch_byte(from->pc, execution_cpu_port(from));
    }
    if (recommit_visible_rom_patches()) {
        visible_rom_recommitted = true;
    }
    if (launch_byte_last && from && from->valid) {
        visible_rom_recommitted =
            recommit_visible_rom_fetch_byte(from->pc, execution_cpu_port(from)) ||
            visible_rom_recommitted;
    }
    // Only the first step resuming where a free-run breakpoint just trapped in
    // visible ROM needs the sustained settle: that fetch line still holds the BRK
    // and only a continuous run refreshes it.
    bool sustained_settle = false;
    if (from && from->valid && rom_bp_hit_pc_valid &&
            from->pc == rom_bp_hit_pc) {
        sustained_settle = true;
        rom_bp_hit_pc_valid = false;
    }
    // Settle for the re-trap relaunch, and for every parked non-frozen release
    // that carries visible-ROM patches: while the CPU sits in the RAM spin
    // loop the settle's stop/resume cannot run it past anything, and the
    // sustained continuous run is the only thing that refreshes the live
    // instruction-fetch ROM copy - without it a G/continue released from a
    // parked ROM context can miss a ROM-image breakpoint whose fetch line is
    // hot with the pre-patch byte (reproducible under concurrent REST/DMA
    // load). Plain forward steps in ROM never release (parked emulation), so
    // stepping latency is unaffected; the freeze path keeps the old gating
    // because its settle frees the live program, not a spin loop.
    bool parked_release = from && from->valid;
    if (visible_rom_recommitted &&
            (sustained_settle || (parked_release && !machine_is_frozen()))) {
        settle_visible_rom_for_live_fetch(true);
    }
    if (from && from->valid) {
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
    // Handing the CPU back to free-running operation must re-enable interrupts so
    // the C64 returns to a LIVE runtime. The debugger single-steps with interrupts
    // masked (I=1) so a step is not interrupted; that mask is faithfully captured
    // into ctx->sr (a program's own CLI/SEI is preserved - verified), but for a
    // program that never re-enables interrupts (e.g. BASIC's idle loop) it leaks
    // into the resume and the cursor/keyboard/jiffy stay dead until a reset. Clear
    // I here so leaving Debug/closing the monitor resumes a live machine.
    //
    // ONLY when KERNAL is mapped (HIRAM, $01 bit 1, set): a program running with
    // KERNAL banked out has no IRQ handler at $FFFE and legitimately requires
    // I=1, so re-enabling interrupts there would wedge it worse than the freeze.
    // Use the DDR-resolved effective bank when available.
    uint8_t resume_effective_port = from.live_cpu_port_valid ?
        (uint8_t)(from.live_cpu_port & 0x07) : (uint8_t)(resume_port & 0x07);
    uint8_t resume_sr = from.sr;
    if (resume_effective_port & 0x02) {
        resume_sr &= (uint8_t)~0x04;
    }
    poke_visible_preserving_freeze_restore(0x0000, resume_ddr);
    poke_visible_preserving_freeze_restore(0x0001, resume_port);
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
        0xA9, resume_sr,
        0x48,
        0xA0, from.y,
        0xA2, from.x,
        0xA9, from.a,
        0x40
    };
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        poke_visible_preserving_freeze_restore((uint16_t)(HANDLER_ADDR + i),
                                               bytes[i]);
    }
    poke_visible_preserving_freeze_restore(SPIN_JMP, 0x4C);
    poke_visible_preserving_freeze_restore(SPIN_OPERAND_LO,
                                           (uint8_t)(HANDLER_ADDR & 0xFF));
    poke_visible_preserving_freeze_restore(SPIN_OPERAND_HI,
                                           (uint8_t)(HANDLER_ADDR >> 8));
    // Restore the soft vectors the interrupted program needs once it resumes,
    // inside this same stopped session so a live (overlay-mode) CPU sees them
    // before it leaves the spin loop. The handler/trampoline scratch bytes in
    // the cassette buffer are deliberately left in place: the CPU is about to
    // execute the restore stub that lives there. Clearing handler_installed
    // makes any later uninstall_handler() a no-op so it can never overwrite the
    // stub the CPU may still be running.
    if (handler_installed) {
        poke_visible_preserving_freeze_restore(BRK_VECTOR_LO,
                                               saved_brk_vector[0]);
        poke_visible_preserving_freeze_restore(BRK_VECTOR_HI,
                                               saved_brk_vector[1]);
        handler_installed = false;
    }
    uninstall_hard_nmi_vector();
    uninstall_hard_vector();
    if (nmi_trampoline_installed) {
        poke_visible_preserving_freeze_restore(NMI_VECTOR_LO,
                                               saved_nmi_vector[0]);
        poke_visible_preserving_freeze_restore(NMI_VECTOR_HI,
                                               saved_nmi_vector[1]);
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
            poke_visible_preserving_freeze_restore(
                (uint16_t)(NMI_TRAMPOLINE_ADDR + i),
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
                                        bool force_cpu_port, bool staged,
                                        bool sustained_settle)
{
    bool clean = has_banked_ram_patch();
    bool stopped_it = clean ? begin_clean_stopped_session() : begin_stopped_session();
    uint8_t old_nmi_lo = peek_visible(NMI_VECTOR_LO);
    uint8_t old_nmi_hi = peek_visible(NMI_VECTOR_HI);
    uint8_t restore_nmi_lo = old_nmi_lo;
    uint8_t restore_nmi_hi = old_nmi_hi;
    // Built up to exactly NMI_TRAMPOLINE_BYTES_LEN bytes (3 stack pulls + NMI
    // vector restore + optional CPU-port force + JMP target). Sized off the
    // constant so adding an instruction can never silently overflow.
    uint8_t bytes[NMI_TRAMPOLINE_BYTES_LEN];
    int len = 0;

    if (nmi_trampoline_installed) {
        restore_nmi_lo = saved_nmi_vector[0];
        restore_nmi_hi = saved_nmi_vector[1];
    } else {
        saved_nmi_vector[0] = old_nmi_lo;
        saved_nmi_vector[1] = old_nmi_hi;
        for (int i = 0; i < NMI_TRAMPOLINE_BYTES_LEN; i++) {
            saved_nmi_trampoline_bytes[i] =
                peek_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i));
        }
        nmi_trampoline_installed = true;
    }

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
    bytes[len++] = restore_nmi_lo;
    bytes[len++] = 0x8D;
    bytes[len++] = (uint8_t)(NMI_VECTOR_LO & 0xFF);
    bytes[len++] = (uint8_t)(NMI_VECTOR_LO >> 8);
    bytes[len++] = 0xA9;
    bytes[len++] = restore_nmi_hi;
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

    for (int i = 0; i < len; i++) {
        poke_visible((uint16_t)(NMI_TRAMPOLINE_ADDR + i), bytes[i]);
    }
    poke_visible(NMI_VECTOR_LO, (uint8_t)(NMI_TRAMPOLINE_ADDR & 0xFF));
    poke_visible(NMI_VECTOR_HI, (uint8_t)(NMI_TRAMPOLINE_ADDR >> 8));
    save_and_install_hard_nmi_vector(cpu_port);
    // Final ROM-image commits before the CPU is released (see method note):
    // guarantees the live 6510 fetches the intended ROM byte rather than a stale
    // pre-patch or pre-restore byte in the overlay/Telnet launch path.
    bool visible_rom_recommitted =
        recommit_visible_rom_fetch_byte(target, cpu_port);
    if (recommit_visible_rom_patches()) {
        visible_rom_recommitted = true;
    }
    // Settle only on the bp-hit re-trap (see release_to_run): on a plain launch
    // the extra stop/resume frees the 6510 and lets it run past the BRK.
    if (visible_rom_recommitted && sustained_settle) {
        settle_visible_rom_for_live_fetch(sustained_settle);
    }
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
    if (staged) {
        end_stopped_session(stopped_it);
    } else {
        // The NMI request must be raised while the CPU is still stopped, then
        // released during resume so the CPU observes the pending edge. The
        // backend hook handles request+release+clear in one atomic operation.
        pulse_nmi_and_release(stopped_it);
    }
    cpu_parked_in_spin = false;
}

DebugSession::Result BrkDebugSession :: perform_run(const DebugContext *from,
                                                    uint16_t start_pc,
                                                    bool use_start_pc,
                                                    DebugContext *out,
                                                    uint8_t cpu_port)
{
    reset_cancel_requested = false;
    refresh_debug_ownership();
    save_and_install_handler();
    // Relaunch metadata for the runaway retry.
    const DebugContext *launch_ctx = 0;
    DebugContext start_context;
    bool nmi_launch_valid = false;
    uint16_t nmi_launch_target = 0;
    bool nmi_launch_force_cpu_port = false;
    if (cpu_parked_in_spin && from && from->valid) {
        launch_ctx = from;
        begin_run_window();
        release_to_run(from);
    } else if (cpu_parked_in_spin && use_start_pc && has_last_context) {
        start_context = last_context;
        start_context.pc = start_pc;
        launch_ctx = &start_context;
        begin_run_window();
        release_to_run(&start_context);
    } else if (from && from->valid) {
        launch_ctx = from;
        bool staged = run_window_refreeze_enabled && machine_is_frozen();
        nmi_redirect_to(from->pc, cpu_port, false, staged);
        if (staged) {
            request_staged_nmi();
        }
        begin_run_window();
        if (staged) {
            clear_staged_nmi();
        }
    } else if (use_start_pc) {
        // Non-parked run-to/breakpoint launch. Provide captured registers when
        // available; otherwise high-memory monitor starts retry through the same
        // NMI redirect because there are no registers to synthesize.
        if (has_any_patch()) {
            if (has_last_context) {
                start_context = last_context;
                start_context.pc = start_pc;
                launch_ctx = &start_context;
            } else if (has_resume_context && resume_context.valid) {
                start_context = resume_context;
                start_context.pc = start_pc;
                launch_ctx = &start_context;
            }
        }
        bool staged = run_window_refreeze_enabled && machine_is_frozen();
        bool force_cpu_port = patch_requires_visible_rom(
            monitor_backing_store_for_cpu_port(start_pc, cpu_port));
        if (!launch_ctx && has_high_memory_patch()) {
            nmi_launch_valid = true;
            nmi_launch_target = start_pc;
            nmi_launch_force_cpu_port = force_cpu_port;
        }
        // Contextless visible-ROM launch (bp+Go / run-to a KERNAL/BASIC target
        // from a RAM bootstrap): the freshly DMA-committed ROM-image BRK can lag
        // the live 6510 instruction-fetch path, so the CPU races past the target
        // BRK and never traps (intermittent entry "DEBUG TIMEOUT" under load).
        // Unlike a parked/context launch, here the CPU sits at its own running PC
        // far from the target, so a sustained fetch-path settle safely clocks
        // that unrelated code to propagate the ROM image into the fetch path
        // before the NMI redirects execution toward the target. Skipped when a
        // launch context is present (the CPU is at the launch PC and a settle
        // could run it past a nearby BRK) - matching release_to_run's gating.
        // Gate on the installed BREAKPOINT bank (visible ROM), not the launch
        // PC's bank: the bootstrap that the CPU is redirected to lives in RAM, so
        // force_cpu_port (derived from start_pc) is false here even though the
        // target BRK is in a fetch-lagging ROM window.
        bool rom_entry_settle = (launch_ctx == 0) && has_visible_rom_patch();
        nmi_redirect_to(start_pc, cpu_port,
                        force_cpu_port, staged, rom_entry_settle);
        if (staged) {
            request_staged_nmi();
        }
        begin_run_window();
        if (staged) {
            clear_staged_nmi();
        }
    } else {
        begin_run_window();
        release_to_run(0);
    }
    int wait_ms = has_high_memory_patch() ?
        HIGH_MEMORY_BREAKPOINT_WAIT_MS : BREAKPOINT_WAIT_MS;
    Result waited = wait_for_sentinel(wait_ms);
    waited = relaunch_on_breakpoint_runaway(
        waited, launch_ctx, nmi_launch_valid, nmi_launch_target,
        nmi_launch_force_cpu_port, cpu_port, wait_ms);
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
    // A visible-ROM BRK capture can leave the live fetch path holding that BRK
    // for the captured PC. The next launch from that exact PC needs the one-shot
    // sustained ROM-fetch settle, regardless of whether the capture came from
    // Go/breakpoint handling or a step operation.
    if (out && out->valid &&
            monitor_backing_store_is_visible_rom(
                monitor_backing_store_for_cpu_port(out->pc, cpu_port))) {
        rom_bp_hit_pc_valid = true;
        rom_bp_hit_pc = out->pc;
    }
    end_run_window();
    return DBG_OK;
}

DebugSession::Result BrkDebugSession :: step_linear_via_trampoline(
    const DebugContext *from, uint16_t start_pc,
    const DebugPredictResult &pred, DebugContext *out, uint8_t cpu_port,
    const uint8_t *instruction_bytes)
{
    if (pred.length == 0 || pred.length > 3) {
        return DBG_REFUSED;
    }
    // Fetch the instruction through read_step_bytes when the caller did not
    // supply it: under the freezer the live aperture (peek_cpu) does not
    // serve BASIC/KERNAL for ROM addresses, so a raw read would copy
    // freezer-cart garbage into the trampoline.
    uint8_t fetched[3] = { 0, 0, 0 };
    if (!instruction_bytes) {
        if (!read_step_bytes(start_pc, fetched, pred.length)) {
            return DBG_NOT_SUPPORTED;
        }
        instruction_bytes = fetched;
    }
    bool stopped = begin_stopped_session();
    for (uint8_t i = 0; i < pred.length; i++) {
        poke_cpu((uint16_t)(INSN_TRAMPOLINE_ADDR + i),
                 instruction_bytes[i], cpu_port);
    }
    end_stopped_session(stopped);

    PatchInstallResult patched = install_brk_at(
        (uint16_t)(INSN_TRAMPOLINE_ADDR + pred.length), cpu_port);
    if (patched != PATCH_INSTALL_OK) {
        restore_patches();
        return (patched == PATCH_INSTALL_NOT_SUPPORTED) ?
            DBG_NOT_SUPPORTED : DBG_PATCH_FAILED;
    }
    save_and_install_visible_hard_vector();
    Result result;
    if (from && from->valid && cpu_parked_in_spin) {
        DebugContext launch = *from;
        launch.pc = INSN_TRAMPOLINE_ADDR;
        result = perform_run(&launch, INSN_TRAMPOLINE_ADDR, false, out, cpu_port);
    } else {
        if (from && from->valid) {
            last_context = *from;
            has_last_context = true;
        }
        cpu_parked_in_spin = false;
        result = perform_run(0, INSN_TRAMPOLINE_ADDR, true, out, cpu_port);
    }
    if (result == DBG_OK && out && out->valid) {
        out->pc = pred.fall_through;
        if (has_last_context) {
            last_context.pc = pred.fall_through;
        }
    }
    return result;
}

static void set_nz(uint8_t *sr, uint8_t value)
{
    *sr = (uint8_t)((*sr & (uint8_t)~0x82) |
        (value == 0 ? 0x02 : 0x00) |
        (value & 0x80));
}

namespace {

// 6502 SR bits.
enum {
    SR_C = 0x01, SR_Z = 0x02, SR_I = 0x04, SR_D = 0x08,
    SR_B = 0x10, SR_U = 0x20, SR_V = 0x40, SR_N = 0x80
};

static void set_flag(uint8_t *sr, uint8_t flag, bool value)
{
    if (value) {
        *sr = (uint8_t)(*sr | flag);
    } else {
        *sr = (uint8_t)(*sr & (uint8_t)~flag);
    }
}

static void alu_adc(DebugContext *ctx, uint8_t m)
{
    uint8_t a = ctx->a;
    unsigned carry_in = (ctx->sr & SR_C) ? 1 : 0;
    unsigned bin = (unsigned)a + m + carry_in;
    // NMOS 6502: N/V/Z reflect the binary intermediate even in decimal mode.
    set_flag(&ctx->sr, SR_Z, (bin & 0xFF) == 0);
    if (ctx->sr & SR_D) {
        unsigned lo = (unsigned)(a & 0x0F) + (m & 0x0F) + carry_in;
        unsigned hi = (unsigned)(a & 0xF0) + (m & 0xF0);
        if (lo > 9) {
            lo += 6;
            hi += 0x10;
        }
        set_flag(&ctx->sr, SR_N, (hi & 0x80) != 0);
        set_flag(&ctx->sr, SR_V, ((a ^ (uint8_t)hi) & ~(a ^ m) & 0x80) != 0);
        if (hi > 0x90) {
            hi += 0x60;
        }
        set_flag(&ctx->sr, SR_C, hi > 0xFF);
        ctx->a = (uint8_t)((hi & 0xF0) | (lo & 0x0F));
    } else {
        set_flag(&ctx->sr, SR_N, (bin & 0x80) != 0);
        set_flag(&ctx->sr, SR_V, (~(a ^ m) & (a ^ (uint8_t)bin) & 0x80) != 0);
        set_flag(&ctx->sr, SR_C, bin > 0xFF);
        ctx->a = (uint8_t)bin;
    }
}

static void alu_sbc(DebugContext *ctx, uint8_t m)
{
    uint8_t a = ctx->a;
    unsigned borrow = (ctx->sr & SR_C) ? 0 : 1;
    unsigned bin = (unsigned)a - m - borrow;
    set_flag(&ctx->sr, SR_N, (bin & 0x80) != 0);
    set_flag(&ctx->sr, SR_Z, (bin & 0xFF) == 0);
    set_flag(&ctx->sr, SR_V, ((a ^ m) & (a ^ (uint8_t)bin) & 0x80) != 0);
    set_flag(&ctx->sr, SR_C, bin < 0x100);
    if (ctx->sr & SR_D) {
        unsigned lo = (unsigned)(a & 0x0F) - (m & 0x0F) - borrow;
        unsigned hi = (unsigned)(a & 0xF0) - (m & 0xF0);
        if (lo & 0x10) {
            lo -= 6;
            hi -= 0x10;
        }
        if (hi & 0x100) {
            hi -= 0x60;
        }
        ctx->a = (uint8_t)((hi & 0xF0) | (lo & 0x0F));
    } else {
        ctx->a = (uint8_t)bin;
    }
}

static void alu_compare(uint8_t *sr, uint8_t reg, uint8_t m)
{
    uint8_t diff = (uint8_t)(reg - m);
    set_flag(sr, SR_C, reg >= m);
    set_flag(sr, SR_Z, reg == m);
    set_flag(sr, SR_N, (diff & 0x80) != 0);
}

} // namespace

// Full documented-opcode interpreter for non-control-flow instructions,
// executed while the CPU stays parked. Data accesses go through the DMA
// peek/poke path (the same coherent path every monitor read/write uses), so
// I/O reads and writes have the side effects the real instruction would have.
// Together with emulate_control_flow_step this steps any documented
// instruction without releasing the CPU, which is what makes stepping in
// fetch-lagging banks (RAM under ROM, visible ROM, and everything under the
// freezer) deterministic - and keeps parked_step_walk free of unfreeze/
// refreeze cycles, which the freezer does not survive in rapid succession.
// Undocumented opcodes return DBG_NOT_SUPPORTED (trampoline fallback).
DebugSession::Result BrkDebugSession :: interpret_simple_linear(
    const DebugContext *from, uint16_t start_pc,
    const DebugPredictResult &pred, DebugContext *out, uint8_t cpu_port,
    const uint8_t *instruction_bytes)
{
    // Context mutation is only truthful while the CPU is parked in the spin
    // loop (every parked resume rebuilds the full register file). A non-parked
    // launch keeps the live registers, so fall through to the trampoline run.
    if (!from || !from->valid || !out || !cpu_parked_in_spin ||
            pred.kind != DBG_PREDICT_LINEAR) {
        return DBG_NOT_SUPPORTED;
    }
    uint8_t bytes[3] = { 0, 0, 0 };
    if (instruction_bytes) {
        bytes[0] = instruction_bytes[0];
        bytes[1] = instruction_bytes[1];
        bytes[2] = instruction_bytes[2];
    } else if (!read_step_bytes(start_pc, bytes, pred.length)) {
        return DBG_NOT_SUPPORTED;
    }

    // Never decode an undocumented opcode as the documented instruction that
    // shares its bit pattern (e.g. $9E SHX abs,Y aliasing to "STX abs,X").
    // Callers already refuse illegal opcodes, so this is the last-line guard.
    if (disassembler_6502_is_illegal(bytes[0])) {
        return DBG_NOT_SUPPORTED;
    }

    DebugContext next = *from;
    uint8_t op = bytes[0];

    // Implied / accumulator / stack instructions first.
    switch (op) {
        case 0xEA: goto done;                                        // NOP
        case 0x18: set_flag(&next.sr, SR_C, false); goto done;       // CLC
        case 0x38: set_flag(&next.sr, SR_C, true); goto done;        // SEC
        case 0x58: set_flag(&next.sr, SR_I, false); goto done;       // CLI
        case 0x78: set_flag(&next.sr, SR_I, true); goto done;        // SEI
        case 0xB8: set_flag(&next.sr, SR_V, false); goto done;       // CLV
        case 0xD8: set_flag(&next.sr, SR_D, false); goto done;       // CLD
        case 0xF8: set_flag(&next.sr, SR_D, true); goto done;        // SED
        case 0xAA: next.x = next.a; set_nz(&next.sr, next.x); goto done; // TAX
        case 0xA8: next.y = next.a; set_nz(&next.sr, next.y); goto done; // TAY
        case 0x8A: next.a = next.x; set_nz(&next.sr, next.a); goto done; // TXA
        case 0x98: next.a = next.y; set_nz(&next.sr, next.a); goto done; // TYA
        case 0xBA: next.x = next.sp; set_nz(&next.sr, next.x); goto done; // TSX
        case 0x9A: next.sp = next.x; goto done;                      // TXS
        case 0xE8: next.x = (uint8_t)(next.x + 1); set_nz(&next.sr, next.x); goto done; // INX
        case 0xC8: next.y = (uint8_t)(next.y + 1); set_nz(&next.sr, next.y); goto done; // INY
        case 0xCA: next.x = (uint8_t)(next.x - 1); set_nz(&next.sr, next.x); goto done; // DEX
        case 0x88: next.y = (uint8_t)(next.y - 1); set_nz(&next.sr, next.y); goto done; // DEY
        case 0x0A: { // ASL A
            set_flag(&next.sr, SR_C, (next.a & 0x80) != 0);
            next.a = (uint8_t)(next.a << 1);
            set_nz(&next.sr, next.a);
            goto done;
        }
        case 0x4A: { // LSR A
            set_flag(&next.sr, SR_C, (next.a & 0x01) != 0);
            next.a = (uint8_t)(next.a >> 1);
            set_nz(&next.sr, next.a);
            goto done;
        }
        case 0x2A: { // ROL A
            uint8_t c = (uint8_t)((next.sr & SR_C) ? 1 : 0);
            set_flag(&next.sr, SR_C, (next.a & 0x80) != 0);
            next.a = (uint8_t)((next.a << 1) | c);
            set_nz(&next.sr, next.a);
            goto done;
        }
        case 0x6A: { // ROR A
            uint8_t c = (uint8_t)((next.sr & SR_C) ? 0x80 : 0);
            set_flag(&next.sr, SR_C, (next.a & 0x01) != 0);
            next.a = (uint8_t)((next.a >> 1) | c);
            set_nz(&next.sr, next.a);
            goto done;
        }
        case 0x48: // PHA
            poke_cpu((uint16_t)(0x0100 + next.sp), next.a, cpu_port);
            next.sp = (uint8_t)(next.sp - 1);
            goto done;
        case 0x08: // PHP pushes with B and U set.
            poke_cpu((uint16_t)(0x0100 + next.sp),
                     (uint8_t)(next.sr | SR_B | SR_U), cpu_port);
            next.sp = (uint8_t)(next.sp - 1);
            goto done;
        case 0x68: // PLA
            next.sp = (uint8_t)(next.sp + 1);
            next.a = peek_cpu((uint16_t)(0x0100 + next.sp), cpu_port);
            set_nz(&next.sr, next.a);
            goto done;
        case 0x28: // PLP: B is not a real flag; keep U set.
            next.sp = (uint8_t)(next.sp + 1);
            next.sr = (uint8_t)((peek_cpu((uint16_t)(0x0100 + next.sp),
                                          cpu_port) | SR_U) & (uint8_t)~SR_B);
            goto done;
        default:
            break;
    }

    {
        // Addressing-mode decode for the remaining documented ops (aaabbbcc).
        uint8_t cc = (uint8_t)(op & 0x03);
        uint8_t bbb = (uint8_t)((op >> 2) & 0x07);
        bool has_addr = false;
        bool is_imm = false;
        uint16_t addr = 0;
        uint8_t imm = 0;

        if (cc == 0x01) { // cc=01: (zp,X) zp # abs (zp),Y zp,X abs,Y abs,X
            switch (bbb) {
                case 0: { // (zp,X)
                    uint8_t zp = (uint8_t)(bytes[1] + next.x);
                    // Pointer bytes at $00/$01 are the 6510 port on a real
                    // fetch; a DMA read sees RAM under it. Trampoline instead.
                    if (zp <= 0x01 || (uint8_t)(zp + 1) <= 0x01) {
                        return DBG_NOT_SUPPORTED;
                    }
                    addr = (uint16_t)(peek_cpu(zp, cpu_port) |
                                      (peek_cpu((uint8_t)(zp + 1), cpu_port) << 8));
                    has_addr = true;
                    break;
                }
                case 1: addr = bytes[1]; has_addr = true; break;        // zp
                case 2: imm = bytes[1]; is_imm = true; break;           // #
                case 3: addr = (uint16_t)(bytes[1] | (bytes[2] << 8)); has_addr = true; break; // abs
                case 4: { // (zp),Y
                    if (bytes[1] <= 0x01 || (uint8_t)(bytes[1] + 1) <= 0x01) {
                        return DBG_NOT_SUPPORTED;
                    }
                    uint16_t base = (uint16_t)(peek_cpu(bytes[1], cpu_port) |
                        (peek_cpu((uint8_t)(bytes[1] + 1), cpu_port) << 8));
                    addr = (uint16_t)(base + next.y);
                    has_addr = true;
                    break;
                }
                case 5: addr = (uint8_t)(bytes[1] + next.x); has_addr = true; break; // zp,X
                case 6: addr = (uint16_t)((bytes[1] | (bytes[2] << 8)) + next.y); has_addr = true; break; // abs,Y
                case 7: addr = (uint16_t)((bytes[1] | (bytes[2] << 8)) + next.x); has_addr = true; break; // abs,X
                default: break;
            }
            if (!has_addr && !is_imm) {
                return DBG_NOT_SUPPORTED;
            }
            // $00/$01 data access is the 6510's internal port: a DMA
            // peek/poke cannot read the port bits or produce the banking
            // side effect a real access has. The trampoline runs it live.
            if (has_addr && addr <= 0x0001) {
                return DBG_NOT_SUPPORTED;
            }
            uint8_t aaa = (uint8_t)(op >> 5);
            if (aaa == 4) { // STA
                if (is_imm) return DBG_NOT_SUPPORTED;
                poke_cpu(addr, next.a, cpu_port);
                goto done;
            }
            uint8_t m = is_imm ? imm : peek_cpu(addr, cpu_port);
            switch (aaa) {
                case 0: next.a = (uint8_t)(next.a | m); set_nz(&next.sr, next.a); goto done; // ORA
                case 1: next.a = (uint8_t)(next.a & m); set_nz(&next.sr, next.a); goto done; // AND
                case 2: next.a = (uint8_t)(next.a ^ m); set_nz(&next.sr, next.a); goto done; // EOR
                case 3: alu_adc(&next, m); goto done;                                        // ADC
                case 5: next.a = m; set_nz(&next.sr, next.a); goto done;                     // LDA
                case 6: alu_compare(&next.sr, next.a, m); goto done;                         // CMP
                case 7: alu_sbc(&next, m); goto done;                                        // SBC
                default: return DBG_NOT_SUPPORTED;
            }
        }

        if (cc == 0x02) { // cc=10: ASL ROL LSR ROR STX LDX DEC INC (zp/abs[,X|,Y])
            uint8_t aaa = (uint8_t)(op >> 5);
            switch (bbb) {
                case 0: // # (LDX only)
                    if (aaa == 5) {
                        next.x = bytes[1];
                        set_nz(&next.sr, next.x);
                        goto done;
                    }
                    return DBG_NOT_SUPPORTED;
                case 1: addr = bytes[1]; has_addr = true; break;        // zp
                case 3: addr = (uint16_t)(bytes[1] | (bytes[2] << 8)); has_addr = true; break; // abs
                case 5: // zp,X (zp,Y for STX/LDX)
                    addr = (aaa == 4 || aaa == 5) ?
                        (uint8_t)(bytes[1] + next.y) : (uint8_t)(bytes[1] + next.x);
                    has_addr = true;
                    break;
                case 7: // abs,X (abs,Y for LDX)
                    addr = (aaa == 5) ?
                        (uint16_t)((bytes[1] | (bytes[2] << 8)) + next.y) :
                        (uint16_t)((bytes[1] | (bytes[2] << 8)) + next.x);
                    has_addr = true;
                    break;
                default:
                    return DBG_NOT_SUPPORTED;
            }
            if (!has_addr) {
                return DBG_NOT_SUPPORTED;
            }
            if (addr <= 0x0001) { // 6510 port: trampoline runs it live.
                return DBG_NOT_SUPPORTED;
            }
            if (aaa == 4) { // STX
                poke_cpu(addr, next.x, cpu_port);
                goto done;
            }
            if (aaa == 5) { // LDX
                next.x = peek_cpu(addr, cpu_port);
                set_nz(&next.sr, next.x);
                goto done;
            }
            uint8_t m = peek_cpu(addr, cpu_port);
            switch (aaa) {
                case 0: // ASL
                    set_flag(&next.sr, SR_C, (m & 0x80) != 0);
                    m = (uint8_t)(m << 1);
                    break;
                case 1: { // ROL
                    uint8_t c = (uint8_t)((next.sr & SR_C) ? 1 : 0);
                    set_flag(&next.sr, SR_C, (m & 0x80) != 0);
                    m = (uint8_t)((m << 1) | c);
                    break;
                }
                case 2: // LSR
                    set_flag(&next.sr, SR_C, (m & 0x01) != 0);
                    m = (uint8_t)(m >> 1);
                    break;
                case 3: { // ROR
                    uint8_t c = (uint8_t)((next.sr & SR_C) ? 0x80 : 0);
                    set_flag(&next.sr, SR_C, (m & 0x01) != 0);
                    m = (uint8_t)((m >> 1) | c);
                    break;
                }
                case 6: m = (uint8_t)(m - 1); break; // DEC
                case 7: m = (uint8_t)(m + 1); break; // INC
                default: return DBG_NOT_SUPPORTED;
            }
            set_nz(&next.sr, m);
            poke_cpu(addr, m, cpu_port);
            goto done;
        }

        if (cc == 0x00) { // cc=00: BIT STY LDY CPY CPX (subset of modes)
            uint8_t aaa = (uint8_t)(op >> 5);
            switch (bbb) {
                case 0: // # (LDY/CPY/CPX)
                    imm = bytes[1];
                    is_imm = true;
                    break;
                case 1: addr = bytes[1]; has_addr = true; break;        // zp
                case 3: addr = (uint16_t)(bytes[1] | (bytes[2] << 8)); has_addr = true; break; // abs
                case 5: addr = (uint8_t)(bytes[1] + next.x); has_addr = true; break; // zp,X
                case 7: addr = (uint16_t)((bytes[1] | (bytes[2] << 8)) + next.x); has_addr = true; break; // abs,X
                default:
                    return DBG_NOT_SUPPORTED;
            }
            if (has_addr && addr <= 0x0001) { // 6510 port: trampoline.
                return DBG_NOT_SUPPORTED;
            }
            switch (aaa) {
                case 1: { // BIT (zp/abs only)
                    if (!has_addr || (bbb != 1 && bbb != 3)) return DBG_NOT_SUPPORTED;
                    uint8_t m = peek_cpu(addr, cpu_port);
                    set_flag(&next.sr, SR_Z, (uint8_t)(next.a & m) == 0);
                    set_flag(&next.sr, SR_N, (m & 0x80) != 0);
                    set_flag(&next.sr, SR_V, (m & 0x40) != 0);
                    goto done;
                }
                case 4: // STY (zp, abs, zp,X)
                    if (!has_addr || bbb == 7) return DBG_NOT_SUPPORTED;
                    poke_cpu(addr, next.y, cpu_port);
                    goto done;
                case 5: // LDY
                    next.y = is_imm ? imm : peek_cpu(addr, cpu_port);
                    set_nz(&next.sr, next.y);
                    goto done;
                case 6: // CPY (#, zp, abs)
                    if (has_addr && (bbb == 5 || bbb == 7)) return DBG_NOT_SUPPORTED;
                    alu_compare(&next.sr, next.y,
                                is_imm ? imm : peek_cpu(addr, cpu_port));
                    goto done;
                case 7: // CPX (#, zp, abs)
                    if (has_addr && (bbb == 5 || bbb == 7)) return DBG_NOT_SUPPORTED;
                    alu_compare(&next.sr, next.x,
                                is_imm ? imm : peek_cpu(addr, cpu_port));
                    goto done;
                default:
                    return DBG_NOT_SUPPORTED;
            }
        }
    }
    return DBG_NOT_SUPPORTED;

done:
    next.pc = pred.fall_through;
    *out = next;
    last_context = next;
    has_last_context = true;
    return DBG_OK;
}

bool BrkDebugSession :: step_bank_is_ram_under_rom(uint16_t addr,
                                                   uint8_t cpu_port) const
{
    return monitor_backing_store_for_cpu_port(addr, cpu_port) ==
               MONITOR_BACKING_RAM &&
           monitor_backing_store_for_cpu_port(addr, 0x07) !=
               MONITOR_BACKING_RAM;
}

bool BrkDebugSession :: step_bank_fetch_unreliable(uint16_t addr,
                                                   uint8_t cpu_port) const
{
    if (monitor_backing_store_is_visible_rom(
            monitor_backing_store_for_cpu_port(addr, cpu_port))) {
        return true;
    }
    return step_bank_is_ram_under_rom(addr, cpu_port);
}

static bool branch_taken_6502(uint8_t opcode, uint8_t sr)
{
    uint8_t flag;
    switch (opcode & 0xC0) {
        case 0x00: flag = (uint8_t)(sr & 0x80); break;  // BPL/BMI test N
        case 0x40: flag = (uint8_t)(sr & 0x40); break;  // BVC/BVS test V
        case 0x80: flag = (uint8_t)(sr & 0x01); break;  // BCC/BCS test C
        default:   flag = (uint8_t)(sr & 0x02); break;  // BNE/BEQ test Z
    }
    return (flag != 0) == ((opcode & 0x20) != 0);
}

// Complete a control-flow single step by computing its architectural effects
// while the CPU stays parked in the debug spin loop, instead of releasing the
// live CPU into a bank whose instruction fetches can lag DMA-committed bytes.
//
// Rationale: the U64 slot aperture serves fetches from a registered latch that
// is not invalidated by firmware DMA writes, so a landing BRK freshly planted
// in RAM-under-ROM (or in visible ROM during a short step window) is not
// reliably observed on the first fetches after a release. JMP/branch/JSR/RTS/
// RTI have exact architectural semantics - PC, SP, SR, and $01xx stack bytes
// only - so with a parked context the step needs no live execution at all:
// every resume path rebuilds the full register file including SP from the
// context (TXS in the spin trampoline / resume stub), and the stack bytes are
// written through the always-coherent DMA path the breakpoint engine already
// relies on in low RAM. Flags are untouched by all of these except RTI, which
// pulls SR from the stack, so no ALU emulation is involved.
//
// Applies only when the launch site or a landing address is in such a bank;
// plain-RAM steps keep running the live CPU. Returns DBG_NOT_SUPPORTED when
// not applicable so the caller falls through to the real-run path. A JSR
// stepped over (not into) must really run its callee and stays on the
// breakpoint+Go path, whose fall-through BRK is observed after a sustained
// run.
DebugSession::Result BrkDebugSession :: emulate_control_flow_step(
    const DebugContext *from, uint16_t start_pc,
    const DebugPredictResult &pred, bool prefer_jsr_target,
    DebugContext *out, uint8_t cpu_port, bool force,
    const uint8_t *insn_bytes)
{
    if (!from || !from->valid || !out || !cpu_parked_in_spin) {
        return DBG_NOT_SUPPORTED;
    }
    // Instruction and vector bytes must come through read_step_bytes: while
    // the machine is held by the freezer, the live aperture (read_patch_byte
    // -> peek_cpu) does not serve BASIC/KERNAL for ROM addresses, so a raw
    // re-read returns freezer-cart garbage. read_step_bytes prefers the
    // monitor ROM cache and stays truthful in every UI mode.
    uint8_t fetched[3];
    if (!insn_bytes) {
        if (!read_step_bytes(start_pc, fetched, 3)) {
            return DBG_NOT_SUPPORTED;
        }
        insn_bytes = fetched;
    }
    DebugContext next = *from;
    bool launch_unreliable = force ||
        step_bank_fetch_unreliable(start_pc, cpu_port);
    bool push_traced_return = false;
    bool pop_traced_return = false;
    switch (pred.kind) {
        case DBG_PREDICT_JMP_ABS: {
            if (!pred.has_target) {
                return DBG_NOT_SUPPORTED;
            }
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(pred.branch_target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            next.pc = pred.branch_target;
            break;
        }
        case DBG_PREDICT_JMP_IND: {
            uint16_t op = (uint16_t)(insn_bytes[1] | (insn_bytes[2] << 8));
            // NMOS 6502 JMP ($xxFF) wraps the vector high byte within the page.
            uint16_t op_hi = (uint16_t)((op & 0xFF00) | ((op + 1) & 0x00FF));
            uint8_t vec_lo = 0, vec_hi = 0;
            if (!read_step_bytes(op, &vec_lo, 1) ||
                    !read_step_bytes(op_hi, &vec_hi, 1)) {
                return DBG_NOT_SUPPORTED;
            }
            uint16_t target = (uint16_t)(vec_lo | (vec_hi << 8));
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            next.pc = target;
            break;
        }
        case DBG_PREDICT_BRANCH: {
            if (!pred.has_target) {
                return DBG_NOT_SUPPORTED;
            }
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(pred.fall_through, cpu_port) &&
                    !step_bank_fetch_unreliable(pred.branch_target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            next.pc = branch_taken_6502(insn_bytes[0], next.sr) ?
                pred.branch_target : pred.fall_through;
            break;
        }
        case DBG_PREDICT_JSR: {
            // Only a Step Into consumes the JSR itself; a Step Over's callee
            // really runs (free-run or parked walk).
            if (!prefer_jsr_target || !pred.has_target) {
                return DBG_NOT_SUPPORTED;
            }
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(pred.branch_target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            // JSR pushes the address of its own third byte; RTS pulls it and
            // adds one. Write the real stack bytes so a later free-running RTS
            // (or Step Out) pulls exactly what an undebugged run would see.
            uint16_t ret = (uint16_t)(start_pc + 2);
            poke_cpu((uint16_t)(0x0100 + next.sp), (uint8_t)(ret >> 8),
                     cpu_port);
            poke_cpu((uint16_t)(0x0100 + ((next.sp - 1) & 0xFF)),
                     (uint8_t)(ret & 0xFF), cpu_port);
            next.sp = (uint8_t)(next.sp - 2);
            next.pc = pred.branch_target;
            push_traced_return = true;
            break;
        }
        case DBG_PREDICT_RTS: {
            uint16_t sp1 = (uint16_t)(0x0100 + ((next.sp + 1) & 0xFF));
            uint16_t sp2 = (uint16_t)(0x0100 + ((next.sp + 2) & 0xFF));
            uint16_t ret = (uint16_t)(peek_cpu(sp1, cpu_port) |
                                      (peek_cpu(sp2, cpu_port) << 8));
            uint16_t target = (uint16_t)(ret + 1);
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            // Same active-frame guard as the real-run path: without a traced
            // Step Into frame, reject stale/forged stack targets early. A
            // forced walk executes stack truth like the real CPU (KERNAL and
            // BASIC use push-address-then-RTS dispatch, which has no JSR at
            // the caller side).
            uint16_t traced_target;
            if (!force && !peek_return_target(&traced_target)) {
                uint16_t caller = (uint16_t)(ret - 2);
                uint8_t caller_byte = 0;
                if (!read_step_bytes(caller, &caller_byte, 1) ||
                        caller_byte != 0x20) {
                    return DBG_NOT_IN_SUBROUTINE;
                }
            }
            next.sp = (uint8_t)(next.sp + 2);
            next.pc = target;
            pop_traced_return = true;
            break;
        }
        case DBG_PREDICT_RTI: {
            uint16_t sp1 = (uint16_t)(0x0100 + ((next.sp + 1) & 0xFF));
            uint16_t sp2 = (uint16_t)(0x0100 + ((next.sp + 2) & 0xFF));
            uint16_t sp3 = (uint16_t)(0x0100 + ((next.sp + 3) & 0xFF));
            uint16_t target = (uint16_t)(peek_cpu(sp2, cpu_port) |
                                         (peek_cpu(sp3, cpu_port) << 8));
            if (!launch_unreliable &&
                    !step_bank_fetch_unreliable(target, cpu_port)) {
                return DBG_NOT_SUPPORTED;
            }
            // RTI pulls SR then PC. B does not exist in the register; keep the
            // stored SR normalized the way captured contexts are (U set).
            next.sr = (uint8_t)((peek_cpu(sp1, cpu_port) | 0x20) & ~0x10);
            next.sp = (uint8_t)(next.sp + 3);
            next.pc = target;
            pop_traced_return = true;
            break;
        }
        default:
            return DBG_NOT_SUPPORTED;
    }
    if (push_traced_return) {
        push_return_target(pred.fall_through);
    }
    if (pop_traced_return) {
        pop_return_target(next.pc);
    }
    *out = next;
    last_context = next;
    has_last_context = true;
    return DBG_OK;
}

// True when a free-run leg (Step Over callee, Step Out unwind) would have to
// launch or land in visible ROM. Those runs are unreliable on fetch-lagging
// hardware: the freezer path cannot settle the ROM fetch at all, and the
// overlay path intermittently derails on the first callee fetches. Plain-RAM
// and RAM-under-ROM free runs stay on the (validated) live path.
bool BrkDebugSession :: frozen_rom_run_unreliable(uint16_t launch_pc,
                                                  uint16_t landing_pc,
                                                  bool landing_valid,
                                                  uint8_t cpu_port)
{
    if (!visible_rom_fetch_lags() || !cpu_parked_in_spin) {
        return false;
    }
    // Only the freezer truly cannot free-run (and does not survive rapid
    // unfreeze/refreeze cycles), so only a frozen machine routes Step Over
    // of a JSR and Step Out through the parked walk. In the running UI
    // modes breakpoint+Go is the proven-reliable primitive, and walking a
    // large frame while parked would stop on the step budget mid-frame
    // (an arbitrary PC deep inside BASIC/KERNAL) instead of reaching the
    // return site.
    if (!machine_is_frozen()) {
        return false;
    }
    if (monitor_backing_store_is_visible_rom(
            monitor_backing_store_for_cpu_port(launch_pc, cpu_port))) {
        return true;
    }
    return landing_valid && monitor_backing_store_is_visible_rom(
        monitor_backing_store_for_cpu_port(landing_pc, cpu_port));
}

// Walk a parked context forward one instruction at a time until it reaches
// stop_pc in the same stack frame (stop_sp), without ever free-running the
// CPU through a fetch-lagging bank: control flow is completed architecturally
// (emulate_control_flow_step, forced) and every other instruction executes for
// real from the plain-RAM trampoline. This is the deterministic replacement
// for the free-run legs of Step Over and Step Out when they would launch or
// land in visible ROM.
//
// The walk stops early - reporting the truthful parked context, never a stale
// one, because stack bytes and trampoline side effects are already committed -
// when it reaches an enabled breakpoint, an unsteppable opcode (BRK/illegal),
// a step primitive fails, or the budget runs out. The caller surfaces the
// context wherever the walk stopped; the E2E oracles flag a wrong endpoint.
DebugSession::Result BrkDebugSession :: parked_step_walk(
    const DebugContext &start, uint16_t stop_pc, uint8_t stop_sp,
    const MonitorBreakpoints *bps, uint16_t skip_breakpoint_address,
    bool skip_breakpoint_address_valid, DebugContext *out, uint8_t cpu_port)
{
    // Interpreted steps are a couple of DMA accesses each (~ms), so this
    // covers real KERNAL/BASIC helpers - including state-dependent loops like
    // the FAC normalize - within a few seconds. A callee that legitimately
    // runs longer stops mid-way with the truthful walked context and can be
    // continued with another Over/Out/Go.
    static const int PARKED_STEP_WALK_BUDGET = 8192;
    if (!out) {
        return DBG_REFUSED;
    }
    DebugContext cur = start;
    for (int i = 0; i < PARKED_STEP_WALK_BUDGET; i++) {
        if (cur.pc == stop_pc && cur.sp == stop_sp) {
            *out = cur;
            return DBG_OK;
        }
        uint8_t port = execution_cpu_port(&cur);
        MonitorBackingStore skip_target = monitor_backing_store_for_cpu_port(
            skip_breakpoint_address, port);
        if (context_at_breakpoint(cur, bps, skip_breakpoint_address,
                                  skip_target, skip_breakpoint_address_valid,
                                  true)) {
            *out = cur;
            return DBG_OK;
        }
        // read_step_bytes, not the raw live aperture: under the freezer the
        // aperture does not serve BASIC/KERNAL for ROM addresses.
        uint8_t bytes[3];
        if (!read_step_bytes(cur.pc, bytes, 3)) {
            *out = cur;
            return (i > 0) ? DBG_OK : DBG_NOT_SUPPORTED;
        }
        DebugPredictResult pred;
        debug_predict(cur.pc, bytes, false, &pred);
        if (pred.kind == DBG_PREDICT_BRK || pred.kind == DBG_PREDICT_UNSAFE) {
            *out = cur;
            return (i > 0) ? DBG_OK : DBG_REFUSED;
        }
        DebugContext next;
        Result r;
        if (pred.kind == DBG_PREDICT_LINEAR) {
            r = interpret_simple_linear(&cur, cur.pc, pred, &next, port, bytes);
            if (r != DBG_OK) {
                r = step_linear_via_trampoline(&cur, cur.pc, pred, &next, port,
                                               bytes);
            }
        } else {
            r = emulate_control_flow_step(&cur, cur.pc, pred, true, &next,
                                          port, true, bytes);
        }
        if (r != DBG_OK || !next.valid) {
            *out = cur;
            return (i > 0) ? DBG_OK : r;
        }
        cur = next;
    }
    *out = cur;
    return DBG_OK;
}

DebugSession::Result BrkDebugSession :: step_with_predict(
    const DebugContext *from, uint16_t start_pc,
    const DebugPredictResult &pred, bool prefer_jsr_target,
    DebugContext *out, uint8_t cpu_port,
    const MonitorBreakpoints *bps,
    uint16_t skip_breakpoint_address,
    bool skip_breakpoint_address_valid,
    const uint8_t *linear_step_bytes,
    bool allow_linear_interpret)
{
    if (pred.kind == DBG_PREDICT_UNSAFE || pred.kind == DBG_PREDICT_BRK) {
        return DBG_REFUSED;
    }
    // Fetch-lagging hardware (U64): a step whose launch site or landing BRK
    // sits in visible ROM or in RAM under a ROM window must not depend on the
    // live CPU fetching a freshly-committed byte right after a release. With a
    // parked context, control-flow instructions are completed architecturally
    // without running the CPU at all (see emulate_control_flow_step). Context
    // mutation is safe ONLY while parked: every parked resume rebuilds the
    // full register file including SP. A non-parked launch (NMI redirect)
    // keeps the live registers, so a context-only simulation there would
    // desync the live SP and a later RTS would mispull (DEBUG TIMEOUT).
    if (out && visible_rom_fetch_lags()) {
        Result emulated = emulate_control_flow_step(from, start_pc, pred,
                                                    prefer_jsr_target, out,
                                                    cpu_port);
        if (emulated != DBG_NOT_SUPPORTED) {
            return emulated;
        }
    }
    // Step Over of a JSR that would free-run through visible ROM: enter the
    // callee architecturally and walk it while parked instead of releasing
    // the live CPU into the fetch-lagging aperture.
    if (pred.kind == DBG_PREDICT_JSR && !prefer_jsr_target && out &&
            from && from->valid && pred.has_target &&
            frozen_rom_run_unreliable(start_pc, pred.branch_target, true,
                                      cpu_port)) {
        DebugContext entered;
        Result r = emulate_control_flow_step(from, start_pc, pred, true,
                                             &entered, cpu_port, true);
        if (r == DBG_OK && entered.valid) {
            return parked_step_walk(entered, pred.fall_through, from->sp,
                                    bps, skip_breakpoint_address,
                                    skip_breakpoint_address_valid, out,
                                    cpu_port);
        }
    }
    // Linear instructions never change SP/PC beyond the fall-through: run them
    // from a plain-RAM trampoline copy (or interpret the simple ones) instead
    // of fetching them through a lagging launch bank.
    if (pred.kind == DBG_PREDICT_LINEAR && out &&
            visible_rom_fetch_lags() &&
            ((!debug_owner.remote &&
              monitor_backing_store_is_visible_rom(
                  monitor_backing_store_for_cpu_port(start_pc, cpu_port))) ||
             step_bank_is_ram_under_rom(start_pc, cpu_port))) {
        if (allow_linear_interpret) {
            Result interpreted = interpret_simple_linear(from, start_pc, pred, out,
                                                         cpu_port, linear_step_bytes);
            if (interpreted == DBG_OK) {
                return DBG_OK;
            }
        }
        return step_linear_via_trampoline(from, start_pc, pred, out, cpu_port,
                                          linear_step_bytes);
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
        bps, skip_breakpoint_address, skip_target, skip_breakpoint_address_valid,
        true);
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
    // A Step Out that would free-run through visible ROM (launch or return
    // site) walks the remainder of the frame while parked instead of
    // releasing the live CPU into the fetch-lagging aperture.
    if (frozen_rom_run_unreliable(from.pc, target, true, cpu_port)) {
        Result walked = parked_step_walk(from, target,
                                         (uint8_t)(from.sp + 2), bps,
                                         from.pc, true, ctx, cpu_port);
        if (walked == DBG_OK && ctx->valid && ctx->pc == target) {
            pop_return_target(target);
        }
        // An early stop (breakpoint hit, unsteppable opcode, budget) already
        // advanced the real machine state - stack bytes and data side effects
        // are committed - so the walked context is the truth and MUST be
        // adopted; reporting a failure here would leave the monitor's cached
        // context stale against the parked CPU.
        return walked;
    }
    // Run the real 6510 out to the caller (breakpoint at the return target)
    // rather than simulating the RTS: a simulated RTS only adjusts the cached
    // SP, leaving the live stack pointer wrong for the next real step.
    MonitorBackingStore from_target =
        monitor_backing_store_for_cpu_port(from.pc, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(bps, from.pc,
                                                        from_target, true, true);
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
            context_at_breakpoint(*ctx, bps, from.pc, from_target, true, true)) {
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
    reset_cancel_requested = false;
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
        if (context_at_breakpoint(step_ctx, bps, from.pc, from_target, true, true)) {
            return DBG_OK;
        }
        DebugContext out;
        return run_to(step_ctx, from.pc, 0, step_ctx.pc, &out);
    }

    MonitorBackingStore from_target =
        monitor_backing_store_for_cpu_port(from.pc, cpu_port);
    PatchInstallResult bp_patched = install_breakpoints(
        bps, from.pc, from_target, skip_current_bp, skip_current_bp);
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
        // No breakpoints remain: hand the interrupted program back to free-
        // running execution from its captured context. resume_from_parked
        // _context() rebuilds a register-restore stub that the parked spin
        // loop falls into and RTIs through, preserving the program's $0001
        // banking, and restores the soft BRK vector, the direct hard BRK
        // vector (the KERNAL-out path), and the NMI trampoline in the same
        // stopped session. This must be used instead of the NMI jump_to()
        // fallback below: that fallback vectors through the KERNAL NMI
        // handler, which is absent when the program runs with KERNAL banked
        // out ($01 != $07), so it would hang/not execute under-ROM code.
        //
        // A plain G issued after single-stepping passes no freshly captured
        // context (resume_from->valid == false), but we ARE parked in the spin
        // loop with a valid last_context. Mirror the breakpoint path below
        // (which already handles cpu_parked_in_spin && has_last_context) so the
        // banking-preserving stub is used in that case too, instead of the
        // KERNAL-dependent jump_to() fallback that silently fails to execute a
        // RAM-under-ROM program.
        const DebugContext *parked_ctx = NULL;
        DebugContext start_context;
        if (cpu_parked_in_spin && resume_from->valid) {
            parked_ctx = resume_from;
        } else if (cpu_parked_in_spin && has_last_context) {
            start_context = last_context;
            start_context.pc = resume_from->valid ? resume_from->pc : start_pc;
            parked_ctx = &start_context;
        }
        if (parked_ctx) {
            restore_patches();
            resume_context = *parked_ctx;
            has_resume_context = parked_ctx->valid;
            resume_from_parked_context(*parked_ctx);
            cpu_parked_in_spin = false;
            has_last_context = false;
            debug_context_reset(&last_context);
            return DBG_OK;
        }
        uint16_t go_pc = resume_from->valid ? resume_from->pc : start_pc;
        uninstall_handler();
        has_last_context = false;
        debug_context_reset(&last_context);
        has_resume_context = false;
        debug_context_reset(&resume_context);
        free_run_no_breakpoint(go_pc);
        return DBG_OK;
    }

    save_and_install_handler();
    // Relaunch metadata for the runaway retry.
    const DebugContext *launch_ctx = 0;
    DebugContext go_start_context;
    bool nmi_launch_valid = false;
    uint16_t nmi_launch_target = 0;
    bool nmi_launch_force_cpu_port = false;
    if (resume_from->valid && cpu_parked_in_spin) {
        launch_ctx = resume_from;
        begin_run_window();
        release_to_run(resume_from);
    } else if (cpu_parked_in_spin && has_last_context) {
        go_start_context = last_context;
        go_start_context.pc = resume_from->valid ? resume_from->pc : start_pc;
        launch_ctx = &go_start_context;
        begin_run_window();
        release_to_run(&go_start_context);
    } else if (resume_from->valid) {
        launch_ctx = resume_from;
        bool staged = run_window_refreeze_enabled && machine_is_frozen();
        nmi_redirect_to(resume_from->pc, cpu_port, false, staged);
        if (staged) {
            request_staged_nmi();
        }
        begin_run_window();
        if (staged) {
            clear_staged_nmi();
        }
    } else if (start_pc != 0) {
        // Non-parked breakpoint-continue. Provide captured registers when
        // available; otherwise high-memory monitor starts retry through the same
        // NMI redirect because there are no registers to synthesize.
        if (has_any_patch()) {
            if (has_last_context) {
                go_start_context = last_context;
                go_start_context.pc = start_pc;
                launch_ctx = &go_start_context;
            } else if (has_resume_context && resume_context.valid) {
                go_start_context = resume_context;
                go_start_context.pc = start_pc;
                launch_ctx = &go_start_context;
            }
        }
        bool staged = run_window_refreeze_enabled && machine_is_frozen();
        bool force_cpu_port = patch_requires_visible_rom(
            monitor_backing_store_for_cpu_port(start_pc, cpu_port));
        if (!launch_ctx && has_high_memory_patch()) {
            nmi_launch_valid = true;
            nmi_launch_target = start_pc;
            nmi_launch_force_cpu_port = force_cpu_port;
        }
        // Contextless visible-ROM launch (bp+Go / run-to a KERNAL/BASIC target
        // from a RAM bootstrap): the freshly DMA-committed ROM-image BRK can lag
        // the live 6510 instruction-fetch path, so the CPU races past the target
        // BRK and never traps (intermittent entry "DEBUG TIMEOUT" under load).
        // Unlike a parked/context launch, here the CPU sits at its own running PC
        // far from the target, so a sustained fetch-path settle safely clocks
        // that unrelated code to propagate the ROM image into the fetch path
        // before the NMI redirects execution toward the target. Skipped when a
        // launch context is present (the CPU is at the launch PC and a settle
        // could run it past a nearby BRK) - matching release_to_run's gating.
        // Gate on the installed BREAKPOINT bank (visible ROM), not the launch
        // PC's bank: the bootstrap that the CPU is redirected to lives in RAM, so
        // force_cpu_port (derived from start_pc) is false here even though the
        // target BRK is in a fetch-lagging ROM window.
        bool rom_entry_settle = (launch_ctx == 0) && has_visible_rom_patch();
        nmi_redirect_to(start_pc, cpu_port,
                        force_cpu_port, staged, rom_entry_settle);
        if (staged) {
            request_staged_nmi();
        }
        begin_run_window();
        if (staged) {
            clear_staged_nmi();
        }
    } else {
        begin_run_window();
        release_to_run(0);
    }

    int wait_ms = has_high_memory_patch() ?
        HIGH_MEMORY_BREAKPOINT_WAIT_MS : BREAKPOINT_WAIT_MS;
    Result waited = wait_for_sentinel(wait_ms);
    waited = relaunch_on_breakpoint_runaway(
        waited, launch_ctx, nmi_launch_valid, nmi_launch_target,
        nmi_launch_force_cpu_port, cpu_port, wait_ms);
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
    // A free-run breakpoint hit in visible ROM leaves the live fetch path holding
    // the BRK for this PC. Flag it so the next step launched from here gets the
    // one-shot sustained ROM-fetch settle (see release_to_run / the member note).
    if (captured.valid &&
            monitor_backing_store_is_visible_rom(
                monitor_backing_store_for_cpu_port(captured.pc, cpu_port))) {
        rom_bp_hit_pc_valid = true;
        rom_bp_hit_pc = captured.pc;
    }
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

    // Step once to escape self-hits or stale visible-ROM launch fetches.
    bool launch_site_lags = visible_rom_fetch_lags() && !debug_owner.remote &&
        monitor_backing_store_is_visible_rom(
            monitor_backing_store_for_cpu_port(run_pc, cpu_port));
    if (run_pc == target_pc || launch_site_lags) {
        uint8_t bytes[3];
        if (launch_site_lags) {
            // Use the disassembly byte source for the trampoline copy.
            if (!read_step_bytes(run_pc, bytes, 3)) {
                return DBG_REFUSED;
            }
        } else {
            for (int i = 0; i < 3; i++) {
                bytes[i] = read_patch_byte((uint16_t)(run_pc + i), cpu_port);
            }
        }
        DebugPredictResult pred;
        debug_predict(run_pc, bytes, false, &pred);
        DebugContext stepped;
        // Self-hit escape steps into; launch priming steps over.
        bool into = (run_pc == target_pc);
        Result skip = step_with_predict(have_context ? &from : 0, run_pc, pred, into,
                                        &stepped, cpu_port, bps, run_pc, true,
                                        launch_site_lags ? bytes : 0,
                                        !launch_site_lags);
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
        resume_context = last_context;
        has_resume_context = last_context.valid;
        resume_from_parked_context(last_context);
    } else {
        has_resume_context = false;
        debug_context_reset(&resume_context);
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
        resume_context = *ctx;
        has_resume_context = true;
        resume_from_parked_context(*ctx);
    } else if (resume_pending) {
        resume_context = last_context;
        has_resume_context = last_context.valid;
        resume_from_parked_context(last_context);
    } else {
        has_resume_context = false;
        debug_context_reset(&resume_context);
        uninstall_handler();
    }
    cpu_parked_in_spin = false;
    release_debug_ownership();
}

bool BrkDebugSession :: has_parked_context_handoff(void) const
{
    return cpu_parked_in_spin && has_last_context;
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
    // than the previous session's PC. Keep resume_context hidden from snapshot();
    // it is only a retry seed if a no-context launch with installed patches runs
    // away. Patch/handler teardown is cleanup()'s job.
    has_last_context = false;
    debug_context_reset(&last_context);
    clear_return_targets();
}
