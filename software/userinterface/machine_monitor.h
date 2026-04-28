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
    MONITOR_VIEW_DISASM,
    MONITOR_VIEW_ASCII,
    MONITOR_VIEW_SCREEN
};

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
MonitorError monitor_parse_registers(const char *text, uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *sp, uint8_t *sr);

void monitor_fill_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint8_t value);
void monitor_transfer_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest);
int monitor_compare_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest, char *out, int out_len);
int monitor_hunt_memory(MemoryBackend *backend, uint16_t start, uint16_t end, const uint8_t *needle, int needle_len, char *out, int out_len);

MonitorError monitor_format_evaluate(const char *input, char *out, int out_len);

class UserInterface;
class Screen;
class Keyboard;
class Window;

class MachineMonitor : public UIObject
{
    MemoryBackend *backend;
    MachineMonitorState state;

    uint16_t registers_pc;
    uint8_t registers_a;
    uint8_t registers_x;
    uint8_t registers_y;
    uint8_t registers_sp;
    uint8_t registers_sr;

    Screen *screen;
    Keyboard *keyboard;
    Window *window;
    int content_height;
    char status_line[40];
    int pending_hex_nibble;
    bool edit_mode;
    bool edit_cursor_visible;
    bool help_visible;
    uint8_t last_vic_bank;
#ifdef RUNS_ON_PC
    uint8_t edit_blink_polls;
#else
    uint16_t edit_blink_ms;
#endif

    uint8_t canonical_read(uint16_t address);
    void canonical_write(uint16_t address, uint8_t value);
    void read_row(uint16_t address, uint8_t *dst, uint16_t len);
    void set_status(const char *text);
    void draw();
    void draw_header();
    void draw_message();
    void draw_status();
    void draw_help();
    void draw_hex();
    void draw_ascii();
    void draw_screen_codes();
    void draw_disassembly();
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
    void exit_edit_mode();
    void reset_edit_blink();
    bool update_edit_blink();
    uint8_t disasm_length(uint16_t address);
    uint16_t disasm_next_addr(uint16_t address);
    uint16_t disasm_prev_addr(uint16_t address);
    void step_disassembly(int lines);
    bool inline_edit_supported(void) const;
    uint16_t row_span(void) const;

public:
    MachineMonitor(UserInterface *ui, MemoryBackend *backend);
    void init(Screen *screen, Keyboard *keyboard);
    void deinit(void);
    int poll(int);
};

#endif