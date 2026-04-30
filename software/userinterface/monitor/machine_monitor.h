#ifndef MACHINE_MONITOR_H
#define MACHINE_MONITOR_H

#include <stddef.h>

#include "ui_elements.h"
#include "memory_backend.h"

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

enum {
    MONITOR_BINARY_MIN_BYTES_PER_ROW = 1,
    MONITOR_BINARY_MAX_BYTES_PER_ROW = 4,
};

// Backwards-compat alias for prior internal code/tests.
#define MONITOR_VIEW_DISASM MONITOR_VIEW_ASM

enum {
    MONITOR_HEX_BYTES_PER_ROW = 8,
    MONITOR_TEXT_BYTES_PER_ROW = 32,
    MONITOR_HEX_ROW_CHARS = 37,
    MONITOR_TEXT_ROW_CHARS = 4 + 1 + MONITOR_TEXT_BYTES_PER_ROW,
    MONITOR_DISASM_ROW_CHARS = 38,
    MONITOR_DISASM_SOURCE_COL = 30,
    MONITOR_DISASM_TEXT_COL = 15,
};

struct MachineMonitorState
{
    MachineMonitorView view;
    uint16_t current_addr;
    uint16_t base_addr;
    uint8_t disasm_offset;
    bool illegal_enabled;
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
void monitor_reset_saved_state(void);
void monitor_invalidate_saved_state(void);
void monitor_apply_go(MachineMonitorState *state, uint16_t address);
void monitor_format_hex_row(uint16_t address, const uint8_t *bytes, char *out);
void monitor_format_text_row(uint16_t address, const uint8_t *bytes, int count, bool screen_codes, char *out);
void monitor_format_status_line(char *out, uint8_t port01, uint8_t vic_bank);

MonitorError monitor_parse_address(const char *text, uint16_t *address);
MonitorError monitor_parse_expression(const char *text, uint16_t *value);
MonitorError monitor_parse_byte_value(const char *text, uint8_t *value);
MonitorError monitor_parse_fill(const char *text, uint16_t *start, uint16_t *end, uint8_t *value);
MonitorError monitor_parse_transfer(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest);
MonitorError monitor_parse_compare(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest);
MonitorError monitor_parse_hunt(const char *text, uint16_t *start, uint16_t *end, uint8_t *needle, int *needle_len);

void monitor_fill_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint8_t value);
void monitor_transfer_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest);
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

class UserInterface;
class Screen;
class Keyboard;
class Window;

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
    uint8_t binary_bytes_per_row;
    Clipboard clipboard;

    Screen *screen;
    Keyboard *keyboard;
    Window *window;
    int content_height;
    int pending_hex_nibble;
    bool edit_mode;
    bool edit_cursor_visible;
    bool help_visible;
    bool range_mode;
    uint16_t range_anchor;
    bool number_picker_active;
    int number_selected;
    uint16_t number_preview_value;
    uint8_t number_base_bytes[2];
    bool number_word;
    int number_edit_length;
    char number_edit_buffer[17];
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
    void read_row(uint16_t address, uint8_t *dst, uint16_t len) const;
    uint8_t binary_byte_stride(void) const;
    void apply_go_local(uint16_t address);
    bool number_shortcut_allowed(void) const;
    void draw();
    void draw_header();
    void draw_status();
    void draw_help();
    void draw_number_picker();
    void refresh_popup_overlay();
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
    void opcode_picker_open(char seed);
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
    int number_picker_handle_key(int key);
    bool prompt_command(const char *title, char *buffer, int max_len, bool template_mode = false);
    void toggle_help();
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
    void apply_hex_digit(uint8_t value);
    void apply_ascii_char(char value);
    void apply_screen_char(char value);
    void apply_logical_delete();
    void asm_edit_history_reset(uint16_t anchor_addr);
    void asm_edit_history_push(uint16_t addr, uint8_t prev_byte, uint8_t prev_part, uint8_t prev_pending);
    bool asm_edit_history_pop();
    void exit_edit_mode();
    void reset_edit_blink();
    bool update_edit_blink();
    uint8_t disasm_length(uint16_t address) const;
    bool    asm_is_branch(uint16_t address);
    uint8_t asm_edit_part_count(uint16_t address);
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
    void init(Screen *screen, Keyboard *keyboard);
    void deinit(void);
    int poll(int);
};

#endif
