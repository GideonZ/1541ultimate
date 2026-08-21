#ifndef MACHINE_MONITOR_H
#define MACHINE_MONITOR_H

#include <stddef.h>

#include "ui_elements.h"
#include "memory_backend.h"
#include "monitor_bookmarks.h"
#include "monitor_breakpoints.h"
#include "monitor_debug.h"
#include "monitor_debug_session.h"

enum MonitorError {
    MONITOR_OK = 0,
    MONITOR_ADDR,
    MONITOR_SYNTAX,
    MONITOR_VALUE,
    MONITOR_RANGE
};

enum MachineMonitorView {
    MONITOR_VIEW_HEX = 0,
    MONITOR_VIEW_ASM,
    MONITOR_VIEW_ASCII,
    MONITOR_VIEW_SCREEN,
    MONITOR_VIEW_BINARY
};

enum DebugStepOp { DEBUG_OP_OVER, DEBUG_OP_TRACE, DEBUG_OP_OUT, DEBUG_OP_GO, DEBUG_OP_CURSOR };
enum DebugStepSource { DEBUG_SRC_RAM, DEBUG_SRC_VISIBLE_ROM, DEBUG_SRC_RAM_UNDER_ROM, DEBUG_SRC_IO };
enum DebugStepPlan { DEBUG_PLAN_DIRECT, DEBUG_PLAN_STOP };
struct DebugStepDecision { DebugStepPlan plan; const char *alert; const char *reason; };
DebugStepDecision debug_classify_step(DebugStepOp op, DebugStepSource src,
                                      bool ui_freeze, bool have_parked_context,
                                      bool over_runs_callee = false);

enum MonitorScreenCharset {
    MONITOR_SCREEN_CHARSET_UPPER_GRAPHICS = 0,
    MONITOR_SCREEN_CHARSET_LOWER_UPPER = 1
};

enum {
    MONITOR_MEMORY_MIN_BYTES_PER_ROW = 8,
    MONITOR_MEMORY_MAX_BYTES_PER_ROW = 16,
    MONITOR_BINARY_MIN_BYTES_PER_ROW = 1,
    MONITOR_BINARY_MAX_BYTES_PER_ROW = 4,
    MONITOR_BINARY_SPRITE_MODE_MARKER = 0xFE,
};

// Backwards-compat alias for prior internal code/tests.
#define MONITOR_VIEW_DISASM MONITOR_VIEW_ASM

enum {
    MONITOR_HEX_BYTES_PER_ROW = 8,
    MONITOR_TEXT_BYTES_PER_ROW = 32,
    MONITOR_MEMORY_ROW_8_CHARS = 37,
    MONITOR_MEMORY_ROW_16_CHARS = 38,
    MONITOR_HEX_ROW_CHARS = 37,
    MONITOR_TEXT_ROW_CHARS = 4 + 1 + MONITOR_TEXT_BYTES_PER_ROW,
    MONITOR_DISASM_ROW_CHARS = 38,
    MONITOR_DISASM_SOURCE_COL = 30,
    MONITOR_DISASM_TEXT_COL = 15,
    // How many bytes one Assembly DATA row shows. Two rather than three: it
    // divides the $D000-$DFFF region exactly, so the region has no short row
    // at its end and every row holds the same number of editable bytes.
    MONITOR_DATA_ROW_BYTES = 2,
    MONITOR_HUNT_NEEDLE_MAX = 80,
};

struct MachineMonitorState
{
    MachineMonitorView view;
    uint16_t current_addr;
    uint16_t base_addr;
    uint8_t disasm_offset;
    bool illegal_enabled;
    uint8_t screen_charset;
    uint8_t cpu_port;
};

struct Clipboard {
    uint8_t *data;
    size_t length;
};

struct Cursor {
    uint16_t address;
    uint8_t bit_index; // 7 = MSB, 0 = LSB
};

const char *monitor_error_text(MonitorError error);

// The name a trace line uses for a view. Exposed so the token set can be
// pinned by a test rather than only by whoever reads the log next.
const char *monitor_view_name(MachineMonitorView view);
void monitor_reset_saved_state(void);
void monitor_invalidate_saved_state(void);
void monitor_reset_saved_cpu_view(void);
void monitor_format_breakpoint_mismatch(char *out, int out_len,
                                        MonitorBackingStore target,
                                        MonitorBackingStore current);
void monitor_apply_go(MachineMonitorState *state, uint16_t address);
void monitor_format_hex_row(uint16_t address, const uint8_t *bytes, char *out);
void monitor_format_text_row(uint16_t address, const uint8_t *bytes, int count, bool screen_codes, char *out);
void monitor_format_status_line(char *out, uint8_t port01, uint8_t vic_bank);
// The same row where the monitor's view bank and the live execution bank can
// differ: "CPUn" while they agree, "CxOy" while they do not, x live and y view.
void monitor_format_status_line_banks(char *out, uint8_t view_cpu_port,
                                      uint8_t live_cpu_port, uint8_t vic_bank);

MonitorError monitor_parse_address(const char *text, uint16_t *address);
MonitorError monitor_parse_expression(const char *text, uint16_t *value);
MonitorError monitor_parse_byte_value(const char *text, uint8_t *value);
MonitorError monitor_parse_fill(const char *text, uint16_t *start, uint16_t *end, uint8_t *value);
MonitorError monitor_parse_transfer(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest);
// The same, plus the optional fourth field `,DDDD-EEEE` naming the part of the
// source that is code, in source addresses. Without it `relocate` comes back
// false and the first three fields are exactly what monitor_parse_transfer
// gives, which is what keeps the three-argument command unchanged.
MonitorError monitor_parse_transfer_relocate(const char *text, uint16_t *start, uint16_t *end,
                                             uint16_t *dest, bool *relocate,
                                             uint16_t *code_start, uint16_t *code_end);
MonitorError monitor_parse_compare(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest);
MonitorError monitor_parse_hunt(const char *text, uint16_t *start, uint16_t *end, uint8_t *needle, int *needle_len);

// Convert a printable host character to its monitor screen-code representation.
// Returns the screen code, or 0xFF for chars that have no useful mapping.
uint8_t monitor_screen_code_for_char(char c,
                                     uint8_t screen_charset = MONITOR_SCREEN_CHARSET_UPPER_GRAPHICS);

void monitor_fill_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint8_t value);
void monitor_transfer_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest);
// Copy, then move absolute operands in the code range that point into the
// copied source range. Returns how many operands were rewritten.
int monitor_transfer_memory_relocate(MemoryBackend *backend, uint16_t start, uint16_t end,
                                     uint16_t dest, uint16_t code_start, uint16_t code_end,
                                     bool illegal_enabled);
int monitor_compare_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest, char *out, int out_len);
int monitor_hunt_memory(MemoryBackend *backend, uint16_t start, uint16_t end, const uint8_t *needle, int needle_len, char *out, int out_len);
int monitor_hunt_collect(MemoryBackend *backend, uint16_t start, uint16_t end, const uint8_t *needle, int needle_len, uint16_t *out_addrs, int max_addrs);
int monitor_compare_collect(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest, uint16_t *out_addrs, int max_addrs);

MonitorError monitor_format_evaluate(const char *input, char *out, int out_len);

// LOAD/SAVE parameter parsers. Used by the monitor and exercised by host tests.
// Load template: "PRG,0000,AUTO" — field 1 is "PRG" (use embedded load address)
// or a 4-hex start address; field 2 is a 4-hex offset; field 3 is "AUTO" or a
// hex length up to 0x10000.
MonitorError monitor_parse_load_params(const char *text, bool *use_prg_addr, uint16_t *start_addr,
                                       uint16_t *offset, bool *length_auto, uint32_t *length);

// Save template: "0800-9FFF" — start-end hex range, inclusive. Range size must
// be in (0, 65536].
MonitorError monitor_parse_save_params(const char *text, uint16_t *start, uint16_t *end);

// Validate a requested load against the actual file size and the 64K limit.
// Returns the effective number of bytes to read in *effective_len.
MonitorError monitor_validate_load_size(uint32_t file_size, uint32_t offset, bool length_auto,
                                        uint32_t length, uint32_t *effective_len);

// One structured command prompt: everything about how it is presented and what
// it takes, in one place, so what a prompt shows and what it accepts cannot
// drift apart. The vocabulary of `syntax` is documented beside the matcher in
// machine_monitor.cc.
struct MonitorCommandInput {
    const char *title;      // shown above the field, and states the syntax
    const char *syntax;     // the shape `accepts` is built from
    bool (*accepts)(const char *candidate);
    // Rewrites a typed key before it is validated, where case depends on
    // position rather than on the field as a whole. NULL for most prompts.
    int (*transform)(const char *buffer, int cursor, int key);
    bool template_mode;     // pre-filled, and the first typed key replaces it
    bool uppercase;         // typed letters are normalised to upper case
};

extern const MonitorCommandInput monitor_input_jump;
extern const MonitorCommandInput monitor_input_go;
extern const MonitorCommandInput monitor_input_fill;
extern const MonitorCommandInput monitor_input_transfer;
extern const MonitorCommandInput monitor_input_compare;
extern const MonitorCommandInput monitor_input_hunt;
extern const MonitorCommandInput monitor_input_load;
extern const MonitorCommandInput monitor_input_save;

// Whether `candidate` is still on its way to something `syntax` accepts: true
// when it is already acceptable, and when further typing could still make it
// so. Lexical only; the parsers above stay authoritative for meaning.
bool monitor_syntax_accepts_prefix(const char *syntax, const char *candidate);

// The C64's top-left left-arrow key, as Keyboard_C64 delivers it. Back
// everywhere in the monitor except where it is edit data.
extern const int monitor_key_arrow_left;

// The built-in help text, NULL-terminated. One line may carry a single "%s"
// conversion, filled with the key that opens help. A line is drawn as written,
// so its own characters are what has to fit the window's width.
extern const char *const monitor_help_lines[];

class UserInterface;
class Screen;
class Keyboard;
class Window;
class MonitorBookmarks;

class MachineMonitor : public UIObject
{
    MemoryBackend *backend;
    MachineMonitorState state;
    bool last_load_use_prg;
    uint16_t last_load_start;
    uint16_t last_load_offset;
    bool last_load_length_auto;
    uint32_t last_load_length;
    uint16_t last_save_start;
    uint16_t last_save_end;
    char last_save_name[40];
    bool last_go_valid;
    uint16_t last_go_addr;
    bool go_pending;
    uint16_t go_pending_addr;
    bool go_pending_has_context;
    DebugContext go_pending_context;
    // C= plus R asks for a reset and leaves; the caller that owns the
    // machine performs it, as it does for Go.
    bool reset_pending;
    // C= plus I swaps the interface and leaves. The whole user interface has
    // to close, not just the monitor, because the swapped setting only takes
    // effect the next time the menu is opened. The caller answers MENU_HIDE
    // for this, the same answer the file browser gives for the same key.
    bool interface_swap_pending;
    uint8_t memory_bytes_per_row;
    uint8_t binary_bytes_per_row;
    Clipboard clipboard;

    Screen *screen;
    Keyboard *keyboard;
    Window *window;
    int content_height;
    int pending_hex_nibble;
    bool edit_mode;
    bool poll_mode;
    uint16_t poll_deadline;
    uint8_t poll_fraction;
    bool edit_cursor_visible;
    bool help_visible;
    bool range_mode;
    uint16_t range_anchor;
    // Instruction boundary the Assembly view disassembles from: the last
    // address it was sent to (jump/Go/bookmark/hunt/follow/return).
    // Scrolling doesn't move it, so bytes decode the same when scrolled
    // away and back. See MachineMonitor::decode_row.
    uint16_t asm_baseline;
    bool number_picker_active;
    int number_selected;
    uint16_t number_preview_value;
    uint8_t number_base_bytes[2];
    bool number_word;
    int number_edit_length;
    char number_edit_buffer[17];
    bool number_expr_active;
    bool number_expr_word;
    int number_expr_length;
    char number_expr_buffer[25];
    char number_expr_status[8];
    int number_popup_x;
    int number_popup_y;
    uint16_t number_target_addr;
    uint8_t number_target_bytes;
    bool number_target_locked;
    bool hunt_picker_active;
    int hunt_count;
    int hunt_selected;
    int hunt_top;
    uint16_t hunt_addrs[256];
    const char *hunt_picker_label;
    uint8_t asm_edit_part;
    uint8_t asm_edit_pending;
    // The data region the Assembly view last grouped a row in, so the region
    // bounds are found once per redraw rather than once per row. Mutable
    // because decode_row is const and is where the lookup happens.
    mutable uint16_t data_region_start;
    mutable uint16_t data_region_end;
    mutable bool data_region_valid;
    // Per-instruction undo trail used by DEL in ASM edit mode. Each slot
    // captures the byte we are about to overwrite so DEL can restore it.
    enum { ASM_EDIT_HISTORY_MAX = 16 };
    uint16_t asm_edit_hist_addr[ASM_EDIT_HISTORY_MAX];
    uint8_t  asm_edit_hist_byte[ASM_EDIT_HISTORY_MAX];
    uint8_t  asm_edit_hist_part[ASM_EDIT_HISTORY_MAX];
    uint8_t  asm_edit_hist_pending[ASM_EDIT_HISTORY_MAX];
    int      asm_edit_hist_len;
    uint16_t asm_edit_hist_anchor_addr;
    bool opcode_picker_active;
    char opcode_prefix[4];
    int opcode_prefix_len;
    uint8_t opcode_candidates[256];
    int opcode_candidate_count;
    int opcode_selected;
    int opcode_top;
    int opcode_drawn_rows;
    // Direct-typing operand buffer used while the picker is open. Allows
    // power users to type "LDA 1000" or "LDA #FF" or "LDA (10),Y" without
    // having to navigate the picker. Empty when not in operand-typing mode.
    char opcode_operand[16];
    int  opcode_operand_len;
    uint8_t current_vic_bank;
    uint8_t last_live_vic_bank;
    bool vic_bank_override;
    MonitorBookmarks *bookmarks;
    MonitorDebug debug;
    MonitorBreakpoints breakpoints;
    DebugSession *debug_session;
    bool debug_cursor_override;
    bool debug_entry_context_valid;
    DebugContext debug_entry_context;
    uint16_t debug_entry_addr;
    bool debug_run_window_refreeze_enabled;
    bool reset_exits_monitor;
    bool reset_exit_pending;
    bool release_host_after_exit;
    bool reopen_after_reset;
    bool reopen_on_debug_reset;
    bool restore_debug_after_reset;
    bool deferred_debug_go_pending;
    DebugContext deferred_debug_go_context;
    bool breakpoint_popup_active;
    uint8_t breakpoint_selected;
    char debug_status_text[40];
    bool debug_status_visible;
    bool bookmark_popup_active;
    uint8_t bookmark_selected;
    char bookmark_status_text[40];
    bool bookmark_status_visible;
    bool bookmark_status_emphasis;
    uint16_t bookmark_status_deadline;
    // Set with the note, cleared when it is first drawn: until then it has not
    // had its time on screen. See update_bookmark_status().
    bool bookmark_status_pending;
    struct ReturnStackEntry {
        MonitorBookmarkSlot location;
        uint16_t base_addr;
        uint8_t disasm_offset;
    };
    ReturnStackEntry return_stack[10];
    uint8_t return_stack_count;
    // Cursor bit-position within the current byte in BINARY view.
    // 7 = MSB (leftmost rendered bit), 0 = LSB. Horizontal navigation and
    // typed 0/1 edits advance by one bit.
    uint8_t binary_bit_index;
#ifdef RUNS_ON_PC
    uint8_t edit_blink_polls;
#else
    uint16_t edit_blink_ms;
#endif

    uint8_t canonical_read(uint16_t address);
    void canonical_write(uint16_t address, uint8_t value);
    void canonical_write_instruction(uint16_t address, const uint8_t *bytes, uint8_t length);
    void read_row(uint16_t address, uint8_t *dst, uint16_t len) const;
    uint8_t memory_byte_stride(void) const;
    uint8_t binary_byte_stride(void) const;
    int memory_row_chars(void) const;
    int memory_hex_column(int byte_offset) const;
    void apply_go_local(uint16_t address);
    bool number_shortcut_allowed(void) const;
    bool range_shortcut_allowed(void) const;
    bool bookmark_shortcut_allowed(void) const;
    bool bookmark_set_shortcut_allowed(void) const;
    void draw();
    void draw_header();
    void draw_status();
    void draw_help();
    void draw_bookmark_popup();
    void draw_number_picker();
    void draw_popup_overlays();
    void refresh_popup_overlay();
    void refresh_opcode_overlay();
    void draw_hex();
    void draw_ascii();
    void draw_screen_codes();
    void draw_disassembly();
    void draw_binary();
    void draw_binary_row(int y, uint16_t addr, const uint8_t *bytes, int byte_count);
    // Re-paint border + entire content. Used after a sub-dialog (file picker,
    // confirmation popup, ...) that may have stomped over our window.
    void redraw_full();
    void draw_hunt_picker();
    void hunt_picker_open(int count);
    void hunt_picker_open_labeled(int count, const char *label);
    void hunt_picker_close();
    void hunt_picker_jump();
    int hunt_picker_handle_key(int key);
    void draw_opcode_picker();
    bool opcode_picker_open(char seed);
    bool opcode_prefix_is_valid(const char *prefix);
    void opcode_picker_close();
    void opcode_picker_refilter();
    void opcode_picker_commit();
    bool opcode_picker_commit_typed();
    int opcode_picker_handle_key(int key);
    void draw_hex_row(int y, uint16_t address, const uint8_t *bytes);
    void draw_text_row(int y, uint16_t address, const uint8_t *bytes, bool screen_codes);
    void ensure_current_visible();
    void set_view(MachineMonitorView view);
    void move_current(int delta);
    void move_binary_bits(int delta);
    void page_move(int lines);
    Cursor active_cursor(void) const;
    bool range_contains(uint16_t address) const;
    bool clipboard_copy_bytes(const uint8_t *data, size_t length);
    bool clipboard_copy_current(void);
    bool clipboard_copy_range(void);
    bool clipboard_copy_byte(uint8_t value);
    bool clipboard_paste(void);
    void toggle_range_mode(void);
    void open_number_picker(void);
    void number_picker_resolve_target(void);
    void number_picker_reset_edit_buffer(void);
    void number_picker_refresh_preview_from_memory(void);
    void number_picker_update_preview_from_buffer(void);
    void number_picker_set_row(int row);
    void number_picker_place_popup(void);
    void number_picker_anchor(int *x, int *y) const;
    uint16_t number_picker_current_addr(void) const;
    uint8_t number_picker_current_bytes(void) const;
    void number_picker_commit(void);
    bool number_picker_copy_preview(void);
    void number_picker_open_expression(void);
    void number_picker_open_expression(char op);
    void number_picker_close_expression(void);
    void number_picker_expression_set_status(const char *status);
    MonitorError number_picker_evaluate_expression(uint16_t *value) const;
    int number_picker_handle_key(int key);
    // A free-form monitor prompt: no syntax restriction, but the top-left
    // left-arrow key leaves it, the same as RUN/STOP.
    bool prompt_command(const char *title, char *buffer, int max_len,
                        bool template_mode = false, bool uppercase = true);
    // A structured monitor prompt: the descriptor supplies the title, the
    // presentation, and the refusal of a character the command could never
    // accept.
    bool prompt_command(const MonitorCommandInput &input, char *buffer, int max_len);
    void toggle_help();
    bool debug_active(void) const { return debug.is_active(); }
    int debug_handle_key(int key);
    int reset_machine_and_reopen(void);
    bool debug_enter(void); void debug_leave(void); void debug_sync_cursor_to_context(void);
    bool debug_handle_terminal_result(DebugSession::Result result);
    void debug_request_over(void); void debug_request_trace(void); void debug_request_out(void);
    void debug_request_go(void); void debug_request_cursor(void);
    void debug_show_status(const char *message); void debug_clear_status(void);
    DebugStepSource debug_step_source(uint16_t pc, uint8_t cpu_port) const;
    uint8_t debug_exec_cpu_port(const DebugContext *from) const;
    bool debug_resolve_step(DebugStepOp op, uint16_t start_pc, DebugContext *from,
                            bool over_runs_callee = false);
    bool debug_has_breakpoint(void) const; bool debug_has_enabled_breakpoint(void) const;
    MonitorBackingStore breakpoint_target_for_view(uint16_t address) const;
    MonitorBackingStore breakpoint_target_for_live_cpu(uint16_t address) const;
    void show_breakpoint_mapping_note(uint16_t address, MonitorBackingStore target);
    void debug_popup_result(int result); void debug_toggle_breakpoint(void);
    void debug_open_breakpoint_popup(void); void edit_breakpoint_label(uint8_t slot);
    int debug_breakpoint_popup_handle_key(int key); void debug_close_breakpoint_popup(void);
    void debug_render_breakpoint_popup(void); void ensure_debug_pc_visible(void);
    void debug_cleanup_session(void); void restore_debug_mode_after_reset(void);
    DebugSession *ensure_debug_session(void); bool debug_capture_context(DebugContext *out);
    void clear_pending_go(void); void debug_full_restore_screen(void);
    void restore_underlying_status_row(void); void draw_debug_footer(void);
    void dismiss_bookmark_status(void);
    bool update_bookmark_status(void);
    void show_bookmark_status(uint8_t slot, const MonitorBookmarkSlot *bookmark, int kind);
    void show_navigation_status(uint8_t index, const char *kind, uint16_t address);
    void clear_bookmark_transient_state(void);
    void capture_bookmark(MonitorBookmarkSlot *bookmark) const;
    bool restore_location(const MonitorBookmarkSlot *bookmark);
    bool restore_return_location(const ReturnStackEntry *entry);
    bool restore_bookmark(uint8_t slot);
    uint8_t return_stack_push_current(void);
    bool return_stack_pop(ReturnStackEntry *entry, uint8_t *index);
    bool target_visible(uint16_t target) const;
    void follow_to_target(uint16_t target);
    bool follow_target(uint16_t *target);
    bool follow_current(void);
    bool return_current(void);
    void set_bookmark(uint8_t slot);
    void edit_bookmark_label(uint8_t slot);
    int bookmark_popup_handle_key(int key);
    int handle_key(int key);
    void handle_load_command();
    void handle_save_command();
    // Display a confirmation overlay summarizing a completed LOAD/SAVE
    // (filename, byte range, byte count) and wait for the user to dismiss it.
    void show_io_confirmation(const char *op, const char *name,
                              uint16_t start_addr, uint32_t bytes);
    // Returns true when the monitor should exit (e.g., GOTO has dispatched a
    // DMA jump and the C64 must now resume executing user code).
    bool handle_go_command();
    // Prompt to change the binary view's bytes-per-row (1..4) on the fly.
    // No-op (with informative popup) outside of BINARY view.
    void handle_width_command();
    // Apply a typed bit (0 or 1) to the byte at the cursor at binary_bit_index.
    void binary_apply_bit(uint8_t bit_value);
    void enter_edit_mode();
    void commit_pending_hex_nibble(void);
    void apply_hex_digit(uint8_t value);
    void apply_ascii_char(char value);
    void apply_screen_char(char value);
    void apply_logical_delete();
    void asm_edit_history_reset(uint16_t anchor_addr);
    void asm_edit_history_push(uint16_t addr, uint8_t prev_byte, uint8_t prev_part, uint8_t prev_pending);
    bool asm_edit_history_pop();
    int handle_reset_shortcut(void);
    int handle_interface_shortcut(void);
    void exit_edit_mode();
    void reset_edit_blink();
    bool update_edit_blink();
    uint16_t next_poll_interval_ms(void);
    void reset_poll_deadline(void);
    void decode_row(uint16_t address, uint8_t *row_bytes,
                    struct Disassembled6502 *decoded) const;
    uint8_t disasm_length(uint16_t address) const;
    bool    asm_is_branch(uint16_t address);
    uint8_t asm_edit_part_count(uint16_t address);
    bool address_is_data(uint16_t address) const;
    bool data_region_bounds(uint16_t address, uint16_t *start, uint16_t *end) const;
    uint8_t data_group_length(uint16_t address) const;
    uint8_t range_span(uint16_t address) const;
    uint16_t disasm_next_addr(uint16_t address);
    uint16_t disasm_prev_addr(uint16_t address);
    uint16_t disasm_prev_visible_addr(uint16_t address);
    int disasm_visible_row(uint16_t address) const;
    uint16_t disasm_advance_rows(uint16_t address, int rows);
    uint16_t disasm_rewind_rows(uint16_t address, int rows);
    void restore_disasm_cursor_row(int row);
    void step_disassembly(int lines);
    void page_disassembly(int lines);
    void ensure_disasm_visible();
    bool inline_edit_supported(void) const;
    uint16_t row_span(void) const;

public:
    MachineMonitor(UserInterface *ui, MemoryBackend *backend);
    void set_debug_run_window_refreeze_enabled(bool enabled);
    void set_reset_exits_monitor(bool enabled);
    bool consume_release_host_after_exit(void);
    bool has_deferred_debug_go(void) const { return deferred_debug_go_pending; }
    void dispatch_deferred_debug_go(void);
    void request_reopen_after_reset(void);
    void request_debug_reset_cancel(void);
    void invalidate_live_cpu_port_view(void);
    bool is_debug_session_active(void) const;
    bool live_cpu_port_known(void) const;
    bool debug_observed_cpu_port_held(void) const;
    void leave_debug_for_exit(void) { debug_leave(); }
    const char *debug_status_message(void) const { return debug_status_visible ? debug_status_text : ""; }
    bool consume_reopen_after_reset(void);
    void init(Screen *screen, Keyboard *keyboard);
    void deinit(void);
    int poll(int);
    bool consume_pending_go(uint16_t *address, DebugContext *context = 0,
                            bool *has_context = 0);
    // Whether C= plus R asked for a machine reset before leaving.
    bool consume_pending_reset(void);
    // Whether C= plus I swapped the interface before leaving, which means the
    // whole user interface has to close rather than just the monitor.
    bool consume_pending_interface_swap(void);
};

#endif
