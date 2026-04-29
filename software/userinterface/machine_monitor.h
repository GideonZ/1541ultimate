#ifndef MACHINE_MONITOR_H
#define MACHINE_MONITOR_H

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
    MONITOR_VIEW_SCREEN
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
};

const char *monitor_error_text(MonitorError error);
void monitor_reset_saved_state(void);
void monitor_apply_goto(MachineMonitorState *state, uint16_t address);
void monitor_format_hex_row(uint16_t address, const uint8_t *bytes, char *out);
void monitor_format_text_row(uint16_t address, const uint8_t *bytes, int count, bool screen_codes, char *out);

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

class UserInterface;
class Screen;
class Keyboard;
class Window;

class MachineMonitor : public UIObject
{
    MemoryBackend *backend;
    MachineMonitorState state;

    Screen *screen;
    Keyboard *keyboard;
    Window *window;
    int content_height;
    int pending_hex_nibble;
    bool edit_mode;
    bool edit_cursor_visible;
    bool help_visible;
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
    // Direct-typing operand buffer used while the picker is open. Allows
    // power users to type "LDA 1000" or "LDA #FF" or "LDA (10),Y" without
    // having to navigate the picker. Empty when not in operand-typing mode.
    char opcode_operand[16];
    int  opcode_operand_len;
    uint8_t last_vic_bank;
#ifdef RUNS_ON_PC
    uint8_t edit_blink_polls;
#else
    uint16_t edit_blink_ms;
#endif

    uint8_t canonical_read(uint16_t address);
    void canonical_write(uint16_t address, uint8_t value);
    void read_row(uint16_t address, uint8_t *dst, uint16_t len);
    void draw();
    void draw_header();
    void draw_status();
    void draw_help();
    void draw_hex();
    void draw_ascii();
    void draw_screen_codes();
    void draw_disassembly();
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
    void page_move(int lines);
    bool prompt_command(const char *title, char *buffer, int max_len, bool template_mode = false);
    void toggle_help();
    int handle_key(int key);
    void enter_edit_mode();
    void apply_hex_digit(uint8_t value);
    void apply_ascii_char(char value);
    void apply_screen_char(char value);
    void apply_logical_delete();
    void apply_logical_delete_buffer();
    void asm_edit_history_reset(uint16_t anchor_addr);
    void asm_edit_history_push(uint16_t addr, uint8_t prev_byte, uint8_t prev_part, uint8_t prev_pending);
    bool asm_edit_history_pop();
    void exit_edit_mode();
    void reset_edit_blink();
    bool update_edit_blink();
    uint8_t disasm_length(uint16_t address);
    bool    asm_is_branch(uint16_t address);
    uint8_t asm_edit_part_count(uint16_t address);
    uint16_t disasm_next_addr(uint16_t address);
    uint16_t disasm_prev_addr(uint16_t address);
    void step_disassembly(int lines);
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
