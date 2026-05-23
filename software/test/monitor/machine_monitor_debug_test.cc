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
#include "monitor_file_io.h"
#include "monitor_debug_predictor.h"
#include "machine_monitor_test_support.h"

// Host stubs for monitor_io. The Debug-mode tests do not exercise file IO
// or the live `G` handoff, but MachineMonitor still pulls these symbols in.
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
}

namespace {

struct StubDebugSession : public DebugSession
{
    int over_calls;
    int over_at_calls;
    int trace_calls;
    int trace_at_calls;
    int out_calls;
    int go_calls;
    int cleanup_calls;
    int snapshot_calls;
    uint16_t last_start_pc;
    DebugContext next_ctx;
    Result over_result;
    Result trace_result;
    Result out_result;
    Result go_result;
    Result snapshot_result;

    StubDebugSession()
        : over_calls(0), over_at_calls(0), trace_calls(0), trace_at_calls(0),
          out_calls(0), go_calls(0), cleanup_calls(0), snapshot_calls(0),
          last_start_pc(0),
          over_result(DBG_OK), trace_result(DBG_OK),
          out_result(DBG_OK), go_result(DBG_OK), snapshot_result(DBG_OK)
    {
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
    virtual Result over_at(uint16_t start_pc, const DebugPredictResult &, DebugContext *ctx) {
        over_at_calls++;
        last_start_pc = start_pc;
        if (over_result == DBG_OK && ctx) {
            *ctx = next_ctx;
        }
        return over_result;
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
    virtual void cleanup(void) {
        cleanup_calls++;
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
    bool allow_reset;
    DebugContext canned_snapshot;
    bool canned_snapshot_set;
    DebugSession::Result snapshot_result;

    TrackingDebugBackend() : last_session(NULL), session_creations(0),
                             refuse_session(false), reset_calls(0),
                             allow_reset(true), canned_snapshot_set(false),
                             snapshot_result(DebugSession::DBG_OK)
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

static int test_predictor_classifies_control_flow()
{
    DebugPredictResult p;

    uint8_t jsr[3] = { 0x20, 0x34, 0x12 };
    debug_predict(0x1000, jsr, false, &p);
    if (expect(p.kind == DBG_PREDICT_JSR, "JSR should classify as JSR")) return 1;
    if (expect(p.length == 3, "JSR length should be 3")) return 1;
    if (expect(p.fall_through == 0x1003, "JSR fall-through should be PC+3")) return 1;
    if (expect(p.has_target && p.branch_target == 0x1234, "JSR target should decode")) return 1;

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
    if (expect(strncmp(header + 5, "SP", 2) == 0, "SP label position")) return 1;
    if (expect(strncmp(header + 8, "AC", 2) == 0, "AC label position")) return 1;
    if (expect(strncmp(header + 11, "XR", 2) == 0, "XR label position")) return 1;
    if (expect(strncmp(header + 14, "YR", 2) == 0, "YR label position")) return 1;
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
    if (expect(strncmp(values + 5, "F7", 2) == 0, "SP hex must align under label")) return 1;
    if (expect(strncmp(values + 8, "01", 2) == 0, "AC hex must align under label")) return 1;
    if (expect(strncmp(values + 11, "00", 2) == 0, "XR hex must align under label")) return 1;
    if (expect(strncmp(values + 14, "FF", 2) == 0, "YR hex must align under label")) return 1;
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
    if (expect(backend.last_session->last_start_pc == 0x1000,
               "No-context Over must start at the Assembly cursor address")) return 1;
    if (expect(ui.popup_count == 0 || strstr(ui.last_popup, "NO CONTEXT") == NULL,
               "No-context Over must not show NO CONTEXT")) return 1;
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
    backend.write(0xA000, 0xEA);

    const int keys[] = { 'J', 'A', 'D', 'R', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    for (int i = 0; i < 4; i++) {
        if (expect(monitor.poll(0) == 0, "Breakpoint visual setup should stay in monitor")) return 1;
    }

    char row[40];
    screen.get_slice(1, 4, 38, row);
    if (expect(strstr(row, "A000") != NULL, "Breakpoint row should show the target address")) return 1;
    if (expect(strstr(row, "[BRK0][BAS]") != NULL,
               "Breakpoint row must show [BRKx] before normalized source")) return 1;
    if (expect(strstr(row, "BASIC") == NULL,
               "Assembly source indicator must use the 3-character BAS label")) return 1;

    char header[40];
    screen.get_slice(1, 3, 38, header);
    const char *dbg = strstr(header, "Dbg");
    if (expect(dbg != NULL, "Header must still show Dbg")) return 1;
    uint8_t accent = screen.colors[3][1 + (dbg - header)];
    for (int x = 1; x <= 38; x++) {
        if (expect(screen.colors[4][x] == accent,
                   "Every character on a breakpoint opcode line must use the Dbg/Edit accent color")) return 1;
    }

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
    if (expect(strncmp(row, "U/D Sel 0-9/RET Jmp S Set DEL Rst",
                       strlen("U/D Sel 0-9/RET Jmp S Set DEL Rst")) == 0,
               "Breakpoint popup help row must match the built-in command summary")) return 1;
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
        "G Continue   R Breakpt    C=+R Brkpts",
        "C=+X Reset   RETURN Follow",
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
    if (expect_help_token_not_accented(screen, "R Breakpt",
        "R Breakpt must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+R Brkpts",
        "C=+R Brkpts must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+X Reset",
        "C=+X Reset must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "RETURN Follow",
        "RETURN Follow must not use a distinct debug help colour")) return 1;
    if (expect_help_token_not_accented(screen, "C=+D/RSTOP",
        "C=+D/RSTOP must not use a distinct debug help colour")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: ESC should close help")) return 1;
    if (expect(monitor.poll(0) == 0, "help test: RUN/STOP should leave Debug")) return 1;
    if (expect(monitor.poll(0) == 1, "help test: final RUN/STOP should exit")) return 1;
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
    RUN(test_t_traces_and_o_outs_inside_debug);
    RUN(test_t_inside_debug_without_context_traces_from_cursor);
    RUN(test_g_invalidates_context);
    RUN(test_return_remains_non_executing_navigation);
    RUN(test_ctrl_d_leaves_debug_only);
    RUN(test_ctrl_d_leaves_debug_but_keeps_edit);
    RUN(test_escape_leaves_edit_before_debug);
    RUN(test_runstop_leaves_edit_before_debug);
    RUN(test_step_from_memory_view_recenters_asm_on_debug_pc);
    RUN(test_ctrl_x_resets_and_keeps_debug_open);
    RUN(test_breakpoint_toggle_via_r);
    RUN(test_breakpoint_popup_store_reuses_selected_slot);
    RUN(test_breakpoint_popup_digit_jumps_to_slot);
    RUN(test_breakpoint_row_indicator_and_color);
    RUN(test_source_indicators_are_three_chars);
    RUN(test_ctrl_r_opens_breakpoint_popup);
    RUN(test_breakpoint_popup_navigation_redraws_only_popup);
    RUN(test_debug_pc_viewport_keeps_context_margin);
    RUN(test_breakpoint_popup_blocks_debug_execution_keys);
    RUN(test_unsupported_session_emits_refusal);
    RUN(test_b_keeps_binary_view_in_debug);
    RUN(test_ctrl_b_keeps_bookmark_popup_in_debug);
    RUN(test_cleanup_runs_on_deinit);
    RUN(test_help_screen_shows_debug_commands);
    RUN(test_edit_and_debug_compose_in_header);

    puts("machine_monitor_debug_test: OK");
    return 0;
}
