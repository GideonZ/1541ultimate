namespace {
static const char *const monitor_debug_session_refused = "DEBUG NOT SUPPORTED";
static const char *const monitor_debug_session_unsafe = "UNSAFE TARGET";
static const char *const monitor_debug_session_unsupported_opcode = "UNSUPPORTED OPCODE";
static const char *const monitor_debug_session_not_in_subroutine = "NOT IN SUBROUTINE";
static const char *const monitor_debug_session_return_not_reached = "RETURN NOT REACHED";
static const char *const monitor_debug_session_timeout = "DEBUG TIMEOUT";
static const char *const monitor_debug_session_patch = "PATCH FAILED";
static const char *const monitor_debug_blocking_breakpoint_fmt = "BRK $%04X IN ROM BLOCKS DEBUG";
static const char *const monitor_debug_blocking_breakpoint_plain = "A BRK IN ROM BLOCKS DEBUG";

static const char *monitor_debug_result_name(DebugSession::Result result)
{
    switch (result) {
        case DebugSession::DBG_OK: return "OK";
        case DebugSession::DBG_NOT_SUPPORTED: return "NOT_SUPPORTED";
        case DebugSession::DBG_REFUSED: return "UNSAFE_TARGET";
        case DebugSession::DBG_UNSUPPORTED_OPCODE: return "UNSUPPORTED_OPCODE";
        case DebugSession::DBG_NOT_IN_SUBROUTINE: return "NOT_IN_SUBROUTINE";
        case DebugSession::DBG_RETURN_NOT_REACHED: return "RETURN_NOT_REACHED";
        case DebugSession::DBG_TIMEOUT: return "TIMEOUT";
        case DebugSession::DBG_CANCELLED: return "CANCELLED";
        case DebugSession::DBG_RESET: return "RESET";
        case DebugSession::DBG_PATCH_FAILED: return "PATCH_FAILED";
        case DebugSession::DBG_BREAKPOINT_NOT_INSTALLABLE: return "BREAKPOINT_NOT_INSTALLABLE";
    }
    return "UNKNOWN";
}

static bool monitor_step_bank_is_ram_under_rom(MemoryBackend *backend,
                                               uint16_t pc, uint8_t cpu_port)
{
    if (!backend || !backend->supports_cpu_banking()) return false;
    cpu_port &= 0x07;
    if (cpu_port == 0x07) return false;
    return backend->backing_store_for_cpu_port(pc, cpu_port) == MONITOR_BACKING_RAM &&
           backend->backing_store_for_cpu_port(pc, 0x07) != MONITOR_BACKING_RAM;
}
}

static MachineMonitorState monitor_reset_reentry_state(const MachineMonitorState &source)
{
    MachineMonitorState state = source;
    if (state.view == MONITOR_VIEW_ASM) {
        state.base_addr = state.current_addr;
        state.disasm_offset = 0;
    }
    return state;
}

void MachineMonitor :: set_debug_run_window_refreeze_enabled(bool enabled)
{
    debug_run_window_refreeze_enabled = enabled;
    if (debug_session) debug_session->set_run_window_refreeze_enabled(enabled);
}

void MachineMonitor :: set_reset_exits_monitor(bool enabled) { reset_exits_monitor = enabled; }

bool MachineMonitor :: consume_release_host_after_exit(void)
{
    bool ret = release_host_after_exit;
    release_host_after_exit = false;
    return ret;
}

void MachineMonitor :: request_reopen_after_reset(void)
{
    monitor_reset_reopen_state = monitor_reset_reentry_state(state);
    monitor_reset_reopen_state_valid = true;
    monitor_reset_reopen_debug_active = debug.is_active();
    reopen_after_reset = true;
    reopen_on_debug_reset = true;
    if (debug_session) debug_session->request_reset_cancel();
}

void MachineMonitor :: request_debug_reset_cancel(void)
{
    reopen_after_reset = false;
    reopen_on_debug_reset = false;
    monitor_reset_reopen_state_valid = false;
    monitor_reset_reopen_debug_active = false;
    reset_exit_pending = reset_exits_monitor;
    if (debug_session) debug_session->request_reset_cancel();
}

void MachineMonitor :: invalidate_live_cpu_port_view(void)
{
    if (backend) backend->invalidate_live_cpu_port_cache();
}

bool MachineMonitor :: is_debug_session_active(void) const
{
    return debug_session && debug_session->is_debug_session_active();
}

bool MachineMonitor :: consume_reopen_after_reset(void)
{
    bool ret = reopen_after_reset;
    reopen_after_reset = false;
    return ret;
}

void MachineMonitor :: clear_pending_go(void)
{
    go_pending = false;
    go_pending_addr = 0;
    go_pending_has_context = false;
    debug_context_reset(&go_pending_context);
}

void MachineMonitor :: debug_full_restore_screen(void)
{
    // A freeze-mode debug step's temporary unfreeze overwrites the firmware
    // chrome (title/border rows) with the live BASIC screen; redraw it. No-op
    // in overlay mode, which has no freeze/refreeze run window.
    if (!debug_session || !debug_session->screen_render_target_invalidated()) {
        return;
    }
    if (screen) {
        restore_underlying_status_row();
    }
    redraw_full();
}

void MachineMonitor :: restore_underlying_status_row(void)
{
    if (!screen || !get_ui()) {
        return;
    }
    // The firmware's own chrome goes first: the product title on the top row
    // and the horizontal rules under it and along the bottom. The live C64
    // screen was on display while the machine ran, so those rows hold whatever
    // the C64 had there. UserInterface draws them, and nothing else repaints
    // them, so they stay blank for the rest of the session if this is skipped.
    // set_screen_title() clears the screen, which is why it runs before the
    // object below and the monitor's own redraw_full().
    get_ui()->set_screen_title();
    UIObject *root = get_ui()->get_root_object();
    if (root) {
        root->redraw();
    }
    const char *help = "F3=HELP";
    int width = screen->get_size_x();
    int height = screen->get_size_y();
    int x = width - (int)strlen(help) - 1;
    if (x < 0) {
        x = 0;
    }
    screen->move_cursor(x, height - 1);
    screen->output("\eA");
    screen->output(help);
    screen->output("\eO");
}

void MachineMonitor :: draw_debug_footer()
{
    char header_row[40];
    char value_row[40];
    int width;
    int draw_len;
    bool header_accent[MonitorDebug::FOOTER_WIDTH];
    bool value_accent[MonitorDebug::FOOTER_WIDTH];
    const DebugContext &ctx = debug.context();
    const char *flags = "NV-BDIZC";

    if (!window) {
        return;
    }
    MonitorDebug::format_footer_header(header_row, sizeof(header_row));
    MonitorDebug::format_footer_values(debug.context(), value_row, sizeof(value_row));
    memset(header_accent, 0, sizeof(header_accent));
    memset(value_accent, 0, sizeof(value_accent));
    header_accent[MonitorDebug::FOOTER_POS_PC + 0] = true;
    header_accent[MonitorDebug::FOOTER_POS_PC + 1] = true;
    header_accent[MonitorDebug::FOOTER_POS_AC + 0] = true;
    header_accent[MonitorDebug::FOOTER_POS_XR + 0] = true;
    header_accent[MonitorDebug::FOOTER_POS_YR + 0] = true;
    header_accent[MonitorDebug::FOOTER_POS_SP + 0] = true;
    if (ctx.valid) {
        for (int i = 0; i < 4; i++) {
            value_accent[MonitorDebug::FOOTER_POS_PC + i] = true;
        }
        for (int i = 0; i < 2; i++) {
            value_accent[MonitorDebug::FOOTER_POS_AC + i] = true;
            value_accent[MonitorDebug::FOOTER_POS_XR + i] = true;
            value_accent[MonitorDebug::FOOTER_POS_YR + i] = true;
            value_accent[MonitorDebug::FOOTER_POS_SP + i] = true;
        }
        for (int i = 0; i < 8; i++) {
            value_accent[MonitorDebug::FOOTER_POS_FLAGS + i] = true;
        }
        for (int i = 0; i < 8; i++) {
            if (flags[i] == '-') {
                continue;
            }
            if ((ctx.sr & (1 << (7 - i))) != 0) {
                header_accent[MonitorDebug::FOOTER_POS_FLAGS + i] = true;
            }
        }
    }
    width = window->get_size_x();
    draw_len = width;
    if (draw_len > MonitorDebug::FOOTER_WIDTH) {
        draw_len = MonitorDebug::FOOTER_WIDTH;
    }
    draw_with_style_mask(window, window->get_size_y() - 3, header_row, draw_len,
                         0, header_accent,
                         get_ui()->color_fg, MONITOR_UI_ACCENT_COLOR);
    // The register row is written one character at a time straight into the
    // character matrix that UserInterface::copy_active_screen_matrix() hands to
    // /v1/machine:menu_screen, and that is the only way a REST client can read
    // the debugger's registers. Without this section the drawing task can be
    // preempted part way along the row, and the reader then returns a row that
    // holds the new PC next to the previous SP. copy_active_screen_matrix()
    // takes the same critical section, so the row it copies is either entirely
    // the previous step's or entirely this step's.
    ENTER_SAFE_SECTION
    draw_with_style_mask(window, window->get_size_y() - 2, value_row, draw_len,
                         0, value_accent,
                         get_ui()->color_fg, MONITOR_UI_ACCENT_COLOR);
    LEAVE_SAFE_SECTION
    window->set_color(get_ui()->color_fg);
}

bool MachineMonitor :: debug_enter()
{
    if (debug.is_active()) {
        return true;
    }
    monitor_log_address("debug enter", state.current_addr);
    help_visible = false;
    debug_entry_addr = state.current_addr;
    DebugSession *session = ensure_debug_session();
    if (session && !session->claim_debug_ownership(screen && screen->prefers_full_refresh())) {
        get_ui()->popup("DEBUG IN USE", BUTTON_OK);
        redraw_full();
        return false;
    }
    debug.invalidate_context();
    debug.enter();
    if (restore_debug_after_reset) {
        debug_cursor_override = true;
    }
    // Debug mode reserves two rows above the normal status footer for the CPU
    // table. Recompute so all view drawers shrink uniformly.
    if (window) {
        content_height = window->get_size_y() - 4;
        if (content_height < 1) content_height = 1;
    }
    // Best-effort capture: ask the backend for an immediate snapshot if it
    // knows how. Keep an explicitly selected cursor address authoritative:
    // the first execution command should run where the user navigated, not
    // silently switch to an unrelated live PC snapshot.
    DebugContext captured;
    debug_entry_context_valid = false;
    debug_context_reset(&debug_entry_context);
    if (debug_cursor_override) {
        debug.invalidate_context();
    }
    bool skip_capture = debug_cursor_override && backend &&
        disassembler_6502_is_illegal(backend->read(state.current_addr));
    if (!restore_debug_after_reset && !skip_capture && debug_capture_context(&captured)) {
        debug_entry_context = captured;
        debug_entry_context_valid = captured.valid;
        if (!debug_cursor_override) {
            debug.set_context(captured);
            debug_sync_cursor_to_context();
        }
    }
    return true;
}

void MachineMonitor :: debug_leave()
{
    if (!debug.is_active()) {
        return;
    }
    monitor_log("debug leave");
    clear_pending_go();
    debug_clear_status();
    debug.leave();
    if (backend) {
        // Leaving Debug returns the CPU to the interrupted program, which may
        // rewrite $01 before the monitor is looked at again. Dropping the
        // captured port makes the status row fall back to what is still
        // measurable, and lets the next freeze take a fresh reading.
        backend->invalidate_live_cpu_port_cache();
    }
    if (restore_debug_after_reset) {
        debug.invalidate_context();
        debug_entry_context_valid = false;
        debug_context_reset(&debug_entry_context);
    }
    if (!restore_debug_after_reset) {
        debug_cursor_override = false;
    }
    breakpoint_popup_active = false;
    if (window) {
        content_height = window->get_size_y() - 2;
        if (content_height < 1) content_height = 1;
    }
    // Restore any patched BRKs / vectors but keep the session object alive
    // for the rest of the monitor lifetime. The session destructor in
    // deinit() takes care of final teardown.
    if (debug_session) {
        DebugContext cleanup_context;
        const DebugContext *cleanup_target = NULL;
        debug_context_reset(&cleanup_context);
        if (debug.has_context()) {
            cleanup_context = debug.context();
            cleanup_target = &cleanup_context;
        } else if (debug_entry_context_valid) {
            cleanup_target = &debug_entry_context;
        }
        debug_session->cleanup_to_context(cleanup_target);
        debug_session->release_debug_ownership();
        debug_full_restore_screen();
        // Forget the cached PC so re-entering Debug starts from the monitor
        // cursor (where the user navigated) instead of resuming the previous
        // session's program counter.
        debug_session->forget_context();
    }
    // Drop the displayed register context too; re-entry recaptures only if the
    // backend can snapshot afresh, otherwise the cursor position drives the
    // first step.
    debug.invalidate_context();
    debug_entry_context_valid = false;
    debug_context_reset(&debug_entry_context);
}

void MachineMonitor :: debug_sync_cursor_to_context(void)
{
    uint16_t old_addr;
    uint16_t pc;

    if (!debug.is_active() || !debug.has_context()) {
        return;
    }
    if (backend && backend->supports_cpu_banking() &&
            debug.context().live_cpu_port_valid) {
        uint8_t cpu_bank = (uint8_t)(debug.context().live_cpu_port & 0x07);
        if ((uint8_t)(state.cpu_port & 0x07) != cpu_bank) {
            state.cpu_port = cpu_bank;
            backend->set_monitor_cpu_port(cpu_bank);
        }
    }
    old_addr = state.current_addr;
    pc = debug.context().pc;
    if (state.view == MONITOR_VIEW_ASM) {
        if (debug_entry_context_valid && pc == disasm_next_addr(debug_entry_context.pc)) {
            int entry_row = disasm_visible_row(debug_entry_context.pc);
            state.current_addr = pc;
            if (entry_row >= 0 && content_height > 0) {
                entry_row++;
                if (entry_row >= content_height) {
                    entry_row = content_height - 1;
                }
                state.disasm_offset = (uint8_t)entry_row;
            }
            ensure_disasm_visible();
            return;
        }
        if (pc == disasm_next_addr(old_addr)) {
            state.current_addr = pc;
            if (content_height > 0 && state.disasm_offset < (uint8_t)(content_height - 1)) {
                state.disasm_offset++;
            }
            ensure_disasm_visible();
            return;
        }
        if (pc == disasm_prev_addr(old_addr)) {
            state.current_addr = pc;
            if (state.disasm_offset > 0) {
                state.disasm_offset--;
            }
            ensure_disasm_visible();
            return;
        }
        state.current_addr = pc;
        ensure_debug_pc_visible();
    } else {
        state.current_addr = pc;
        ensure_current_visible();
    }
}

void MachineMonitor :: restore_debug_mode_after_reset(void)
{
    if (!restore_debug_after_reset || debug.is_active()) {
        return;
    }
    monitor_log_address("debug restore-after-reset", state.current_addr);
    help_visible = false;
    debug_entry_addr = state.current_addr;
    DebugSession *session = ensure_debug_session();
    if (session && !session->claim_debug_ownership(screen && screen->prefers_full_refresh())) {
        restore_debug_after_reset = false;
        get_ui()->popup("DEBUG IN USE", BUTTON_OK);
        redraw_full();
        return;
    }
    debug.enter();
    debug.invalidate_context();
    debug_cursor_override = true;
    if (window) {
        content_height = window->get_size_y() - 4;
        if (content_height < 1) content_height = 1;
    }
}

DebugSession *MachineMonitor :: ensure_debug_session()
{
    if (debug_session) {
        return debug_session;
    }
    if (!backend) {
        return NULL;
    }
    debug_session = backend->create_debug_session();
    if (debug_session) {
        debug_session->set_cancel_keyboard(keyboard);
        debug_session->set_run_window_refreeze_enabled(debug_run_window_refreeze_enabled);
    }
    return debug_session;
}

void MachineMonitor :: debug_cleanup_session()
{
    if (debug_session) {
        debug_session->cleanup();
        debug_session->release_debug_ownership();
        delete debug_session;
        debug_session = NULL;
    }
    // Safety net beyond cleanup()'s own patch restore: catches a byte that
    // reached the ROM image any other way, so a session never hands the next
    // one a modified KERNAL. Compares before writing against a snapshot
    // retaken on legitimate ROM replacement, so a real new ROM is left alone.
    if (u64_restore_pristine_rom_image) {
        u64_restore_pristine_rom_image();
    }
}

void MachineMonitor :: dispatch_deferred_debug_go(void)
{
    if (!deferred_debug_go_pending) {
        return;
    }
    deferred_debug_go_pending = false;
    if (debug_session) {
        debug_session->cleanup_to_context(
            deferred_debug_go_context.valid ? &deferred_debug_go_context : 0);
        delete debug_session;
        debug_session = NULL;
    }
    debug_context_reset(&deferred_debug_go_context);
    // Same safety net as debug_cleanup_session(). This path is the one where it
    // matters most: the C64 is handed back and starts executing immediately, so
    // a ROM byte that escaped cleanup_to_context() would be run, not merely
    // left in place.
    if (u64_restore_pristine_rom_image) {
        u64_restore_pristine_rom_image();
    }
    if (backend) {
        // The CPU runs on from here, free to rewrite $01, so the port read at
        // the last debug stop stops describing it.
        backend->invalidate_live_cpu_port_cache();
        backend->end_session();
    }
}

bool MachineMonitor :: debug_capture_context(DebugContext *out)
{
    if (!out) {
        return false;
    }
    debug_context_reset(out);
    DebugSession *session = ensure_debug_session();
    if (!session) {
        return false;
    }
    DebugSession::Result r = session->snapshot(out);
    return r == DebugSession::DBG_OK;
}

bool MachineMonitor :: debug_handle_terminal_result(DebugSession::Result result)
{
    if (result != DebugSession::DBG_RESET) {
        return false;
    }
    monitor_log("debug reset-result");
    if (reopen_on_debug_reset) {
        request_reopen_after_reset();
    } else {
        reopen_after_reset = false;
        monitor_reset_reopen_state_valid = false;
        monitor_reset_reopen_debug_active = false;
    }
    clear_pending_go();
    restore_debug_after_reset = reopen_on_debug_reset && debug.is_active();
    if (restore_debug_after_reset) {
        debug_cursor_override = true;
    }
    debug.invalidate_context();
    debug_cleanup_session();
    // Direct targets (Freeze/Overlay) draw into the just-reset, now C64-owned
    // `screen`; they exit+reopen instead, retaking ownership before repaint.
    // Non-direct hosts (Telnet) keep their own screen and stay open, so they
    // still need the redraw here.
    if (screen && !reset_exits_monitor) {
        redraw_full();
    }
    reset_exit_pending = reopen_on_debug_reset ? reset_exits_monitor : true;
    reopen_on_debug_reset = true;
    return true;
}


namespace {
static void monitor_log_debug(const char *action, uint16_t pc, DebugSession::Result result)
{
    printf("MCM debug %s $%04X %s\n", action, pc, monitor_debug_result_name(result));
}

static void monitor_read_predict_bytes(DebugSession *session, MemoryBackend *backend,
                                       uint16_t start_pc, uint8_t *bytes)
{
    if (!bytes) {
        return;
    }
    bytes[0] = 0;
    bytes[1] = 0;
    bytes[2] = 0;
    if (session && session->read_step_bytes(start_pc, bytes, 3)) {
        return;
    }
    if (backend) {
        for (int i = 0; i < 3; i++) {
            bytes[i] = backend->read((uint16_t)(start_pc + i));
        }
    }
}

const char *monitor_debug_result_message(int result)
{
    switch (result) {
        case DebugSession::DBG_NOT_SUPPORTED: return monitor_debug_session_refused;
        case DebugSession::DBG_REFUSED:       return monitor_debug_session_unsafe;
        case DebugSession::DBG_UNSUPPORTED_OPCODE: return monitor_debug_session_unsupported_opcode;
        case DebugSession::DBG_NOT_IN_SUBROUTINE: return monitor_debug_session_not_in_subroutine;
        case DebugSession::DBG_RETURN_NOT_REACHED: return monitor_debug_session_return_not_reached;
        case DebugSession::DBG_TIMEOUT:       return monitor_debug_session_timeout;
        case DebugSession::DBG_CANCELLED:     return "DEBUG CANCELLED";
        case DebugSession::DBG_RESET:         return NULL;
        case DebugSession::DBG_PATCH_FAILED:  return monitor_debug_session_patch;
        // DBG_BREAKPOINT_NOT_INSTALLABLE is absent: its message carries the
        // blocking address and is built in debug_popup_result(), which can
        // reach the session.
        default: return NULL;
    }
}
}

DebugStepDecision debug_classify_step(DebugStepOp op, DebugStepSource src,
                                      bool ui_freeze, bool have_parked_context,
                                      bool over_runs_callee)
{
    DebugStepDecision d;
    d.plan = DEBUG_PLAN_DIRECT;
    d.alert = 0;
    d.reason = "direct_step";

    // RAM-under-ROM/visible-ROM fetches can serve a stale byte right after a
    // release, which a contextless step's immediate BRK races. A parked
    // context avoids it (control flow completes while parked); otherwise only
    // JSR Step Over is safe, its breakpoint fetching after the callee runs.
    bool unsafe_src = (src == DEBUG_SRC_RAM_UNDER_ROM) ||
                      (src == DEBUG_SRC_VISIBLE_ROM);
    (void)ui_freeze;
    if (unsafe_src && !have_parked_context) {
        if (op == DEBUG_OP_TRACE) {
            d.plan = DEBUG_PLAN_STOP;
            d.alert = "Step Into: run to a breakpoint 1st";
            d.reason = "unsafe_step_into_no_context";
            return d;
        }
        // A non-JSR Step Over is the same immediate fall-through BRK as Step
        // Into, so it is gated in visible ROM (a racy ROM-image patch fetch)
        // but not RAM-under-ROM, whose fall-through patches release through
        // the raster-synced clean (RUR) session instead.
        if (op == DEBUG_OP_OVER && !over_runs_callee &&
                src == DEBUG_SRC_VISIBLE_ROM) {
            d.plan = DEBUG_PLAN_STOP;
            d.alert = "Step Over: run to a breakpoint 1st";
            d.reason = "unsafe_step_over_no_context";
            return d;
        }
    }

    return d;
}

uint8_t MachineMonitor :: debug_exec_cpu_port(const DebugContext *from) const
{
    // Mirror draw_status()'s RAM-under-ROM banking choice so the step chooser
    // gates exactly when the footer shows the matching banking: prefer the live
    // captured CPU port, and fall back to the monitor view bank (not a raw $01
    // read) when there is no valid context yet.
    if (from && from->valid && from->live_cpu_port_valid) {
        return (uint8_t)(from->live_cpu_port & 0x07);
    }
    return (uint8_t)(state.cpu_port & 0x07);
}

DebugStepSource MachineMonitor :: debug_step_source(uint16_t pc, uint8_t cpu_port) const
{
    if (!backend) {
        return DEBUG_SRC_RAM;
    }
    if (monitor_step_bank_is_ram_under_rom(backend, pc, cpu_port)) {
        return DEBUG_SRC_RAM_UNDER_ROM;
    }
    MonitorBackingStore bs = backend->backing_store_for_cpu_port(pc, cpu_port & 0x07);
    if (monitor_backing_store_is_visible_rom(bs)) {
        return DEBUG_SRC_VISIBLE_ROM;
    }
    if (bs == MONITOR_BACKING_IO) {
        return DEBUG_SRC_IO;
    }
    return DEBUG_SRC_RAM;
}

void MachineMonitor :: debug_show_status(const char *message)
{
    if (!message) {
        debug_clear_status();
        return;
    }
    strncpy(debug_status_text, message, sizeof(debug_status_text) - 1);
    debug_status_text[sizeof(debug_status_text) - 1] = 0;
    debug_status_visible = true;
}

void MachineMonitor :: debug_clear_status(void)
{
    debug_status_visible = false;
    debug_status_text[0] = 0;
}

bool MachineMonitor :: debug_resolve_step(DebugStepOp op, uint16_t start_pc,
                                          DebugContext *from,
                                          bool over_runs_callee)
{
    uint8_t exec_port = debug_exec_cpu_port(from);
    DebugStepSource src = debug_step_source(start_pc, exec_port);
    bool ui_freeze = debug_run_window_refreeze_enabled;
    bool have_parked_context = from && from->valid && debug_session &&
        debug_session->has_parked_context();
    DebugStepDecision dec = debug_classify_step(op, src, ui_freeze,
                                                have_parked_context,
                                                over_runs_callee);
    const char *op_name = "step";
    switch (op) {
        case DEBUG_OP_OVER:   op_name = "over"; break;
        case DEBUG_OP_TRACE:  op_name = "trace"; break;
        case DEBUG_OP_OUT:    op_name = "out"; break;
        case DEBUG_OP_GO:     op_name = "go"; break;
        case DEBUG_OP_CURSOR: op_name = "cursor"; break;
    }

    if (dec.plan == DEBUG_PLAN_STOP) {
        if (dec.alert) {
            debug_show_status(dec.alert);
        }
        printf("MCM debug %s $%04X stop reason=%s\n", op_name, start_pc,
               dec.reason ? dec.reason : "");
        return false;
    }
    return true;
}

void MachineMonitor :: debug_request_over()
{
    debug_clear_status();
    DebugContext from = debug.context();
    bool have_context = from.valid;
    uint16_t start_pc = have_context ? from.pc : state.current_addr;
    if (!have_context && debug_cursor_override && backend &&
            disassembler_6502_is_illegal(backend->read(start_pc))) {
        monitor_log_debug("over", start_pc, DebugSession::DBG_UNSUPPORTED_OPCODE);
        debug.invalidate_context();
        get_ui()->popup(monitor_debug_session_unsupported_opcode, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugSession *session = ensure_debug_session();
    if (!session) {
        monitor_log_debug("over", state.current_addr, DebugSession::DBG_NOT_SUPPORTED);
        get_ui()->popup(monitor_debug_session_refused, BUTTON_OK);
        redraw_full();
        return;
    }
    // Decode the current instruction so the session knows what to patch.
    uint8_t bytes[3] = { 0, 0, 0 };
    monitor_read_predict_bytes(session, backend, start_pc, bytes);
    if (disassembler_6502_is_illegal(bytes[0])) {
        monitor_log_debug("over", start_pc, DebugSession::DBG_UNSUPPORTED_OPCODE);
        debug.invalidate_context();
        debug_cleanup_session();
        get_ui()->popup(monitor_debug_session_unsupported_opcode, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugPredictResult pred;
    debug_predict(start_pc, bytes, state.illegal_enabled, &pred);
    if (pred.kind == DBG_PREDICT_UNSAFE || pred.kind == DBG_PREDICT_BRK) {
        monitor_log_debug("over", start_pc, DebugSession::DBG_REFUSED);
        get_ui()->popup(monitor_debug_session_unsafe, BUTTON_OK);
        redraw_full();
        return;
    }
    if (!debug_resolve_step(DEBUG_OP_OVER, start_pc, &from,
                            pred.kind == DBG_PREDICT_JSR)) {
        draw();
        return;
    }
    DebugContext next;
    DebugSession::Result r = have_context ? session->over(from, pred, &breakpoints, &next)
                                          : session->over_at(start_pc, pred, &breakpoints, &next);
    monitor_log_debug("over", start_pc, r);
    debug_full_restore_screen();
    if (r == DebugSession::DBG_OK) {
        if (state.view == MONITOR_VIEW_ASM) {
            state.current_addr = start_pc;
        }
        debug.set_context(next);
        debug_sync_cursor_to_context();
    } else {
        if (debug_handle_terminal_result(r)) {
            return;
        }
        debug_popup_result(r);
    }
}

void MachineMonitor :: debug_request_trace()
{
    debug_clear_status();
    DebugContext from = debug.context();
    bool have_context = from.valid;
    uint16_t start_pc = have_context ? from.pc : state.current_addr;
    if (!have_context && debug_cursor_override && backend &&
            disassembler_6502_is_illegal(backend->read(start_pc))) {
        monitor_log_debug("trace", start_pc, DebugSession::DBG_UNSUPPORTED_OPCODE);
        debug.invalidate_context();
        get_ui()->popup(monitor_debug_session_unsupported_opcode, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugSession *session = ensure_debug_session();
    if (!session) {
        monitor_log_debug("trace", state.current_addr, DebugSession::DBG_NOT_SUPPORTED);
        get_ui()->popup(monitor_debug_session_refused, BUTTON_OK);
        redraw_full();
        return;
    }
    uint8_t bytes[3] = { 0, 0, 0 };
    monitor_read_predict_bytes(session, backend, start_pc, bytes);
    if (disassembler_6502_is_illegal(bytes[0])) {
        monitor_log_debug("trace", start_pc, DebugSession::DBG_UNSUPPORTED_OPCODE);
        debug.invalidate_context();
        debug_cleanup_session();
        get_ui()->popup(monitor_debug_session_unsupported_opcode, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugPredictResult pred;
    debug_predict(start_pc, bytes, state.illegal_enabled, &pred);
    if (pred.kind == DBG_PREDICT_UNSAFE || pred.kind == DBG_PREDICT_BRK) {
        monitor_log_debug("trace", start_pc, DebugSession::DBG_REFUSED);
        get_ui()->popup(monitor_debug_session_unsafe, BUTTON_OK);
        redraw_full();
        return;
    }
    if (!debug_resolve_step(DEBUG_OP_TRACE, start_pc, &from)) {
        draw();
        return;
    }
    DebugContext next;
    DebugSession::Result r = have_context ? session->trace(from, pred, &next)
                                          : session->trace_at(start_pc, pred, &next);
    monitor_log_debug("trace", start_pc, r);
    debug_full_restore_screen();
    if (r == DebugSession::DBG_OK) {
        if (state.view == MONITOR_VIEW_ASM) {
            state.current_addr = start_pc;
        }
        debug.set_context(next);
        debug_sync_cursor_to_context();
    } else {
        if (debug_handle_terminal_result(r)) {
            return;
        }
        debug_popup_result(r);
    }
}

void MachineMonitor :: debug_request_out()
{
    debug_clear_status();
    DebugSession *session = ensure_debug_session();
    if (!session) {
        monitor_log_debug("out", state.current_addr, DebugSession::DBG_NOT_SUPPORTED);
        get_ui()->popup(monitor_debug_session_refused, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugContext from = debug.context();
    if (!from.valid) {
        monitor_log_debug("out", state.current_addr, DebugSession::DBG_NOT_IN_SUBROUTINE);
        get_ui()->popup(monitor_debug_session_not_in_subroutine, BUTTON_OK);
        redraw_full();
        return;
    }
    DebugContext next;
    DebugSession::Result r = session->step_out(from, &breakpoints, &next);
    monitor_log_debug("out", from.pc, r);
    debug_full_restore_screen();
    if (r == DebugSession::DBG_OK) {
        if (state.view == MONITOR_VIEW_ASM) {
            state.current_addr = from.pc;
        }
        debug.set_context(next);
        debug_sync_cursor_to_context();
    } else {
        if (debug_handle_terminal_result(r)) {
            return;
        }
        debug_popup_result(r);
    }
}

void MachineMonitor :: debug_request_go()
{
    debug_clear_status();
    DebugContext from = debug.context();
    if (debug_cursor_override) {
        debug_context_reset(&from);
    }
    if (!from.valid && !debug_cursor_override) {
        DebugContext snap;
        if (debug_capture_context(&snap)) {
            debug.set_context(snap);
            from = snap;
        }
    }
    DebugSession *session = ensure_debug_session();
    if (!session) {
        // Backend has no native debug session - fall back to the existing
        // non-debug `G` semantics (release/jump) so users with a non-stepping
        // backend can still resume from the captured / cursor address.
        last_go_addr = from.valid ? from.pc : state.current_addr;
        last_go_valid = true;
        go_pending = true;
        go_pending_addr = last_go_addr;
        go_pending_has_context = false;
        debug.invalidate_context();
        monitor_log_address("debug go fallback", last_go_addr);
        return;
    }
    // Start PC for a fresh Go is the Assembly view cursor: this is what the
    // user is looking at and where they expect execution to begin. If a
    // captured context is already in hand, the session prefers `from.pc`
    // and ignores this fallback.
    uint16_t start_pc = state.current_addr;
    if (debug_run_window_refreeze_enabled &&
            !debug_has_enabled_breakpoint() &&
            session->has_parked_context_handoff()) {
        // Freeze-mode parked-context G must let the monitor fully tear down
        // and release ownership before the BRK session hands execution back to
        // the live machine. Defer that handoff to run_machine_monitor(), which
        // calls cleanup_to_context() after deinit() and ownership release.
        last_go_addr = from.valid ? from.pc : start_pc;
        last_go_valid = true;
        deferred_debug_go_pending = true;
        if (from.valid) {
            deferred_debug_go_context = from;
        } else {
            debug_context_reset(&deferred_debug_go_context);
        }
        release_host_after_exit = true;
        clear_pending_go();
        debug.invalidate_context();
        monitor_log_address("debug go deferred", last_go_addr);
        return;
    }
    if ((debug_run_window_refreeze_enabled || reset_exits_monitor) &&
            !debug_has_enabled_breakpoint()) {
        // Direct UI no-breakpoint Go uses the debug session's own parked-spin
        // release path, then unwinds through run_machine_monitor() so Freeze
        // releases freezer ownership and Overlay disables the render owner
        // before execution continues at full speed.
        last_go_addr = from.valid ? from.pc : start_pc;
        last_go_valid = true;
        DebugSession::Result result = session->go(from, &breakpoints, start_pc);
        if (debug_handle_terminal_result(result)) {
            return;
        }
        if (result != DebugSession::DBG_OK) {
            printf("MCM debug go $%04X %s\n", last_go_addr,
                   monitor_debug_result_name(result));
            return;
        }
        release_host_after_exit = true;
        clear_pending_go();
        debug.invalidate_context();
        monitor_log_address("debug go handoff", last_go_addr);
        return;
    }
    // Invalidate up-front; G discards the captured context per the spec.
    debug.invalidate_context();
    DebugSession::Result r = session->go(from, &breakpoints, start_pc);
    monitor_log_debug("go", start_pc, r);
    debug_full_restore_screen();
    if (r == DebugSession::DBG_OK) {
        // If the session captured a fresh context (BRK fired during this
        // Go), `snapshot` now returns it. Pull it back so the footer shows
        // the real post-stop state on the next draw.
        DebugContext captured;
        if (session->snapshot(&captured) == DebugSession::DBG_OK &&
            captured.valid) {
            debug.set_context(captured);
            debug_cursor_override = false;
            debug_sync_cursor_to_context();
        }
    } else {
        if (debug_handle_terminal_result(r)) {
            return;
        }
        debug_popup_result(r);
    }
}

void MachineMonitor :: debug_request_cursor()
{
    debug_clear_status();
    DebugContext from = debug.context();
    if (!from.valid) {
        DebugContext snap;
        if (debug_capture_context(&snap)) {
            debug.set_context(snap);
            from = snap;
        }
    }
    DebugSession *session = ensure_debug_session();
    if (!session) {
        monitor_log_debug("cursor", state.current_addr, DebugSession::DBG_NOT_SUPPORTED);
        get_ui()->popup(monitor_debug_session_refused, BUTTON_OK);
        redraw_full();
        return;
    }
    uint16_t start_pc = from.valid ? from.pc : debug_entry_addr;
    DebugContext next;
    DebugSession::Result r = session->run_to(from, state.current_addr, &breakpoints,
                                             start_pc, &next);
    monitor_log_debug("cursor", state.current_addr, r);
    debug_full_restore_screen();
    if (r == DebugSession::DBG_OK) {
        debug.set_context(next);
        debug_sync_cursor_to_context();
    } else {
        if (debug_handle_terminal_result(r)) {
            return;
        }
        debug_popup_result(r);
    }
}

bool MachineMonitor :: debug_has_breakpoint(void) const
{
    for (int i = 0; i < breakpoints.slot_count(); i++) {
        const MonitorBreakpointSlot *bp = breakpoints.get(i);
        if (bp && bp->used) {
            return true;
        }
    }
    return false;
}

bool MachineMonitor :: debug_has_enabled_breakpoint(void) const
{
    for (int i = 0; i < breakpoints.slot_count(); i++) {
        const MonitorBreakpointSlot *bp = breakpoints.get(i);
        if (bp && bp->used && bp->enabled) {
            return true;
        }
    }
    return false;
}

MonitorBackingStore MachineMonitor :: breakpoint_target_for_view(uint16_t address) const
{
    uint8_t view_cpu_port = state.cpu_port & 0x07;

    return backend ? backend->backing_store_for_cpu_port(address, view_cpu_port) :
        monitor_backing_store_for_cpu_port(address, view_cpu_port);
}

MonitorBackingStore MachineMonitor :: breakpoint_target_for_live_cpu(uint16_t address) const
{
    uint8_t live_cpu_port = backend ? backend->get_live_cpu_port() : state.cpu_port;

    return backend ? backend->backing_store_for_cpu_port(address, live_cpu_port) :
        monitor_backing_store_for_cpu_port(address, live_cpu_port);
}

bool MachineMonitor :: live_cpu_port_known(void) const
{
    return backend && backend->live_cpu_port_known();
}

bool MachineMonitor :: debug_observed_cpu_port_held(void) const
{
    return backend && backend->has_debug_observed_cpu_port();
}

void MachineMonitor :: debug_popup_result(int result)
{
    char blocking_msg[40];
    const char *msg = monitor_debug_result_message(result);

    if (result == DebugSession::DBG_BREAKPOINT_NOT_INSTALLABLE) {
        uint16_t blocking = 0;
        if (debug_session && debug_session->blocking_breakpoint(&blocking)) {
            sprintf(blocking_msg, monitor_debug_blocking_breakpoint_fmt, blocking);
            msg = blocking_msg;
        } else {
            msg = monitor_debug_blocking_breakpoint_plain;
        }
    }
    if (msg) {
        get_ui()->popup(msg, BUTTON_OK);
        redraw_full();
    }
}

void MachineMonitor :: show_breakpoint_mapping_note(uint16_t address,
                                                    MonitorBackingStore target)
{
    MonitorBackingStore current = breakpoint_target_for_live_cpu(address);

    if (target == current) {
        return;
    }
    char msg[40];
    monitor_format_breakpoint_mismatch(msg, sizeof(msg), target, current);
    get_ui()->popup(msg, BUTTON_OK);
    redraw_full();
}

void MachineMonitor :: debug_toggle_breakpoint()
{
    uint16_t target = state.current_addr;
    MonitorBackingStore backing_store = breakpoint_target_for_view(target);
    int existing = breakpoints.find_at(target, backing_store);
    if (existing >= 0) {
        breakpoints.clear_slot(existing);
        printf("MCM breakpoint clear %d $%04X\n", existing, target);
        return;
    }
    int slot = breakpoints.allocate(target, (uint8_t)(state.cpu_port & 0x07),
                                    backing_store);
    if (slot < 0) {
        get_ui()->popup("NO FREE BRK SLOT", BUTTON_OK);
        redraw_full();
        return;
    }
    printf("MCM breakpoint set %d $%04X %s\n", slot, target,
           monitor_backing_store_tag(backing_store));
    show_breakpoint_mapping_note(target, backing_store);
}

void MachineMonitor :: debug_open_breakpoint_popup()
{
    breakpoint_popup_active = true;
    monitor_log("breakpoint popup open");
    if (breakpoint_selected >= breakpoints.slot_count()) {
        breakpoint_selected = 0;
    }
}

void MachineMonitor :: debug_close_breakpoint_popup()
{
    breakpoint_popup_active = false;
    monitor_log("breakpoint popup close");
}

void MachineMonitor :: edit_breakpoint_label(uint8_t slot)
{
    const MonitorBreakpointSlot *bp = breakpoints.get(slot);
    char buffer[MONITOR_BREAKPOINT_LABEL_STORAGE];
    char normalized[MONITOR_BREAKPOINT_LABEL_STORAGE];
    char title[16];

    if (!bp || !bp->used) {
        return;
    }
    strncpy(buffer, bp->label, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    sprintf(title, "Label BP%u", (unsigned)slot);
    if (prompt_command(title, buffer, MONITOR_BREAKPOINT_LABEL_MAX, false, true)) {
        MonitorBreakpoints::normalize_label(normalized, sizeof(normalized), buffer);
        breakpoints.set_label(slot, normalized);
        printf("MCM breakpoint label %u %s\n", (unsigned)slot, normalized);
    } else {
        printf("MCM breakpoint label %u cancelled\n", (unsigned)slot);
    }
}

int MachineMonitor :: debug_breakpoint_popup_handle_key(int key)
{
    if (key == KEY_ESCAPE || key == KEY_BREAK || key == KEY_CTRL_P) {
        debug_close_breakpoint_popup();
        redraw_full();
        return 0;
    }
    if (key == KEY_CTRL_O) {
        debug_close_breakpoint_popup();
        return 1;
    }
    if (key == KEY_UP) {
        if (breakpoint_selected > 0) breakpoint_selected--;
        refresh_breakpoint_popup_overlay();
        return 0;
    }
    if (key == KEY_DOWN) {
        if (breakpoint_selected + 1 < (uint8_t)breakpoints.slot_count()) breakpoint_selected++;
        refresh_breakpoint_popup_overlay();
        return 0;
    }
    if (key >= '0' && key <= '9') {
        breakpoint_selected = (uint8_t)(key - '0');
        const MonitorBreakpointSlot *bp = breakpoints.get(breakpoint_selected);
        if (bp && bp->used) {
            state.cpu_port = (uint8_t)(bp->view_cpu_port & 0x07);
            if (backend && backend->supports_cpu_banking()) {
                backend->set_monitor_cpu_port(state.cpu_port);
            }
            apply_go_local(bp->address);
            printf("MCM breakpoint jump %u $%04X\n", (unsigned)breakpoint_selected, bp->address);
            debug_close_breakpoint_popup();
            redraw_full();
        } else {
            refresh_breakpoint_popup_overlay();
        }
        return 0;
    }
    if (key == KEY_RETURN) {
        const MonitorBreakpointSlot *bp = breakpoints.get(breakpoint_selected);
        if (bp && bp->used) {
            state.cpu_port = (uint8_t)(bp->view_cpu_port & 0x07);
            if (backend && backend->supports_cpu_banking()) {
                backend->set_monitor_cpu_port(state.cpu_port);
            }
            state.current_addr = bp->address;
            apply_go_local(bp->address);
            printf("MCM breakpoint jump %u $%04X\n", (unsigned)breakpoint_selected, bp->address);
        }
        debug_close_breakpoint_popup();
        redraw_full();
        return 0;
    }
    if (key == 's' || key == 'S') {
        MonitorBackingStore backing_store = breakpoint_target_for_view(state.current_addr);
        breakpoints.store_slot(breakpoint_selected, state.current_addr,
                               (uint8_t)(state.cpu_port & 0x07),
                               backing_store);
        printf("MCM breakpoint store %u $%04X %s\n",
               (unsigned)breakpoint_selected, state.current_addr,
               monitor_backing_store_tag(backing_store));
        show_breakpoint_mapping_note(state.current_addr, backing_store);
        refresh_breakpoint_popup_overlay();
        return 0;
    }
    if (key == 'l' || key == 'L') {
        // edit_breakpoint_label opens a modal prompt outside the popup box, so
        // this stays a full redraw to clean up after it.
        edit_breakpoint_label(breakpoint_selected);
        refresh_popup_overlay();
        return 0;
    }
    if (key == 'e' || key == 'E') {
        const MonitorBreakpointSlot *bp = breakpoints.get(breakpoint_selected);
        if (bp && bp->used) {
            bool new_enabled = !bp->enabled;
            breakpoints.set_enabled(breakpoint_selected, new_enabled);
            printf("MCM breakpoint %s %u $%04X\n", new_enabled ? "enable" : "disable",
                   (unsigned)breakpoint_selected, bp->address);
        }
        refresh_breakpoint_popup_overlay();
        return 0;
    }
    if (key == KEY_DELETE || key == KEY_BACK) {
        const MonitorBreakpointSlot *bp = breakpoints.get(breakpoint_selected);
        if (bp && bp->used) {
            printf("MCM breakpoint clear %u $%04X\n", (unsigned)breakpoint_selected, bp->address);
        }
        breakpoints.clear_slot(breakpoint_selected);
        refresh_breakpoint_popup_overlay();
        return 0;
    }
    return 0;
}

void MachineMonitor :: debug_render_breakpoint_popup()
{
    enum {
        BRK_POPUP_INNER_WIDTH = 38,
        BRK_POPUP_INNER_HEIGHT = 14,
        BRK_POPUP_SLOT_FIRST_ROW = 2,
        BRK_POPUP_HELP_ROW = 13
    };
    if (!window || !screen) {
        return;
    }
    char popup_lines[BRK_POPUP_INNER_HEIGHT][BRK_POPUP_INNER_WIDTH + 1];
    memset(popup_lines, 0, sizeof(popup_lines));
    strcpy(popup_lines[0], "BREAKPOINTS");
    for (int i = 0; i < breakpoints.slot_count(); i++) {
        MonitorBreakpoints::format_popup_row(popup_lines[BRK_POPUP_SLOT_FIRST_ROW + i],
                                             sizeof(popup_lines[BRK_POPUP_SLOT_FIRST_ROW + i]),
                                             i,
                                             breakpoints.get(i));
    }
    strcpy(popup_lines[BRK_POPUP_HELP_ROW],
           "0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res");
    int screen_x, screen_y;
    window->getOffsets(screen_x, screen_y);
    int popup_x = screen_x + ((window->get_size_x() - BRK_POPUP_INNER_WIDTH) / 2);
    int popup_y = screen_y + ((window->get_size_y() - BRK_POPUP_INNER_HEIGHT) / 2);
    if (popup_x + (BRK_POPUP_INNER_WIDTH + 2) > 40) {
        popup_x = 40 - (BRK_POPUP_INNER_WIDTH + 2);
    }
    if (popup_x < 0) popup_x = 0;
    if (popup_y < screen_y) popup_y = screen_y;
    Window popup(screen, popup_x, popup_y,
                 BRK_POPUP_INNER_WIDTH + 2, BRK_POPUP_INNER_HEIGHT + 2);
    popup.set_color(get_ui()->color_fg);
    popup.draw_border();
    popup.set_color(get_ui()->color_fg);
    for (int row = 0; row < BRK_POPUP_INNER_HEIGHT; row++) {
        char line[BRK_POPUP_INNER_WIDTH + 1];
        int len = (int)strlen(popup_lines[row]);
        int slot_row = row - BRK_POPUP_SLOT_FIRST_ROW;
        bool selected = (slot_row >= 0 && slot_row < breakpoints.slot_count() &&
                         slot_row == (int)breakpoint_selected);

        if (len > BRK_POPUP_INNER_WIDTH) len = BRK_POPUP_INNER_WIDTH;
        memset(line, ' ', BRK_POPUP_INNER_WIDTH);
        if (len > 0) {
            memcpy(line, popup_lines[row], (size_t)len);
        }
        line[BRK_POPUP_INNER_WIDTH] = 0;
        if (selected) {
            draw_with_highlight(&popup, row, line, BRK_POPUP_INNER_WIDTH,
                                0, BRK_POPUP_INNER_WIDTH);
        } else {
            popup.move_cursor(0, row);
            popup.output_length(line, BRK_POPUP_INNER_WIDTH);
        }
    }
}

void MachineMonitor :: ensure_debug_pc_visible(void)
{
    enum { DEBUG_PC_MARGIN = 3 };
    int rows = content_height;
    int margin = DEBUG_PC_MARGIN;

    if (state.view != MONITOR_VIEW_ASM) {
        return;
    }
    if (rows <= 1) {
        state.base_addr = state.current_addr;
        state.disasm_offset = 0;
        return;
    }
    if (margin * 2 >= rows) {
        margin = (rows - 1) / 2;
    }
    state.disasm_offset = (uint8_t)margin;
    ensure_disasm_visible();
}

int MachineMonitor :: debug_handle_key(int key)
{
    if (breakpoint_popup_active) {
        return debug_breakpoint_popup_handle_key(key);
    }
    // Exit Debug shortcuts. ESC / RUN/STOP drop Debug only once Edit has
    // already been left by the outer edit-mode handler.
    if (key == KEY_ESCAPE || key == KEY_BREAK || key == KEY_CTRL_D) {
        debug_leave();
        draw();
        return 0;
    }
    if (key == KEY_CTRL_O) {
        // Leaving the monitor. debug_leave() -> session cleanup() hands the
        // parked CPU back to the interrupted program (overlay: resumed live;
        // freeze: staged for when the freezer menu unfreezes). No deferred
        // go-handoff is scheduled, so the C64 is never left spinning.
        debug_leave();
        return 1;
    }
    // RETURN remains Assembly subroutine navigation. We handle it here only
    // to forward to the existing follow_current/return_current logic; the
    // outer dispatcher will not see this key.
    if (key == KEY_RETURN && state.view == MONITOR_VIEW_ASM && !edit_mode) {
        if (follow_current() || return_current()) {
            draw();
        }
        return 0;
    }
    if (key == KEY_CTRL_P) {
        debug_open_breakpoint_popup();
        draw();
        return 0;
    }
    if (key == 'p' || key == 'P') {
        debug_toggle_breakpoint();
        draw();
        return 0;
    }
    if (key == 'd' || key == 'D') {
        debug_request_over();
        if (reset_exit_pending) {
            return MENU_EXIT;
        }
        draw();
        return 0;
    }
    if (key == 't' || key == 'T') {
        debug_request_trace();
        if (reset_exit_pending) {
            return MENU_EXIT;
        }
        draw();
        return 0;
    }
    if (key == 'u' || key == 'U') {
        // Debug-mode Step Out. This deliberately overrides the Assembly-view
        // undocumented-opcode toggle that 'U' performs outside Debug. 'O'/'o'
        // are left to fall through to the normal dispatcher so the monitor
        // view CPU bank can still be cycled while Debug is active.
        debug_request_out();
        if (reset_exit_pending) {
            return MENU_EXIT;
        }
        draw();
        return 0;
    }
    if (key == 'g' || key == 'G') {
        debug_request_go();
        if (reset_exit_pending) {
            return MENU_EXIT;
        }
        if (go_pending || release_host_after_exit) {
            return 1;
        }
        draw();
        return 0;
    }
    if (key == 'k' || key == 'K') {
        debug_request_cursor();
        if (reset_exit_pending) {
            return MENU_EXIT;
        }
        draw();
        return 0;
    }
    // Any other key falls through to the normal monitor dispatcher so the
    // user can still navigate, switch views, edit, etc. while Debug is on.
    return -1;
}

int MachineMonitor :: reset_machine_and_reopen(void)
{
    monitor_log("reset and reopen");
    request_reopen_after_reset();
    clear_pending_go();
    help_visible = false;
    hunt_picker_active = false;
    opcode_picker_active = false;
    number_picker_active = false;
    bookmark_popup_active = false;
    breakpoint_popup_active = false;
    exit_edit_mode();
    restore_debug_after_reset = debug.is_active();
    if (restore_debug_after_reset) {
        debug_cursor_override = true;
        debug.invalidate_context();
    }
    debug_cleanup_session();
    if (!backend || !backend->supports_reset() || !backend->reset_machine()) {
        reopen_after_reset = false;
        monitor_reset_reopen_state_valid = false;
        get_ui()->popup("RESET UNAVAILABLE", BUTTON_OK);
        redraw_full();
        return 0;
    }
    if (screen && !reset_exits_monitor) {
        redraw_full();
    }
    reset_exit_pending = reset_exits_monitor;
    return reset_exits_monitor ? MENU_EXIT : 0;
}
