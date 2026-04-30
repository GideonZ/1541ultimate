#include "machine_monitor.h"

#include "assembler_6502.h"
#include "disassembler_6502.h"
#include "editor.h"
#include "userinterface.h"
#include "monitor_file_io.h"
#include "screen.h"
#include "keyboard.h"

extern "C" {
#include "dump_hex.h"
#include "small_printf.h"
}

#include <string.h>
#include <stdlib.h>

namespace {

#ifdef RUNS_ON_PC
static const uint8_t monitor_cursor_blink_idle_polls = 3;
#else
static const uint16_t monitor_cursor_blink_half_period_ms = 400;
#endif

static const char *const monitor_help_lines[] = {
    "M Memory    I ASCII     V Screen",
    "A Assembly  B Binary    U Undoc Op",
    "J Jump      G Go",
    "",
    "E Edit      F Fill      T Transfer",
    "C Compare   H Hunt      N Number",
    "W Width     R Range     Z Freeze",
    "O CPU Bank  Sh+O VIC",
    "L Load      S Save",
    "",
    "Open monitor:  C=+O",
    "Close monitor: C=+O / ESC",
    "Leave edit:    C=+E",
    "Copy/Paste:    C=+C / C=+V",
    NULL
};

static bool monitor_saved_state_valid = false;
static MachineMonitorState monitor_saved_state = { MONITOR_VIEW_HEX, 0x0801, 0x0801, 0, false, 0x07 };

// Persistent LOAD/SAVE/GOTO state (across monitor invocations within a single
// firmware run). Defaults match the spec: a "press L, RETURN, RETURN" sequence
// loads a PRG to its embedded address, "press S, RETURN, RETURN" saves the
// canonical $0800-$9FFF range, and GOTO pre-fills with the current cursor
// address (handled at the call site).
static bool     monitor_last_load_use_prg = true;
static uint16_t monitor_last_load_start = 0x0801;
static uint16_t monitor_last_load_offset = 0x0000;
static bool     monitor_last_load_length_auto = true;
static uint32_t monitor_last_load_length = 0;

static uint16_t monitor_last_save_start = 0x0800;
static uint16_t monitor_last_save_end = 0x9FFF;
static char     monitor_last_save_name[40] = "";

static bool     monitor_last_goto_valid = false;
static uint16_t monitor_last_goto_addr = 0;

// Persistent BINARY view configuration. Width is byte-based only; every byte
// renders as 8 bits and R is reserved for range mode.
static uint8_t monitor_binary_bytes_per_row = 1;
static Clipboard monitor_clipboard = { NULL, 0 };

static inline uint8_t monitor_binary_byte_stride(void)
{
    uint8_t b = monitor_binary_bytes_per_row;
    if (b < MONITOR_BINARY_MIN_BYTES_PER_ROW) b = MONITOR_BINARY_MIN_BYTES_PER_ROW;
    if (b > MONITOR_BINARY_MAX_BYTES_PER_ROW) b = MONITOR_BINARY_MAX_BYTES_PER_ROW;
    return b;
}

static bool is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool is_hex_char(char c)
{
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'));
}

static char to_upper_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    }
    return c;
}

static void skip_spaces(const char *&cursor)
{
    while (*cursor && is_space_char(*cursor)) {
        cursor++;
    }
}

static MonitorError parse_hex_digits(const char *&cursor, int min_digits, int max_digits, uint16_t max_value, uint16_t *value)
{
    int digits = 0;
    uint32_t parsed = 0;

    skip_spaces(cursor);
    if (*cursor == '$') {
        cursor++;
    }
    while (is_hex_char(*cursor) && digits < max_digits) {
        char c = to_upper_char(*cursor++);
        parsed <<= 4;
        if (c >= '0' && c <= '9') {
            parsed |= (uint32_t)(c - '0');
        } else {
            parsed |= (uint32_t)(c - 'A' + 10);
        }
        digits++;
    }
    if (digits < min_digits) {
        return MONITOR_ADDR;
    }
    if (parsed > max_value) {
        return (max_value == 0xFF) ? MONITOR_VALUE : MONITOR_ADDR;
    }
    *value = (uint16_t)parsed;
    return MONITOR_OK;
}

static MonitorError expect_separator(const char *&cursor, char sep, MonitorError error)
{
    skip_spaces(cursor);
    if (*cursor != sep) {
        return error;
    }
    cursor++;
    return MONITOR_OK;
}

static int append_text(char *out, int out_len, int pos, const char *text)
{
    while (*text && pos < (out_len - 1)) {
        out[pos++] = *text++;
    }
    out[pos] = 0;
    return pos;
}

static int append_line_address(char *out, int out_len, int pos, uint16_t address)
{
    char line[8];
    sprintf(line, "$%04x\n", address);
    return append_text(out, out_len, pos, line);
}

static int parse_decimal(const char *cursor, uint16_t *value)
{
    uint32_t parsed = 0;
    int digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10 + (uint32_t)(*cursor - '0');
        cursor++;
        digits++;
    }
    if (digits == 0 || *cursor) {
        return 0;
    }
    if (parsed > 0xFFFF) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static int parse_binary(const char *cursor, uint16_t *value)
{
    uint32_t parsed = 0;
    int digits = 0;
    while (*cursor == '0' || *cursor == '1') {
        parsed = (parsed << 1) | (uint32_t)(*cursor - '0');
        cursor++;
        digits++;
        if (parsed > 0xFFFF) {
            return 0;
        }
    }
    if (digits == 0 || *cursor) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static MonitorError parse_number_core(const char *text, uint16_t *value)
{
    char buffer[80];
    int pos = 0;
    const char *cursor = text;

    while (*cursor && pos < 79) {
        if (!is_space_char(*cursor)) {
            buffer[pos++] = *cursor;
        }
        cursor++;
    }
    buffer[pos] = 0;
    if (!buffer[0]) {
        return MONITOR_SYNTAX;
    }
    if (buffer[0] == '$') {
        const char *hex = buffer;
        return parse_hex_digits(hex, 1, 4, 0xFFFF, value);
    }
    if (buffer[0] == '0' && (buffer[1] == 'x' || buffer[1] == 'X')) {
        const char *hex = buffer + 2;
        return parse_hex_digits(hex, 1, 4, 0xFFFF, value);
    }
    if (buffer[0] == '%') {
        return parse_binary(buffer + 1, value) ? MONITOR_OK : MONITOR_SYNTAX;
    }
    return parse_decimal(buffer, value) ? MONITOR_OK : MONITOR_SYNTAX;
}

static char ascii_byte(uint8_t value)
{
    if (value == 0 || value == 8 || value == 10 || value == 13 || (value >= 20 && value <= 31) || (value >= 144 && value <= 159)) {
        return '.';
    }
    if (value < 32 || value > 126) {
        return '.';
    }
    return (char)value;
}

static char screen_code_char(uint8_t value)
{
    static const char translation[128] = {
        /* 00-07 */ '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
        /* 08-0F */ 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
        /* 10-17 */ 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
        /* 18-1F */ 'X', 'Y', 'Z', '[', '#', ']', '^', '<',
        /* 20-27 */ ' ', '!', '"', '#', '$', '%', '&', '\'',
        /* 28-2F */ '(', ')', '*', '+', ',', '-', '.', '/',
        /* 30-37 */ '0', '1', '2', '3', '4', '5', '6', '7',
        /* 38-3F */ '8', '9', ':', ';', '<', '=', '>', '?',
        /* 40-47 */ CHR_HORIZONTAL_LINE, 'S', '|', CHR_HORIZONTAL_LINE,
                    CHR_HORIZONTAL_LINE, CHR_HORIZONTAL_LINE, CHR_HORIZONTAL_LINE, '|',
        /* 48-4F */ '|', CHR_ROUNDED_UPPER_RIGHT, CHR_ROUNDED_LOWER_LEFT, CHR_ROUNDED_LOWER_RIGHT,
                    CHR_SOLID_BAR_LOWER_7, '\\', '/', CHR_SOLID_BAR_UPPER_7,
        /* 50-57 */ CHR_SOLID_BAR_UPPER_7, '*', CHR_HORIZONTAL_LINE, 'H',
                    '|', CHR_ROUNDED_UPPER_LEFT, 'X', 'O',
        /* 58-5F */ 'C', '|', CHR_DIAMOND, '+', '#', CHR_VERTICAL_LINE, 'P', '^',
        /* 60-67 */ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        /* 68-6F */ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        /* 70-77 */ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        /* 78-7F */ ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
    };

    return translation[value & 0x7F];
}

static void draw_padded(Window *window, int y, const char *text, int len)
{
    int width = window->get_size_x();

    if (len > width) {
        len = width;
    }
    if (len < 0) {
        len = 0;
    }

    window->move_cursor(0, y);
    if (len > 0) {
        window->output_length(text, len);
    }
    window->repeat(' ', width - len);
}

static void draw_with_highlight(Window *window, int y, const char *text, int len, int highlight_x, int highlight_len)
{
    int width = window->get_size_x();
    int before = highlight_x;
    int after;

    if (len > width) {
        len = width;
    }
    if (len < 0) {
        len = 0;
    }
    if (highlight_x < 0 || highlight_len <= 0 || highlight_x >= len) {
        draw_padded(window, y, text, len);
        return;
    }
    if (before < 0) {
        before = 0;
    }
    after = highlight_x + highlight_len;
    if (after > len) {
        after = len;
    }

    window->move_cursor(0, y);
    window->reverse_mode(0);
    if (before > 0) {
        window->output_length(text, before);
    }
    window->reverse_mode(1);
    window->output_length(text + before, after - before);
    window->reverse_mode(0);
    if (after < len) {
        window->output_length(text + after, len - after);
    }
    window->repeat(' ', width - len);
}

static void draw_with_mask(Window *window, int y, const char *text, int len, const bool *mask)
{
    int width = window->get_size_x();
    bool reverse = false;

    if (len > width) {
        len = width;
    }
    if (len < 0) {
        len = 0;
    }

    window->move_cursor(0, y);
    window->reverse_mode(0);
    for (int i = 0; i < len; i++) {
        bool want = mask && mask[i];
        if (want != reverse) {
            reverse = want;
            window->reverse_mode(reverse ? 1 : 0);
        }
        window->output_length(text + i, 1);
    }
    if (reverse) {
        window->reverse_mode(0);
    }
    window->repeat(' ', width - len);
}

struct MonitorCpuStatusFields {
    const char *a000;
    const char *d000;
    const char *e000;
};

static const MonitorCpuStatusFields monitor_cpu_status_fields[8] = {
    { "RAM", "RAM", "RAM" }, // CPU0
    { "RAM", "CHR", "RAM" }, // CPU1
    { "RAM", "CHR", "KRN" }, // CPU2
    { "BAS", "CHR", "KRN" }, // CPU3
    { "RAM", "RAM", "RAM" }, // CPU4
    { "RAM", "I/O", "RAM" }, // CPU5
    { "RAM", "I/O", "KRN" }, // CPU6
    { "BAS", "I/O", "KRN" }, // CPU7
};

static const uint16_t monitor_vic_bank_bases[4] = {
    0x0000, 0x4000, 0x8000, 0xC000
};

static uint8_t next_cpu_mode(uint8_t cpu_port)
{
    return (uint8_t)((cpu_port + 1) & 0x07);
}

static uint8_t next_vic_bank(uint8_t vic_bank)
{
    return (uint8_t)((vic_bank + 1) & 0x03);
}

static uint8_t normalize_cpu_mode(uint8_t cpu_port)
{
    return cpu_port & 0x07;
}

static void format_status_line_impl(char *line, uint8_t port01, uint8_t vic_bank)
{
    uint8_t cpu_bank = port01 & 0x07;
    const MonitorCpuStatusFields *fields = &monitor_cpu_status_fields[cpu_bank];
    uint16_t vic_base;
    int pos;

    vic_bank &= 0x03;
    vic_base = monitor_vic_bank_bases[vic_bank];

    sprintf(line, "CPU%c $A:%s $D:%s $E:%s VIC%c $",
            (char)('0' + cpu_bank),
            fields->a000,
            fields->d000,
            fields->e000,
            (char)('0' + vic_bank));
    pos = strlen(line);
    dump_hex_word(line, pos, vic_base);
    line[pos + 4] = 0;
}

static bool inline_edit_view(MachineMonitorView view)
{
    return (view == MONITOR_VIEW_HEX) || (view == MONITOR_VIEW_ASCII) ||
           (view == MONITOR_VIEW_SCREEN) || (view == MONITOR_VIEW_ASM) ||
           (view == MONITOR_VIEW_BINARY);
}

static uint16_t row_span_for_view(MachineMonitorView view)
{
    if (view == MONITOR_VIEW_ASCII || view == MONITOR_VIEW_SCREEN) {
        return MONITOR_TEXT_BYTES_PER_ROW;
    }
    if (view == MONITOR_VIEW_BINARY) {
        // Width is dynamic and configured on the fly by the W shortcut.
        return monitor_binary_byte_stride();
    }
    return MONITOR_HEX_BYTES_PER_ROW;
}

}

void monitor_format_status_line(char *line, uint8_t port01, uint8_t vic_bank)
{
    format_status_line_impl(line, port01, vic_bank);
}

const char *monitor_error_text(MonitorError error)
{
    switch (error) {
        case MONITOR_ADDR: return "?ADDR";
        case MONITOR_SYNTAX: return "?SYNTAX";
        case MONITOR_VALUE: return "?VALUE";
        case MONITOR_RANGE: return "?RANGE";
        default: return "";
    }
}

void monitor_reset_saved_state(void)
{
    // Tests use this to put all persisted monitor state into a deterministic,
    // known position. Production startup, by contrast, uses the static
    // initializers which default to $0801 and the canonical command defaults.
    monitor_saved_state_valid = true;
    monitor_saved_state.view = MONITOR_VIEW_HEX;
    monitor_saved_state.current_addr = 0;
    monitor_saved_state.base_addr = 0;
    monitor_saved_state.disasm_offset = 0;
    monitor_saved_state.illegal_enabled = false;
    monitor_saved_state.cpu_port = 0x07;

    monitor_last_load_use_prg = true;
    monitor_last_load_start = 0x0801;
    monitor_last_load_offset = 0x0000;
    monitor_last_load_length_auto = true;
    monitor_last_load_length = 0;

    monitor_last_save_start = 0x0800;
    monitor_last_save_end = 0x9FFF;
    monitor_last_save_name[0] = 0;

    monitor_last_goto_valid = false;
    monitor_last_goto_addr = 0;

    monitor_binary_bytes_per_row = 1;

    if (monitor_clipboard.data) {
        free(monitor_clipboard.data);
        monitor_clipboard.data = NULL;
    }
    monitor_clipboard.length = 0;
}

void monitor_invalidate_saved_state(void)
{
    monitor_saved_state_valid = false;
}

void monitor_apply_goto(MachineMonitorState *state, uint16_t address)
{
    uint16_t span = row_span_for_view(state->view);
    state->current_addr = address;
    if (state->view == MONITOR_VIEW_BINARY) {
        // Width is not necessarily a power of two; align using modular
        // arithmetic so a row-aligned base remains a multiple of the span.
        if (span == 0) span = 1;
        state->base_addr = (uint16_t)(address - (address % span));
    } else {
        state->base_addr = (uint16_t)(address & (uint16_t)(~(span - 1)));
    }
    if (state->view == MONITOR_VIEW_DISASM) {
        state->base_addr = address;
    }
    state->disasm_offset = 0;
}

void monitor_format_hex_row(uint16_t address, const uint8_t *bytes, char *out)
{
    int i;

    memset(out, ' ', MONITOR_HEX_ROW_CHARS);
    dump_hex_word(out, 0, address);
    for (i = 0; i < MONITOR_HEX_BYTES_PER_ROW; i++) {
        dump_hex_byte(out, 5 + (3 * i), bytes[i]);
        out[29 + i] = ascii_byte(bytes[i]);
    }
    out[MONITOR_HEX_ROW_CHARS] = 0;
}

void monitor_format_text_row(uint16_t address, const uint8_t *bytes, int count, bool screen_codes, char *out)
{
    int i;

    memset(out, ' ', MONITOR_TEXT_ROW_CHARS);
    dump_hex_word(out, 0, address);
    out[4] = ' ';
    for (i = 0; i < count; i++) {
        out[5 + i] = screen_codes ? screen_code_char(bytes[i]) : ascii_byte(bytes[i]);
    }
    out[MONITOR_TEXT_ROW_CHARS] = 0;
}

MonitorError monitor_parse_address(const char *text, uint16_t *address)
{
    const char *cursor = text;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, address);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    skip_spaces(cursor);
    return *cursor ? MONITOR_ADDR : MONITOR_OK;
}

MonitorError monitor_parse_expression(const char *text, uint16_t *value)
{
    return parse_number_core(text, value);
}

MonitorError monitor_parse_byte_value(const char *text, uint8_t *value)
{
    const char *cursor = text;
    uint16_t parsed = 0;
    MonitorError error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed);
    if (error != MONITOR_OK) {
        return MONITOR_VALUE;
    }
    skip_spaces(cursor);
    if (*cursor) {
        return MONITOR_VALUE;
    }
    *value = (uint8_t)parsed;
    return MONITOR_OK;
}

MonitorError monitor_parse_fill(const char *text, uint16_t *start, uint16_t *end, uint8_t *value)
{
    const char *cursor = text;
    uint16_t parsed = 0;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, start);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    error = expect_separator(cursor, '-', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, end);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    if (*end < *start) {
        return MONITOR_RANGE;
    }
    error = expect_separator(cursor, ',', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, &parsed);
    if (error != MONITOR_OK) {
        return MONITOR_VALUE;
    }
    if (parsed > 0xFF) {
        return MONITOR_VALUE;
    }
    skip_spaces(cursor);
    if (*cursor) {
        return MONITOR_SYNTAX;
    }
    *value = (uint8_t)parsed;
    return MONITOR_OK;
}

MonitorError monitor_parse_transfer(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest)
{
    const char *cursor = text;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, start);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    error = expect_separator(cursor, '-', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, end);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    if (*end <= *start) {
        return MONITOR_RANGE;
    }
    error = expect_separator(cursor, ',', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, dest);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    skip_spaces(cursor);
    return *cursor ? MONITOR_SYNTAX : MONITOR_OK;
}

MonitorError monitor_parse_compare(const char *text, uint16_t *start, uint16_t *end, uint16_t *dest)
{
    return monitor_parse_transfer(text, start, end, dest);
}

MonitorError monitor_parse_hunt(const char *text, uint16_t *start, uint16_t *end, uint8_t *needle, int *needle_len)
{
    const char *cursor = text;
    int count = 0;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, start);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    error = expect_separator(cursor, '-', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, end);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    if (*end < *start) {
        return MONITOR_RANGE;
    }
    error = expect_separator(cursor, ',', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    skip_spaces(cursor);
    if (*cursor == '"') {
        cursor++;
        while (*cursor && *cursor != '"') {
            needle[count++] = (uint8_t)*cursor++;
        }
        if (*cursor != '"') {
            return MONITOR_SYNTAX;
        }
        cursor++;
    } else {
        while (*cursor) {
            uint16_t parsed = 0;
            error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed);
            if (error != MONITOR_OK) {
                return count ? MONITOR_SYNTAX : MONITOR_SYNTAX;
            }
            needle[count++] = (uint8_t)parsed;
            skip_spaces(cursor);
        }
    }
    skip_spaces(cursor);
    if (*cursor) {
        return MONITOR_SYNTAX;
    }
    if (count == 0) {
        return MONITOR_SYNTAX;
    }
    *needle_len = count;
    return MONITOR_OK;
}

void monitor_fill_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint8_t value)
{
    uint16_t address = start;
    do {
        backend->write(address, value);
    } while (address++ != end);
}

void monitor_transfer_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest)
{
    uint32_t length = (uint16_t)(end - start);
    if (length == 0) {
        return;
    }
    if (dest > start && dest < end) {
        while (length) {
            length--;
            backend->write((uint16_t)(dest + length), backend->read((uint16_t)(start + length)));
        }
    } else {
        uint32_t index;
        for (index = 0; index < length; index++) {
            backend->write((uint16_t)(dest + index), backend->read((uint16_t)(start + index)));
        }
    }
}

int monitor_compare_memory(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest, char *out, int out_len)
{
    uint32_t length = (uint16_t)(end - start);
    uint32_t index;
    int count = 0;
    int pos = 0;

    out[0] = 0;
    for (index = 0; index < length; index++) {
        if (backend->read((uint16_t)(start + index)) != backend->read((uint16_t)(dest + index))) {
            pos = append_line_address(out, out_len, pos, (uint16_t)(start + index));
            count++;
        }
    }
    if (!count) {
        append_text(out, out_len, pos, "No differences\n");
    }
    return count;
}

int monitor_compare_collect(MemoryBackend *backend, uint16_t start, uint16_t end, uint16_t dest, uint16_t *out_addrs, int max_addrs)
{
    uint32_t length = (uint16_t)(end - start);
    uint32_t index;
    int count = 0;

    if (max_addrs <= 0) return 0;
    for (index = 0; index < length; index++) {
        if (backend->read((uint16_t)(start + index)) != backend->read((uint16_t)(dest + index))) {
            if (count < max_addrs) {
                out_addrs[count] = (uint16_t)(start + index);
            }
            count++;
        }
    }
    return count;
}

int monitor_hunt_collect(MemoryBackend *backend, uint16_t start, uint16_t end, const uint8_t *needle, int needle_len, uint16_t *out_addrs, int max_addrs)
{
    uint32_t limit;
    uint32_t index;
    int count = 0;

    if (max_addrs <= 0) {
        return 0;
    }
    limit = (uint16_t)(end - start) + 1;
    if ((uint32_t)needle_len > limit) {
        return 0;
    }
    for (index = 0; index + (uint32_t)needle_len <= limit; index++) {
        int matched = 1;
        int i;
        for (i = 0; i < needle_len; i++) {
            if (backend->read((uint16_t)(start + index + i)) != needle[i]) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            if (count < max_addrs) {
                out_addrs[count] = (uint16_t)(start + index);
            }
            count++;
        }
    }
    return count;
}

int monitor_hunt_memory(MemoryBackend *backend, uint16_t start, uint16_t end, const uint8_t *needle, int needle_len, char *out, int out_len)
{
    uint32_t limit;
    uint32_t index;
    int count = 0;
    int pos = 0;

    out[0] = 0;
    limit = (uint16_t)(end - start) + 1;
    if ((uint32_t)needle_len > limit) {
        append_text(out, out_len, pos, "No matches\n");
        return 0;
    }
    for (index = 0; index + (uint32_t)needle_len <= limit; index++) {
        int matched = 1;
        int i;
        for (i = 0; i < needle_len; i++) {
            if (backend->read((uint16_t)(start + index + i)) != needle[i]) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            pos = append_line_address(out, out_len, pos, (uint16_t)(start + index));
            count++;
        }
    }
    if (!count) {
        append_text(out, out_len, pos, "No matches\n");
    }
    return count;
}

MonitorError monitor_format_evaluate(const char *input, char *out, int out_len)
{
    uint16_t value = 0;
    MonitorError error = monitor_parse_expression(input, &value);
    if (error != MONITOR_OK) {
        return error;
    }
    (void)out_len;
    sprintf(out, "$%04x / %u / %B", value, value, value & 0xFF);
    return MONITOR_OK;
}

namespace {

static bool keyword_match(const char *&cursor, const char *keyword)
{
    int i;
    for (i = 0; keyword[i]; i++) {
        if (to_upper_char(cursor[i]) != keyword[i]) {
            return false;
        }
    }
    char follow = cursor[i];
    if (follow != 0 && follow != ' ' && follow != '\t' && follow != ',') {
        return false;
    }
    cursor += i;
    return true;
}

// Parse 1..max_digits hex digits (no $ allowed; AUTO/PRG already filtered).
// Used internally for the length field which can hold up to 5 digits (0x10000).
static MonitorError parse_hex_unbounded(const char *&cursor, int max_digits, uint32_t max_value, uint32_t *value)
{
    uint32_t parsed = 0;
    int digits = 0;
    skip_spaces(cursor);
    if (*cursor == '$') {
        cursor++;
    }
    while (is_hex_char(*cursor) && digits < max_digits) {
        char c = to_upper_char(*cursor++);
        parsed <<= 4;
        if (c >= '0' && c <= '9') {
            parsed |= (uint32_t)(c - '0');
        } else {
            parsed |= (uint32_t)(c - 'A' + 10);
        }
        digits++;
    }
    if (digits == 0) {
        return MONITOR_VALUE;
    }
    if (parsed > max_value) {
        return MONITOR_VALUE;
    }
    *value = parsed;
    return MONITOR_OK;
}

}

// Parse the "[start],[offset],[length]" prompt that follows a LOAD file pick.
// All fields are optional, individually and as trailing omissions:
//   ""                  -> PRG, 0000, AUTO  (typical RETURN-only path)
//   "PRG"               -> PRG, 0000, AUTO
//   "0801"              -> 0801, 0000, AUTO
//   "PRG,1000"          -> PRG, 1000, AUTO
//   "0801,0002,0010"    -> 0801, 0002, length=0x10
//   ",,0010"            -> PRG, 0000, length=0x10
// Whitespace around fields is ignored. Empty fields take their default.
MonitorError monitor_parse_load_params(const char *text, bool *use_prg_addr, uint16_t *start_addr,
                                       uint16_t *offset, bool *length_auto, uint32_t *length)
{
    *use_prg_addr = true;
    *start_addr = 0;
    *offset = 0;
    *length_auto = true;
    *length = 0;

    const char *cursor = text;
    skip_spaces(cursor);

    // Field 1 — start address or PRG.
    if (!*cursor) return MONITOR_OK;
    if (*cursor != ',') {
        if (keyword_match(cursor, "PRG")) {
            *use_prg_addr = true;
            *start_addr = 0;
        } else {
            *use_prg_addr = false;
            MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, start_addr);
            if (error != MONITOR_OK) return MONITOR_ADDR;
        }
        skip_spaces(cursor);
    }
    if (!*cursor) return MONITOR_OK;
    if (*cursor != ',') return MONITOR_SYNTAX;
    cursor++;
    skip_spaces(cursor);

    // Field 2 — offset (hex 0..FFFF), optional.
    if (*cursor && *cursor != ',') {
        MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, offset);
        if (error != MONITOR_OK) return MONITOR_ADDR;
        skip_spaces(cursor);
    }
    if (!*cursor) return MONITOR_OK;
    if (*cursor != ',') return MONITOR_SYNTAX;
    cursor++;
    skip_spaces(cursor);

    // Field 3 — length (hex 1..10000) or AUTO, optional.
    if (*cursor) {
        if (keyword_match(cursor, "AUTO")) {
            *length_auto = true;
            *length = 0;
        } else {
            uint32_t parsed = 0;
            MonitorError error = parse_hex_unbounded(cursor, 5, 0x10000, &parsed);
            if (error != MONITOR_OK) return MONITOR_VALUE;
            if (parsed == 0) return MONITOR_VALUE;
            *length_auto = false;
            *length = parsed;
        }
        skip_spaces(cursor);
    }
    if (*cursor) return MONITOR_SYNTAX;
    return MONITOR_OK;
}

MonitorError monitor_parse_save_params(const char *text, uint16_t *start, uint16_t *end)
{
    const char *cursor = text;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, start);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    error = expect_separator(cursor, '-', MONITOR_SYNTAX);
    if (error != MONITOR_OK) {
        return error;
    }
    error = parse_hex_digits(cursor, 1, 4, 0xFFFF, end);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    if (*end < *start) {
        return MONITOR_RANGE;
    }
    skip_spaces(cursor);
    if (*cursor) {
        return MONITOR_SYNTAX;
    }
    return MONITOR_OK;
}

MonitorError monitor_validate_load_size(uint32_t file_size, uint32_t offset, bool length_auto,
                                        uint32_t length, uint32_t *effective_len)
{
    if (offset > file_size) {
        return MONITOR_RANGE;
    }
    if (offset == file_size) {
        return MONITOR_RANGE;
    }
    uint32_t avail = file_size - offset;
    uint32_t use;
    if (length_auto) {
        use = avail;
    } else {
        // Reject if the requested length exceeds what the file can supply: the
        // user explicitly asked for that many bytes and silently truncating
        // would mask the mistake.
        if (length > avail) {
            return MONITOR_RANGE;
        }
        use = length;
    }
    if (use == 0) {
        return MONITOR_RANGE;
    }
    if (use > 65536) {
        return MONITOR_RANGE;
    }
    *effective_len = use;
    return MONITOR_OK;
}

MachineMonitor :: MachineMonitor(UserInterface *ui, MemoryBackend *mem_backend) : UIObject(ui)
{
    backend = mem_backend;
    if (monitor_saved_state_valid) {
        state = monitor_saved_state;
    } else {
        state.view = MONITOR_VIEW_HEX;
        state.current_addr = 0x0801;
        state.base_addr = 0x0801;
        state.disasm_offset = 0;
        state.illegal_enabled = false;
        state.cpu_port = 0x07;
    }
    screen = NULL;
    keyboard = NULL;
    window = NULL;
    content_height = 0;
    pending_hex_nibble = -1;
    edit_mode = false;
    edit_cursor_visible = true;
    help_visible = false;
    range_mode = false;
    range_anchor = state.current_addr;
    number_picker_active = false;
    number_selected = 0;
    number_value = 0;
    hunt_picker_active = false;
    hunt_count = 0;
    hunt_selected = 0;
    hunt_top = 0;
    hunt_picker_label = "Hunt results";
    asm_edit_part = 0;
    asm_edit_pending = 0;
    asm_edit_hist_len = 0;
    asm_edit_hist_anchor_addr = 0;
    opcode_picker_active = false;
    opcode_prefix[0] = 0;
    opcode_prefix_len = 0;
    opcode_candidate_count = 0;
    opcode_selected = 0;
    opcode_top = 0;
    opcode_operand[0] = 0;
    opcode_operand_len = 0;
    current_vic_bank = 0;
    last_live_vic_bank = 0;
    vic_bank_override = false;
    binary_bit_index = 7;
#ifdef RUNS_ON_PC
    edit_blink_polls = monitor_cursor_blink_idle_polls;
#else
    edit_blink_ms = 0;
#endif
}

uint8_t MachineMonitor :: canonical_read(uint16_t address)
{
    return backend->read(address);
}

void MachineMonitor :: canonical_write(uint16_t address, uint8_t value)
{
    backend->write(address, value);
}

void MachineMonitor :: read_row(uint16_t address, uint8_t *dst, uint16_t len) const
{
    uint16_t first = len;
    if ((uint32_t)address + len > 0x10000UL) {
        first = (uint16_t)(0x10000UL - address);
    }
    backend->read_block(address, dst, first);
    if (first < len) {
        backend->read_block(0, dst + first, (uint16_t)(len - first));
    }
}

Cursor MachineMonitor :: active_cursor(void) const
{
    Cursor cursor;
    cursor.address = state.current_addr;
    cursor.bit_index = (binary_bit_index <= 7) ? binary_bit_index : 7;
    return cursor;
}

bool MachineMonitor :: range_contains(uint16_t address) const
{
    if (!range_mode) {
        return false;
    }
    uint16_t start = range_anchor;
    uint16_t end = state.current_addr;
    if (state.view == MONITOR_VIEW_ASM) {
        uint16_t anchor_end = (uint16_t)(range_anchor + disasm_length(range_anchor) - 1);
        uint16_t current_end = (uint16_t)(state.current_addr + disasm_length(state.current_addr) - 1);
        if (state.current_addr < range_anchor) {
            start = state.current_addr;
            end = anchor_end;
        } else {
            start = range_anchor;
            end = current_end;
        }
    }
    if (end < start) {
        uint16_t tmp = start;
        start = end;
        end = tmp;
    }
    return address >= start && address <= end;
}

bool MachineMonitor :: clipboard_copy_byte(uint8_t value)
{
    uint8_t *data = (uint8_t *)malloc(1);
    if (!data) {
        return false;
    }
    data[0] = value;
    if (monitor_clipboard.data) {
        free(monitor_clipboard.data);
    }
    monitor_clipboard.data = data;
    monitor_clipboard.length = 1;
    return true;
}

bool MachineMonitor :: clipboard_copy_current(void)
{
    return clipboard_copy_byte(canonical_read(state.current_addr));
}

bool MachineMonitor :: clipboard_copy_range(void)
{
    uint16_t start = range_anchor;
    uint16_t end = state.current_addr;
    if (state.view == MONITOR_VIEW_ASM) {
        uint16_t anchor_end = (uint16_t)(range_anchor + disasm_length(range_anchor) - 1);
        uint16_t current_end = (uint16_t)(state.current_addr + disasm_length(state.current_addr) - 1);
        if (state.current_addr < range_anchor) {
            start = state.current_addr;
            end = anchor_end;
        } else {
            start = range_anchor;
            end = current_end;
        }
    }
    if (end < start) {
        uint16_t tmp = start;
        start = end;
        end = tmp;
    }

    uint32_t length = (uint32_t)end - (uint32_t)start + 1;
    uint8_t *data = (uint8_t *)malloc((size_t)length);
    if (!data) {
        return false;
    }
    for (uint32_t i = 0; i < length; i++) {
        data[i] = canonical_read((uint16_t)(start + i));
    }
    if (monitor_clipboard.data) {
        free(monitor_clipboard.data);
    }
    monitor_clipboard.data = data;
    monitor_clipboard.length = (size_t)length;
    return true;
}

bool MachineMonitor :: clipboard_paste(void)
{
    if (!monitor_clipboard.data || monitor_clipboard.length == 0) {
        return false;
    }
    for (size_t i = 0; i < monitor_clipboard.length; i++) {
        canonical_write((uint16_t)(state.current_addr + i), monitor_clipboard.data[i]);
    }
    state.current_addr = (uint16_t)(state.current_addr + monitor_clipboard.length);
    binary_bit_index = 7;
    if (state.view == MONITOR_VIEW_ASM) {
        ensure_disasm_visible();
    } else {
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
    return true;
}

void MachineMonitor :: toggle_range_mode(void)
{
    if (!range_mode) {
        range_anchor = state.current_addr;
        range_mode = true;
        return;
    }
    if (!clipboard_copy_range()) {
        get_ui()->popup("?MEM", BUTTON_OK);
        redraw_full();
        return;
    }
    range_mode = false;
}

void MachineMonitor :: open_number_picker(void)
{
    help_visible = false;
    number_value = canonical_read(state.current_addr);
    number_selected = 0;
    number_picker_active = true;
}

int MachineMonitor :: number_picker_handle_key(int key)
{
    if (key == KEY_ESCAPE || key == KEY_BREAK || key == KEY_RETURN) {
        number_picker_active = false;
        draw();
        return 0;
    }
    if (key == KEY_CTRL_O) {
        number_picker_active = false;
        return 1;
    }
    if (key == KEY_UP) {
        if (number_selected > 0) {
            number_selected--;
        }
        draw();
        return 0;
    }
    if (key == KEY_DOWN) {
        if (number_selected < 4) {
            number_selected++;
        }
        draw();
        return 0;
    }
    if (key == KEY_CTRL_C) {
        if (!clipboard_copy_byte(number_value)) {
            get_ui()->popup("?MEM", BUTTON_OK);
            redraw_full();
        }
        number_picker_active = false;
        draw();
        return 0;
    }
    return 0;
}

void MachineMonitor :: ensure_current_visible()
{
    if (state.view == MONITOR_VIEW_DISASM) {
        return;
    }

    uint16_t span = row_span();
    if (span == 0) span = 1;
    uint16_t page_bytes = (uint16_t)(content_height * span);
    uint16_t current_row;
    if (state.view == MONITOR_VIEW_BINARY) {
        current_row = (uint16_t)(state.current_addr - (state.current_addr % span));
        state.base_addr = (uint16_t)(state.base_addr - (state.base_addr % span));
    } else {
        current_row = (uint16_t)(state.current_addr & (uint16_t)(~(span - 1)));
        state.base_addr = (uint16_t)(state.base_addr & (uint16_t)(~(span - 1)));
    }

    if (current_row < state.base_addr) {
        state.base_addr = current_row;
        return;
    }

    if ((uint16_t)(current_row - state.base_addr) >= page_bytes) {
        state.base_addr = (uint16_t)(current_row - (page_bytes - span));
    }
}

void MachineMonitor :: set_view(MachineMonitorView view)
{
    state.view = view;
    if (view == MONITOR_VIEW_ASM) {
        ensure_disasm_visible();
    } else {
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
    binary_bit_index = 7;
}

bool MachineMonitor :: inline_edit_supported(void) const
{
    return inline_edit_view(state.view);
}

uint16_t MachineMonitor :: row_span(void) const
{
    return row_span_for_view(state.view);
}

void MachineMonitor :: move_current(int delta)
{
    state.current_addr = (uint16_t)(state.current_addr + delta);
    if (state.view == MONITOR_VIEW_ASM) {
        ensure_disasm_visible();
    } else {
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
    if (state.view != MONITOR_VIEW_BINARY) {
        binary_bit_index = 7;
    }
    if (edit_mode) {
        reset_edit_blink();
    }
}

void MachineMonitor :: move_binary_bits(int delta)
{
    if (binary_bit_index > 7) {
        binary_bit_index = 7;
    }
    while (delta > 0) {
        if (binary_bit_index > 0) {
            binary_bit_index--;
        } else {
            state.current_addr = (uint16_t)(state.current_addr + 1);
            binary_bit_index = 7;
        }
        delta--;
    }
    while (delta < 0) {
        if (binary_bit_index < 7) {
            binary_bit_index++;
        } else {
            state.current_addr = (uint16_t)(state.current_addr - 1);
            binary_bit_index = 0;
        }
        delta++;
    }
    ensure_current_visible();
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
}

void MachineMonitor :: page_move(int lines)
{
    uint16_t span = row_span();
    if (span == 0) span = 1;
    uint16_t step = (uint16_t)(lines * span);

    state.current_addr = (uint16_t)(state.current_addr + step);
    if (state.view == MONITOR_VIEW_ASM) {
        ensure_disasm_visible();
    } else {
        state.base_addr = (uint16_t)(state.base_addr + step);
        if (state.view == MONITOR_VIEW_BINARY) {
            state.base_addr = (uint16_t)(state.base_addr - (state.base_addr % span));
        } else {
            state.base_addr = (uint16_t)(state.base_addr & (uint16_t)(~(span - 1)));
        }
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
}

bool MachineMonitor :: prompt_command(const char *title, char *buffer, int max_len, bool template_mode)
{
    return get_ui()->string_box(title, buffer, max_len, template_mode, true) > 0;
}

void MachineMonitor :: toggle_help()
{
    help_visible = !help_visible;
}

void MachineMonitor :: reset_edit_blink()
{
    edit_cursor_visible = true;
}

bool MachineMonitor :: update_edit_blink()
{
    return false;
}

uint8_t MachineMonitor :: disasm_length(uint16_t address) const
{
    Disassembled6502 decoded;
    uint8_t row_bytes[3];

    read_row(address, row_bytes, 3);
    disassemble_6502(address, row_bytes, state.illegal_enabled, &decoded);
    return decoded.length ? decoded.length : 1;
}

uint16_t MachineMonitor :: disasm_next_addr(uint16_t address)
{
    return (uint16_t)(address + disasm_length(address));
}

bool MachineMonitor :: asm_is_branch(uint16_t address)
{
    uint8_t op = canonical_read(address);
    const char *templ = disassembler_6502_template(op);
    if (!templ) return false;
    // Mirror the disassembler's own rule (disassembler_6502.cc): any B*
    // mnemonic other than BRK is a relative branch. Some templates spell
    // out "rel" explicitly while others (BCC/BCS/BNE/BEQ) do not, so we
    // test the leading mnemonic rather than relying on "rel" being present.
    if (templ[0] == 'B' && templ[1] != 'R') return true;
    return strstr(templ, "rel") != NULL;
}

// Number of editable parts presented in the ASM edit cursor for the
// instruction at `address`. Branch instructions are encoded as opcode +
// 1-byte signed offset but are *displayed* as opcode + absolute 16-bit
// target, so we expose three editable parts (mnemonic, target high, target
// low) for branches and let the byte-edit path translate the typed target
// back into a relative offset.
uint8_t MachineMonitor :: asm_edit_part_count(uint16_t address)
{
    uint8_t len = disasm_length(address);
    if (len == 0) len = 1;
    if (asm_is_branch(address) && len == 2) return 3;
    return len;
}

uint16_t MachineMonitor :: disasm_prev_addr(uint16_t address)
{
    for (uint16_t back = 3; back >= 1; back--) {
        uint16_t candidate = (uint16_t)(address - back);
        if (disasm_length(candidate) == back) {
            return candidate;
        }
    }
    return (uint16_t)(address - 1);
}

void MachineMonitor :: ensure_disasm_visible()
{
    if (state.view != MONITOR_VIEW_ASM) {
        return;
    }
    int diff = (int16_t)(state.current_addr - state.base_addr);
    if (diff < 0) {
        state.base_addr = state.current_addr;
        return;
    }
    uint16_t a = state.base_addr;
    int rows = 0;
    int max_scan = (content_height > 0 ? content_height : 1) + 64;
    while (a != state.current_addr && rows < max_scan) {
        a = disasm_next_addr(a);
        rows++;
    }
    if (a != state.current_addr) {
        state.base_addr = state.current_addr;
        return;
    }
    if (content_height > 0 && rows >= content_height) {
        int n = rows - (content_height - 1);
        for (int i = 0; i < n; i++) {
            state.base_addr = disasm_next_addr(state.base_addr);
        }
    }
}

void MachineMonitor :: step_disassembly(int lines)
{
    while (lines < 0) {
        state.current_addr = disasm_prev_addr(state.current_addr);
        lines++;
    }
    while (lines > 0) {
        state.current_addr = disasm_next_addr(state.current_addr);
        lines--;
    }
    ensure_disasm_visible();
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
}

void MachineMonitor :: draw_header()
{
    char line[64];
    int width = window->get_size_x();
    if (width >= (int)sizeof(line)) {
        width = (int)sizeof(line) - 1;
    }

    if (help_visible) {
        strcpy(line, "Help (F3/? closes)");
    } else if (hunt_picker_active) {
        sprintf(line, "%s  %d/%d", hunt_picker_label ? hunt_picker_label : "RESULTS",
                hunt_count ? (hunt_selected + 1) : 0, hunt_count);
    } else {
        const char *view_name = "HEX";
        switch (state.view) {
            case MONITOR_VIEW_ASM: view_name = "ASM"; break;
            case MONITOR_VIEW_ASCII: view_name = "ASC"; break;
            case MONITOR_VIEW_SCREEN: view_name = "SCR"; break;
            case MONITOR_VIEW_BINARY: view_name = "BIN"; break;
            default: break;
        }
        if (state.view == MONITOR_VIEW_BINARY) {
            Cursor cursor = active_cursor();
            sprintf(line, "MONITOR %s $%04X/%u", view_name, cursor.address, (unsigned)cursor.bit_index);
        } else {
            sprintf(line, "MONITOR %s $%04X", view_name, state.current_addr);
        }
        if (state.illegal_enabled && state.view == MONITOR_VIEW_ASM) {
            int hp = strlen(line);
            if (hp < (int)sizeof(line) - 9) {
                strcpy(line + hp, "  UndocOp");
            }
        }
    }

    const char *right = NULL;
    if (!help_visible) {
        if (hunt_picker_active) right = "ENTER jumps  ESC exits";
        else if (edit_mode) right = "Edit";
    }
    if (right && (help_visible || hunt_picker_active)) {
        int rlen = strlen(right);
        int cur = strlen(line);
        if (cur > width - rlen - 1) {
            cur = width - rlen - 1;
            if (cur < 0) cur = 0;
            line[cur] = 0;
        }
        int pad = width - cur - rlen;
        for (int i = 0; i < pad && cur < (int)sizeof(line) - 1; i++) {
            line[cur++] = ' ';
        }
        line[cur] = 0;
        strncat(line, right, sizeof(line) - cur - 1);
    } else if (!help_visible && !hunt_picker_active) {
        char fixed[64];
        int first_slot = width;
        int range_slot = width - 18;
        int freeze_slot = width - 11;
        int edit_slot = width - 4;
        if (range_slot < 0) range_slot = width;
        if (freeze_slot < 0) freeze_slot = width;
        if (edit_slot < 0) edit_slot = width;
        if (range_mode && range_slot < first_slot) first_slot = range_slot;
        if (backend && backend->supports_freeze() && backend->is_frozen() && freeze_slot < first_slot) first_slot = freeze_slot;
        if (edit_mode && edit_slot < first_slot) first_slot = edit_slot;

        memset(fixed, ' ', width);
        fixed[width] = 0;
        int left_len = strlen(line);
        if (left_len > first_slot - 1) {
            left_len = first_slot - 1;
        }
        if (left_len < 0) {
            left_len = 0;
        }
        memcpy(fixed, line, left_len);
        if (range_mode && range_slot + 5 <= width) {
            memcpy(fixed + range_slot, "Range", 5);
        }
        if (backend && backend->supports_freeze() && backend->is_frozen() && freeze_slot + 6 <= width) {
            memcpy(fixed + freeze_slot, "Freeze", 6);
        }
        if (edit_mode && edit_slot + 4 <= width) {
            memcpy(fixed + edit_slot, "Edit", 4);
        }
        memcpy(line, fixed, width + 1);
    }
    draw_padded(window, 0, line, strlen(line));
}

void MachineMonitor :: draw_status()
{
    char line[40];
    monitor_format_status_line(line, state.cpu_port, current_vic_bank);
    draw_padded(window, window->get_size_y() - 1, line, strlen(line));
}

void MachineMonitor :: draw_help()
{
    for (int line_idx = 0; line_idx < content_height; line_idx++) {
        const char *text = monitor_help_lines[line_idx];
        if (!text) {
            for (int blank = line_idx; blank < content_height; blank++) {
                draw_padded(window, blank + 1, "", 0);
            }
            break;
        }
        draw_padded(window, line_idx + 1, text, strlen(text));
    }
}

void MachineMonitor :: draw_number_picker()
{
    char lines[5][32];
    char bin[9];
    char hex[3];
    char scr_hex[3];
    char ascii = ascii_byte(number_value);
    char scr = screen_code_char(number_value);

    for (int i = 0; i < 8; i++) {
        bin[i] = (number_value & (uint8_t)(0x80 >> i)) ? '1' : '0';
    }
    bin[8] = 0;
    dump_hex_byte(hex, 0, number_value);
    hex[2] = 0;
    dump_hex_byte(scr_hex, 0, number_value);
    scr_hex[2] = 0;

    sprintf(lines[0], "Hex         $%s", hex);
    sprintf(lines[1], "Decimal     %u", (unsigned)number_value);
    sprintf(lines[2], "Binary      %s", bin);
    sprintf(lines[3], "ASCII       %c", ascii);
    sprintf(lines[4], "Screen      %c ($%s)", scr, scr_hex);

    for (int row = 0; row < content_height; row++) {
        if (row < 5) {
            draw_with_highlight(window, row + 1, lines[row], strlen(lines[row]),
                                row == number_selected ? 0 : -1, strlen(lines[row]));
        } else {
            draw_padded(window, row + 1, "", 0);
        }
    }
}

void MachineMonitor :: draw_hex_row(int y, uint16_t addr, const uint8_t *bytes)
{
    char line[MONITOR_HEX_ROW_CHARS + 1];
    bool mask[MONITOR_HEX_ROW_CHARS];

    monitor_format_hex_row(addr, bytes, line);
    memset(mask, 0, sizeof(mask));
    for (int i = 0; i < MONITOR_HEX_BYTES_PER_ROW; i++) {
        uint16_t cell = (uint16_t)(addr + i);
        if (range_contains(cell)) {
            mask[5 + (3 * i)] = true;
            mask[6 + (3 * i)] = true;
            mask[29 + i] = true;
        }
    }
    if ((!edit_mode || edit_cursor_visible) && state.view == MONITOR_VIEW_HEX && state.current_addr >= addr && state.current_addr < (uint16_t)(addr + MONITOR_HEX_BYTES_PER_ROW)) {
        int index = state.current_addr - addr;
        mask[5 + (3 * index)] = true;
        mask[6 + (3 * index)] = true;
    }
    draw_with_mask(window, y, line, MONITOR_HEX_ROW_CHARS, mask);
}

void MachineMonitor :: draw_text_row(int y, uint16_t addr, const uint8_t *bytes, bool screen_codes)
{
    char line[MONITOR_TEXT_ROW_CHARS + 1];
    bool mask[MONITOR_TEXT_ROW_CHARS];

    monitor_format_text_row(addr, bytes, MONITOR_TEXT_BYTES_PER_ROW, screen_codes, line);
    memset(mask, 0, sizeof(mask));
    for (int i = 0; i < MONITOR_TEXT_BYTES_PER_ROW; i++) {
        if (range_contains((uint16_t)(addr + i))) {
            mask[5 + i] = true;
        }
    }
    if ((!edit_mode || edit_cursor_visible) &&
        ((state.view == MONITOR_VIEW_ASCII && !screen_codes) ||
         (state.view == MONITOR_VIEW_SCREEN && screen_codes)) &&
        state.current_addr >= addr && state.current_addr < (uint16_t)(addr + MONITOR_TEXT_BYTES_PER_ROW)) {
        mask[5 + (state.current_addr - addr)] = true;
    }
    draw_with_mask(window, y, line, MONITOR_TEXT_ROW_CHARS, mask);
}

void MachineMonitor :: draw_hex()
{
    int line_idx;
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_HEX_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_HEX_BYTES_PER_ROW];

        read_row(addr, bytes, MONITOR_HEX_BYTES_PER_ROW);
        if (edit_mode && pending_hex_nibble >= 0 && state.current_addr >= addr && state.current_addr < (uint16_t)(addr + MONITOR_HEX_BYTES_PER_ROW)) {
            int index = state.current_addr - addr;
            bytes[index] = (uint8_t)((pending_hex_nibble << 4) | (bytes[index] & 0x0F));
        }
        draw_hex_row(line_idx + 1, addr, bytes);
    }
}

void MachineMonitor :: draw_ascii()
{
    int line_idx;
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_TEXT_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_TEXT_BYTES_PER_ROW];

        read_row(addr, bytes, MONITOR_TEXT_BYTES_PER_ROW);
        draw_text_row(line_idx + 1, addr, bytes, false);
    }
}

void MachineMonitor :: draw_screen_codes()
{
    int line_idx;
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_TEXT_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_TEXT_BYTES_PER_ROW];

        read_row(addr, bytes, MONITOR_TEXT_BYTES_PER_ROW);
        draw_text_row(line_idx + 1, addr, bytes, true);
    }
}

void MachineMonitor :: draw_binary_row(int y, uint16_t addr, const uint8_t *bytes, int byte_count)
{
    // Format: "AAAA " followed by 8 bits per configured byte (MSB first).
    enum { BIN_LINE_MAX = 4 + 1 + (MONITOR_BINARY_MAX_BYTES_PER_ROW * 8) };
    char line[BIN_LINE_MAX + 1];
    bool mask[BIN_LINE_MAX];
    int bits = byte_count * 8;
    if (bits < 8) bits = 8;
    if (bits > MONITOR_BINARY_MAX_BYTES_PER_ROW * 8) bits = MONITOR_BINARY_MAX_BYTES_PER_ROW * 8;

    memset(line, ' ', sizeof(line) - 1);
    memset(mask, 0, sizeof(mask));
    dump_hex_word(line, 0, addr);
    line[4] = ' ';
    int pos = 5;
    for (int i = 0; i < bits; i++) {
        int byte_off = i / 8;
        int bit_in_byte = 7 - (i % 8);
        if (byte_off >= byte_count) break;
        line[pos++] = ((bytes[byte_off] >> bit_in_byte) & 1) ? '1' : '0';
    }
    int total = pos;
    if (total > (int)sizeof(line) - 1) total = (int)sizeof(line) - 1;
    line[total] = 0;

    for (int i = 0; i < byte_count; i++) {
        if (range_contains((uint16_t)(addr + i))) {
            int first = 5 + (i * 8);
            for (int b = 0; b < 8 && first + b < total; b++) {
                mask[first + b] = true;
            }
        }
    }
    if ((!edit_mode || edit_cursor_visible) &&
        state.view == MONITOR_VIEW_BINARY &&
        state.current_addr >= addr &&
        state.current_addr < (uint16_t)(addr + byte_count)) {
        int byte_offset = state.current_addr - addr;
        uint8_t bit = (binary_bit_index <= 7) ? binary_bit_index : 7;
        int col = byte_offset * 8 + (7 - bit);
        if (col >= 0 && col < bits) {
            mask[5 + col] = true;
        }
    }
    draw_with_mask(window, y, line, total, mask);
}

void MachineMonitor :: draw_binary()
{
    int stride = monitor_binary_byte_stride();
    if (stride < 1) stride = 1;
    uint8_t bytes[MONITOR_BINARY_MAX_BYTES_PER_ROW];
    for (int line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * stride);
        read_row(addr, bytes, (uint16_t)stride);
        draw_binary_row(line_idx + 1, addr, bytes, stride);
    }
}

void MachineMonitor :: redraw_full()
{
    // Re-paint everything after a sub-dialog (file picker, popup, confirmation)
    // closed. We must also restore the screen title bar / separator rows that
    // sit OUTSIDE our window (rows 0/1 and the bottom row), otherwise the user
    // sees a "shrunken" monitor with the C64 chrome wiped away.
    //
    // Window::draw_border() permanently shrinks the window's effective area by
    // one character on each side (it advances offset_x/y and reduces
    // window_x/y by 2). So before redrawing the border we must reset the
    // geometry, otherwise each popup nibbles another row+column off the
    // monitor frame.
    if (!window || !screen) return;
    get_ui()->set_screen_title();
    window->reset_border();
    window->draw_border();
    window->set_color(get_ui()->color_fg);
    draw();
}

void MachineMonitor :: draw_disassembly()
{
    uint16_t addr = state.base_addr;
    int line_idx;
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        Disassembled6502 decoded;
        char line[MONITOR_DISASM_ROW_CHARS + 1];
        uint8_t row_bytes[3];
        int highlight = -1;
        int text_limit;
        int text_len;
        const char *source;
        int source_len;
        int source_pos;

        read_row(addr, row_bytes, 3);
        memset(line, ' ', sizeof(line));
        line[MONITOR_DISASM_ROW_CHARS] = 0;
        disassemble_6502(addr, row_bytes, state.illegal_enabled, &decoded);
        dump_hex_word(line, 0, addr);
        line[4] = ' ';
        dump_hex_byte(line, 5, row_bytes[0]);
        if (decoded.length >= 2) {
            dump_hex_byte(line, 8, row_bytes[1]);
        }
        if (decoded.length >= 3) {
            dump_hex_byte(line, 11, row_bytes[2]);
        }
        line[13] = ' ';
        line[14] = ' ';

        text_limit = MONITOR_DISASM_SOURCE_COL - 1 - MONITOR_DISASM_TEXT_COL;
        text_len = (int)strlen(decoded.text);
        if (text_len > text_limit) {
            text_len = text_limit;
        }
        memcpy(line + MONITOR_DISASM_TEXT_COL, decoded.text, text_len);

        source = backend->source_name(addr);
        source_len = (int)strlen(source);
        if (source_len > MONITOR_DISASM_ROW_CHARS - MONITOR_DISASM_SOURCE_COL - 2) {
            source_len = MONITOR_DISASM_ROW_CHARS - MONITOR_DISASM_SOURCE_COL - 2;
        }
        source_pos = MONITOR_DISASM_ROW_CHARS - source_len - 2;
        line[source_pos] = '[';
        memcpy(line + source_pos + 1, source, source_len);
        line[source_pos + 1 + source_len] = ']';

        if (state.current_addr >= addr && state.current_addr < (uint16_t)(addr + decoded.length)) {
            highlight = 0;
        }
        if (range_mode) {
            uint8_t len = decoded.length ? decoded.length : 1;
            for (uint8_t i = 0; i < len; i++) {
                if (range_contains((uint16_t)(addr + i))) {
                    highlight = 0;
                    break;
                }
            }
        }
        int hl_x = 0;
        int hl_len = MONITOR_DISASM_ROW_CHARS;
        if (highlight == 0 && edit_mode && state.view == MONITOR_VIEW_ASM) {
            uint8_t part = asm_edit_part;
            uint8_t part_count = asm_edit_part_count(addr);
            if (part >= part_count) part = 0;
            // All asm-edit highlights live inside the decoded text region
            // (never on the raw byte columns to the left).
            if (part == 0) {
                int mnem_len = 0;
                while (mnem_len < text_len && decoded.text[mnem_len] != ' ') mnem_len++;
                if (mnem_len == 0) mnem_len = (text_len > 0) ? text_len : 1;
                hl_x = MONITOR_DISASM_TEXT_COL;
                hl_len = mnem_len;
            } else {
                // Locate the operand hex digits inside decoded.text.
                int dollar = -1;
                for (int i = 0; i < text_len; i++) {
                    if (decoded.text[i] == '$') { dollar = i; break; }
                }
                int hexstart = (dollar >= 0) ? dollar + 1 : -1;
                int hexcount = 0;
                while (hexstart >= 0 && hexstart + hexcount < text_len) {
                    char c = decoded.text[hexstart + hexcount];
                    bool digit = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
                    if (!digit) break;
                    hexcount++;
                }
                bool branch = (decoded.length == 2) && asm_is_branch(addr);
                if (hexstart >= 0 && hexcount >= 2) {
                    if ((decoded.length == 3 || branch) && hexcount >= 4) {
                        // part 1 -> first 2 hex chars (high byte, displayed first)
                        // part 2 -> next  2 hex chars (low  byte, displayed second)
                        int off = (part == 1) ? 0 : 2;
                        hl_x = MONITOR_DISASM_TEXT_COL + hexstart + off;
                        hl_len = 2;
                    } else {
                        hl_x = MONITOR_DISASM_TEXT_COL + hexstart;
                        hl_len = 2;
                    }
                } else {
                    // Fallback: highlight the mnemonic to avoid flashing the byte column.
                    int mnem_len = 0;
                    while (mnem_len < text_len && decoded.text[mnem_len] != ' ') mnem_len++;
                    if (mnem_len == 0) mnem_len = 1;
                    hl_x = MONITOR_DISASM_TEXT_COL;
                    hl_len = mnem_len;
                }
            }
        }
        draw_with_highlight(window, line_idx + 1, line, MONITOR_DISASM_ROW_CHARS, highlight < 0 ? -1 : hl_x, hl_len);
        addr = (uint16_t)(addr + decoded.length);
    }
}

void MachineMonitor :: draw_hunt_picker()
{
    if (!window) {
        return;
    }
    int rows = content_height;
    if (rows <= 0) {
        return;
    }
    if (hunt_selected < hunt_top) {
        hunt_top = hunt_selected;
    } else if (hunt_selected >= hunt_top + rows) {
        hunt_top = hunt_selected - rows + 1;
    }
    if (hunt_top < 0) {
        hunt_top = 0;
    }
    int width = window->get_size_x();
    char line[MONITOR_HEX_ROW_CHARS + 1];
    for (int r = 0; r < rows; r++) {
        int idx = hunt_top + r;
        memset(line, ' ', sizeof(line));
        line[MONITOR_HEX_ROW_CHARS] = 0;
        int draw_len = MONITOR_HEX_ROW_CHARS;
        if (idx < hunt_count) {
            uint8_t bytes[MONITOR_HEX_BYTES_PER_ROW];
            read_row(hunt_addrs[idx], bytes, MONITOR_HEX_BYTES_PER_ROW);
            monitor_format_hex_row(hunt_addrs[idx], bytes, line);
        } else {
            draw_padded(window, r + 1, line, 0);
            continue;
        }
        if (draw_len > width) draw_len = width;
        int hl = (idx == hunt_selected) ? 0 : -1;
        draw_with_highlight(window, r + 1, line, draw_len, hl, draw_len);
    }
}

void MachineMonitor :: draw_opcode_picker()
{
    if (!window) return;
    int width = window->get_size_x();
    // Anchor the overlay to the row that holds the current_addr in the
    // disassembly view; if not visible, fall back to the top row.
    int anchor_row = 1;
    {
        uint16_t addr = state.base_addr;
        for (int i = 0; i < content_height; i++) {
            uint8_t l = disasm_length(addr);
            if (l == 0) l = 1;
            if (state.current_addr >= addr && state.current_addr < (uint16_t)(addr + l)) {
                anchor_row = i + 1;
                break;
            }
            addr = (uint16_t)(addr + l);
        }
    }
    // Box width: 18 chars (template up to 13 + small margin). Try to right-align
    // beside the disassembly text.
    int box_w = 18;
    if (box_w > width - 2) box_w = width - 2;
    int box_x = MONITOR_DISASM_TEXT_COL;
    if (box_x + box_w > width) box_x = width - box_w;
    if (box_x < 0) box_x = 0;

    int max_rows = window->get_size_y() - 2 - anchor_row;
    if (max_rows < 3) max_rows = 3;
    int visible = opcode_candidate_count;
    if (visible > max_rows - 1) visible = max_rows - 1;
    if (visible < 1) visible = 1;

    // Adjust scroll window.
    if (opcode_selected < opcode_top) opcode_top = opcode_selected;
    if (opcode_selected >= opcode_top + visible) opcode_top = opcode_selected - visible + 1;
    if (opcode_top < 0) opcode_top = 0;

    char line[40];
    // Header row: typed prefix (or "_" if empty) + operand buffer + match count.
    memset(line, ' ', sizeof(line));
    line[box_w] = 0;
    char prefix_show[6];
    int n = 0;
    for (; n < opcode_prefix_len && n < 3; n++) prefix_show[n] = opcode_prefix[n];
    prefix_show[n++] = '_';
    prefix_show[n] = 0;
    if (opcode_operand_len > 0) {
        sprintf(line, " %s %s %d", prefix_show, opcode_operand, opcode_candidate_count);
    } else {
        sprintf(line, " %s %d", prefix_show, opcode_candidate_count);
    }
    int len = (int)strlen(line);
    if (len < box_w) { memset(line + len, ' ', box_w - len); line[box_w] = 0; }
    window->move_cursor(box_x, anchor_row);
    window->reverse_mode(1);
    window->output_length(line, box_w);
    window->reverse_mode(0);

    // Candidate rows.
    for (int r = 0; r < visible; r++) {
        int idx = opcode_top + r;
        memset(line, ' ', sizeof(line));
        line[box_w] = 0;
        if (idx < opcode_candidate_count) {
            uint8_t op = opcode_candidates[idx];
            const char *templ = disassembler_6502_template(op);
            char buf[16];
            // Copy template up to 13 chars, replacing leading '*' on the
            // mnemonic with a space.
            for (int i = 0; i < 13 && i < (int)sizeof(buf) - 1 && templ && templ[i]; i++) {
                buf[i] = (templ[i] == '*') ? ' ' : templ[i];
                buf[i + 1] = 0;
            }
            // Canonicalise mnemonic (HLT->JAM, etc.).
            char mnem[4];
            mnem[0] = buf[0]; mnem[1] = buf[1]; mnem[2] = buf[2]; mnem[3] = 0;
            if (op == 0x5C || op == 0x7C) strcpy(mnem, "NOP");
            else if (!strcmp(mnem, "HLT")) strcpy(mnem, "JAM");
            else if (!strcmp(mnem, "ASO")) strcpy(mnem, "SLO");
            else if (!strcmp(mnem, "LSE")) strcpy(mnem, "SRE");
            else if (!strcmp(mnem, "DCM")) strcpy(mnem, "DCP");
            else if (!strcmp(mnem, "INS")) strcpy(mnem, "ISC");
            buf[0] = mnem[0]; buf[1] = mnem[1]; buf[2] = mnem[2];
            sprintf(line, " %s", buf);
            int slen = (int)strlen(line);
            if (slen < box_w) { memset(line + slen, ' ', box_w - slen); line[box_w] = 0; }
        }
        window->move_cursor(box_x, anchor_row + 1 + r);
        if (idx == opcode_selected && idx < opcode_candidate_count) {
            window->reverse_mode(1);
            window->output_length(line, box_w);
            window->reverse_mode(0);
        } else {
            window->output_length(line, box_w);
        }
    }
}

void MachineMonitor :: hunt_picker_open(int count)
{
    hunt_picker_open_labeled(count, "Hunt results");
}

void MachineMonitor :: hunt_picker_open_labeled(int count, const char *label)
{
    hunt_count = count;
    if (hunt_count > (int)(sizeof(hunt_addrs) / sizeof(hunt_addrs[0]))) {
        hunt_count = (int)(sizeof(hunt_addrs) / sizeof(hunt_addrs[0]));
    }
    hunt_selected = 0;
    hunt_top = 0;
    hunt_picker_label = label ? label : "Results";
    hunt_picker_active = true;
}

void MachineMonitor :: hunt_picker_close()
{
    hunt_picker_active = false;
}

void MachineMonitor :: hunt_picker_jump()
{
    if (hunt_count <= 0 || hunt_selected < 0 || hunt_selected >= hunt_count) {
        return;
    }
    monitor_apply_goto(&state, hunt_addrs[hunt_selected]);
    hunt_picker_close();
}

int MachineMonitor :: hunt_picker_handle_key(int key)
{
    if (key == KEY_ESCAPE || key == KEY_BREAK || key == KEY_CTRL_O) {
        hunt_picker_close();
        draw();
        return (key == KEY_CTRL_O) ? 1 : 0;
    }
    if (key == KEY_RETURN) {
        hunt_picker_jump();
        draw();
        return 0;
    }
    if (key == KEY_DOWN) {
        if (hunt_selected + 1 < hunt_count) hunt_selected++;
    } else if (key == KEY_UP) {
        if (hunt_selected > 0) hunt_selected--;
    } else if (key == KEY_PAGEDOWN) {
        hunt_selected += content_height > 0 ? content_height : 1;
        if (hunt_selected >= hunt_count) hunt_selected = hunt_count - 1;
    } else if (key == KEY_PAGEUP) {
        hunt_selected -= content_height > 0 ? content_height : 1;
        if (hunt_selected < 0) hunt_selected = 0;
    } else if (key == KEY_HOME) {
        hunt_selected = 0;
    } else if (key == KEY_END) {
        hunt_selected = hunt_count - 1;
    }
    draw();
    return 0;
}

void MachineMonitor :: draw()
{
    draw_header();
    if (help_visible) {
        draw_help();
    } else if (number_picker_active) {
        draw_number_picker();
    } else if (hunt_picker_active) {
        draw_hunt_picker();
    } else {
        switch (state.view) {
            case MONITOR_VIEW_DISASM:
                draw_disassembly();
                break;
            case MONITOR_VIEW_ASCII:
                draw_ascii();
                break;
            case MONITOR_VIEW_SCREEN:
                draw_screen_codes();
                break;
            case MONITOR_VIEW_BINARY:
                draw_binary();
                break;
            default:
                draw_hex();
                break;
        }
        if (opcode_picker_active) {
            draw_opcode_picker();
        }
    }
    draw_status();
    if (screen) {
        screen->sync();
    }
}

void MachineMonitor :: apply_hex_digit(uint8_t value)
{
    if (pending_hex_nibble < 0) {
        pending_hex_nibble = (int)value;
        reset_edit_blink();
    } else {
        uint8_t byte = (uint8_t)((pending_hex_nibble << 4) | value);
        canonical_write(state.current_addr, byte);
        move_current(1);
        pending_hex_nibble = -1;
    }
}

void MachineMonitor :: apply_ascii_char(char value)
{
    canonical_write(state.current_addr, (uint8_t)value);
    move_current(1);
}

void MachineMonitor :: apply_screen_char(char value)
{
    uint8_t code = monitor_screen_code_for_char(value);
    if (code == 0xFF) {
        return;
    }
    canonical_write(state.current_addr, code);
    move_current(1);
}

void MachineMonitor :: binary_apply_bit(uint8_t bit_value)
{
    if (binary_bit_index > 7) binary_bit_index = 7;
    uint8_t byte = canonical_read(state.current_addr);
    uint8_t mask = (uint8_t)(1u << binary_bit_index);
    if (bit_value) byte |= mask;
    else           byte &= (uint8_t)~mask;
    canonical_write(state.current_addr, byte);
    move_binary_bits(1);
    reset_edit_blink();
}

// Unified DEL behaviour shared by edit and non-edit mode. ASC/SCR clear
// the current cell to a space and step the cursor LEFT; HEX clears the
// current byte to 0x00 and steps RIGHT; ASM replaces the current
// instruction with NOP(s) and steps to the next disassembled line.
void MachineMonitor :: apply_logical_delete()
{
    switch (state.view) {
        case MONITOR_VIEW_HEX: {
            canonical_write(state.current_addr, 0x00);
            pending_hex_nibble = -1;
            move_current(1);
            break;
        }
        case MONITOR_VIEW_ASCII: {
            canonical_write(state.current_addr, 0x20);
            move_current(-1);
            break;
        }
        case MONITOR_VIEW_SCREEN: {
            canonical_write(state.current_addr, 0x20);
            move_current(-1);
            break;
        }
        case MONITOR_VIEW_ASM: {
            uint8_t len = disasm_length(state.current_addr);
            for (uint8_t i = 0; i < len; i++) {
                canonical_write((uint16_t)(state.current_addr + i), 0xEA);
            }
            step_disassembly(1);
            break;
        }
        default:
            break;
    }
}

void MachineMonitor :: asm_edit_history_reset(uint16_t anchor_addr)
{
    asm_edit_hist_len = 0;
    asm_edit_hist_anchor_addr = anchor_addr;
}

void MachineMonitor :: asm_edit_history_push(uint16_t addr, uint8_t prev_byte, uint8_t prev_part, uint8_t prev_pending)
{
    if (asm_edit_hist_len >= ASM_EDIT_HISTORY_MAX) {
        // Drop the oldest entry to make room (FIFO eviction). The user's
        // expected DEL undoes recent edits; very long edit chains beyond the
        // buffer simply cannot be undone by the buffer entry that fell off.
        for (int i = 1; i < ASM_EDIT_HISTORY_MAX; i++) {
            asm_edit_hist_addr[i - 1]    = asm_edit_hist_addr[i];
            asm_edit_hist_byte[i - 1]    = asm_edit_hist_byte[i];
            asm_edit_hist_part[i - 1]    = asm_edit_hist_part[i];
            asm_edit_hist_pending[i - 1] = asm_edit_hist_pending[i];
        }
        asm_edit_hist_len = ASM_EDIT_HISTORY_MAX - 1;
    }
    asm_edit_hist_addr[asm_edit_hist_len]    = addr;
    asm_edit_hist_byte[asm_edit_hist_len]    = prev_byte;
    asm_edit_hist_part[asm_edit_hist_len]    = prev_part;
    asm_edit_hist_pending[asm_edit_hist_len] = prev_pending;
    asm_edit_hist_len++;
}

bool MachineMonitor :: asm_edit_history_pop()
{
    if (asm_edit_hist_len <= 0) return false;
    asm_edit_hist_len--;
    uint16_t addr = asm_edit_hist_addr[asm_edit_hist_len];
    uint8_t  byte = asm_edit_hist_byte[asm_edit_hist_len];
    canonical_write(addr, byte);
    state.current_addr = asm_edit_hist_anchor_addr;
    asm_edit_part      = asm_edit_hist_part[asm_edit_hist_len];
    asm_edit_pending   = asm_edit_hist_pending[asm_edit_hist_len];
    return true;
}

// Direct-typing path in the opcode picker uses the existing assembler so it
// accepts the same syntax as monitor_assemble_line ("LDA $1000", "LDA #$FF",
// "JMP ($1000)", "LDA ($10),Y", etc.). The picker collects the operand chars
// after the 3-letter mnemonic into opcode_operand[] and on commit asks the
// assembler to turn the full "MNEM operand" string into bytes.

void MachineMonitor :: opcode_picker_open(char seed)
{
    opcode_picker_active = true;
    opcode_prefix_len = 0;
    opcode_prefix[0] = 0;
    opcode_operand_len = 0;
    opcode_operand[0] = 0;
    if (seed) {
        char up = (seed >= 'a' && seed <= 'z') ? (char)(seed - 'a' + 'A') : seed;
        if ((up >= 'A' && up <= 'Z')) {
            opcode_prefix[0] = up;
            opcode_prefix[1] = 0;
            opcode_prefix_len = 1;
        }
    }
    opcode_selected = 0;
    opcode_top = 0;
    opcode_picker_refilter();
}

void MachineMonitor :: opcode_picker_close()
{
    opcode_picker_active = false;
    opcode_prefix_len = 0;
    opcode_prefix[0] = 0;
    opcode_operand_len = 0;
    opcode_operand[0] = 0;
}

void MachineMonitor :: opcode_picker_refilter()
{
    opcode_candidate_count = 0;
    for (int op = 0; op < 256; op++) {
        const char *templ = disassembler_6502_template((uint8_t)op);
        if (!templ) continue;
        bool illegal = (templ[3] == '*');
        if (illegal && !state.illegal_enabled) continue;
        char mnem[4];
        mnem[0] = templ[0]; mnem[1] = templ[1]; mnem[2] = templ[2]; mnem[3] = 0;
        // Apply canonicalisation so the picker shows the same names the assembler accepts.
        if (op == 0x5C || op == 0x7C) strcpy(mnem, "NOP");
        else if (!strcmp(mnem, "HLT")) strcpy(mnem, "JAM");
        else if (!strcmp(mnem, "ASO")) strcpy(mnem, "SLO");
        else if (!strcmp(mnem, "LSE")) strcpy(mnem, "SRE");
        else if (!strcmp(mnem, "DCM")) strcpy(mnem, "DCP");
        else if (!strcmp(mnem, "INS")) strcpy(mnem, "ISC");
        bool match = true;
        for (int i = 0; i < opcode_prefix_len; i++) {
            if (mnem[i] != opcode_prefix[i]) { match = false; break; }
        }
        if (!match) continue;
        if (opcode_candidate_count < (int)sizeof(opcode_candidates)) {
            opcode_candidates[opcode_candidate_count++] = (uint8_t)op;
        }
    }
    if (opcode_selected >= opcode_candidate_count) opcode_selected = opcode_candidate_count - 1;
    if (opcode_selected < 0) opcode_selected = 0;
    if (opcode_top > opcode_selected) opcode_top = opcode_selected;
}

void MachineMonitor :: opcode_picker_commit()
{
    if (opcode_candidate_count <= 0) return;
    if (opcode_selected < 0 || opcode_selected >= opcode_candidate_count) return;
    uint8_t op = opcode_candidates[opcode_selected];
    // Capture the entire previous instruction (up to 3 bytes) so DEL can
    // restore the original line wholesale. Push in reverse memory order so
    // popping replays the bytes back in low->high order — but order of
    // restoration does not matter since each entry is independent.
    uint8_t prev_len = disasm_length(state.current_addr);
    if (prev_len == 0) prev_len = 1;
    if (prev_len > 3) prev_len = 3;
    // Snapshot anchor part/pending only on the first entry; all subsequent
    // bytes are tagged with part 0 / pending 0 — they're just memory restores
    // and the cursor state will be set by the LAST popped (i.e., FIRST pushed)
    // entry, which is the highest-offset byte. To keep it simple: only the
    // first push (the opcode byte) carries the pre-commit cursor state.
    asm_edit_history_push(state.current_addr, canonical_read(state.current_addr), asm_edit_part, asm_edit_pending);
    for (uint8_t i = 1; i < prev_len; i++) {
        uint16_t a = (uint16_t)(state.current_addr + i);
        asm_edit_history_push(a, canonical_read(a), 0, 0);
    }
    canonical_write(state.current_addr, op);
    uint8_t new_len = disasm_length(state.current_addr);
    if (new_len == 0) new_len = 1;
    opcode_picker_close();
    if (new_len > 1) {
        // Leave the cursor on the (new) operand byte 1 so the user can type
        // hex digits into it next.
        asm_edit_part = 1;
        asm_edit_pending = 0;
    } else {
        // No operand: advance to next instruction.
        step_disassembly(1);
        asm_edit_part = 0;
        asm_edit_pending = 0;
        asm_edit_history_reset(state.current_addr);
    }
    ensure_disasm_visible();
}

int MachineMonitor :: opcode_picker_handle_key(int key)
{
    if (key == KEY_ESCAPE || key == KEY_BREAK) {
        opcode_picker_close();
        draw();
        return 0;
    }
    if (key == KEY_CTRL_O) {
        opcode_picker_close();
        return 1;
    }
    if (key == KEY_RETURN) {
        if (opcode_operand_len > 0) {
            if (opcode_picker_commit_typed()) {
                draw();
                return 0;
            }
        }
        opcode_picker_commit();
        draw();
        return 0;
    }
    if (key == KEY_DOWN) {
        if (opcode_selected + 1 < opcode_candidate_count) opcode_selected++;
        draw();
        return 0;
    }
    if (key == KEY_UP) {
        if (opcode_selected > 0) opcode_selected--;
        draw();
        return 0;
    }
    if (key == KEY_BACK || key == KEY_DELETE) {
        if (opcode_operand_len > 0) {
            opcode_operand_len--;
            opcode_operand[opcode_operand_len] = 0;
        } else if (opcode_prefix_len > 0) {
            opcode_prefix_len--;
            opcode_prefix[opcode_prefix_len] = 0;
            opcode_selected = 0;
            opcode_top = 0;
            opcode_picker_refilter();
        } else {
            opcode_picker_close();
        }
        draw();
        return 0;
    }

    // Mnemonic accumulation: while we have fewer than 3 letters, treat any
    // letter (including A-F) as part of the mnemonic prefix. Once the prefix
    // is complete the picker switches to operand-typing mode.
    bool is_letter = ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z'));
    if (is_letter && opcode_prefix_len < 3) {
        char up = (key >= 'a' && key <= 'z') ? (char)(key - 'a' + 'A') : (char)key;
        opcode_prefix[opcode_prefix_len++] = up;
        opcode_prefix[opcode_prefix_len] = 0;
        opcode_selected = 0;
        opcode_top = 0;
        opcode_picker_refilter();
        // Auto-commit only for implied/accumulator instructions where the
        // operand is irrelevant (e.g. BRK, NOP, TXA). For everything else
        // wait for the user to type an operand or press ENTER.
        if (opcode_candidate_count == 1 && opcode_prefix_len == 3) {
            uint8_t op = opcode_candidates[0];
            uint8_t op_len = disasm_length(state.current_addr);
            (void)op_len;
            // disasm_length depends on memory; check the candidate's own length
            // by looking up the implied/accumulator forms via the assembler.
            uint8_t lookup_op, lookup_len;
            char m[4]; m[0] = opcode_prefix[0]; m[1] = opcode_prefix[1]; m[2] = opcode_prefix[2]; m[3] = 0;
            bool is_imp = monitor_lookup_opcode(m, AM_IMP, state.illegal_enabled, &lookup_op, &lookup_len) && lookup_op == op;
            bool is_acc = monitor_lookup_opcode(m, AM_ACC, state.illegal_enabled, &lookup_op, &lookup_len) && lookup_op == op;
            if (is_imp || is_acc) {
                opcode_picker_commit();
            }
        }
        draw();
        return 0;
    }

    // Operand-typing mode: only available once the mnemonic is complete.
    if (opcode_prefix_len == 3) {
        if (key == ' ') {
            if (opcode_operand_len > 0 && opcode_picker_commit_typed()) {
                draw();
                return 0;
            }
            // SPACE before any operand chars is a no-op (allows "LDA 1000").
            return 0;
        }
        char up = (key >= 'a' && key <= 'z') ? (char)(key - 'a' + 'A') : (char)key;
        bool is_hex   = ((up >= '0' && up <= '9') || (up >= 'A' && up <= 'F'));
        bool is_xy    = (up == 'X' || up == 'Y');
        bool is_punct = (up == '#' || up == '$' || up == '(' || up == ')' || up == ',');
        if (is_hex || is_xy || is_punct) {
            if (opcode_operand_len < (int)sizeof(opcode_operand) - 1) {
                opcode_operand[opcode_operand_len++] = up;
                opcode_operand[opcode_operand_len] = 0;
            }
            draw();
            return 0;
        }
    }
    return 0;
}

bool MachineMonitor :: opcode_picker_commit_typed()
{
    if (opcode_prefix_len != 3) return false;
    if (opcode_operand_len <= 0) return false;

    // Build "MNEM operand" string and ask the existing assembler to encode it.
    char line[24];
    line[0] = opcode_prefix[0];
    line[1] = opcode_prefix[1];
    line[2] = opcode_prefix[2];
    line[3] = ' ';
    int n = 4;
    for (int i = 0; i < opcode_operand_len && n < (int)sizeof(line) - 1; i++) {
        line[n++] = opcode_operand[i];
    }
    line[n] = 0;

    AsmInsn insn;
    MonitorError err = MONITOR_OK;
    if (!monitor_assemble_line(line, state.illegal_enabled, state.current_addr, &insn, &err)) {
        return false;
    }
    if (insn.length < 1 || insn.length > 3) return false;

    // Capture undo history for the whole previous instruction (up to 3 bytes).
    uint8_t prev_len = disasm_length(state.current_addr);
    if (prev_len == 0) prev_len = 1;
    if (prev_len > 3) prev_len = 3;
    asm_edit_history_push(state.current_addr,
                          canonical_read(state.current_addr),
                          asm_edit_part, asm_edit_pending);
    for (uint8_t i = 1; i < prev_len; i++) {
        uint16_t a = (uint16_t)(state.current_addr + i);
        asm_edit_history_push(a, canonical_read(a), 0, 0);
    }

    for (uint8_t i = 0; i < insn.length; i++) {
        canonical_write((uint16_t)(state.current_addr + i), insn.bytes[i]);
    }

    opcode_picker_close();
    step_disassembly(1);
    asm_edit_part = 0;
    asm_edit_pending = 0;
    asm_edit_history_reset(state.current_addr);
    ensure_disasm_visible();
    return true;
}

void MachineMonitor :: exit_edit_mode()
{
    edit_mode = false;
    edit_cursor_visible = true;
    pending_hex_nibble = -1;
    asm_edit_part = 0;
    asm_edit_pending = 0;
    asm_edit_history_reset(state.current_addr);
}

void MachineMonitor :: enter_edit_mode()
{
    help_visible = false;
    edit_mode = true;
    pending_hex_nibble = -1;
    asm_edit_part = 0;
    asm_edit_pending = 0;
    asm_edit_history_reset(state.current_addr);
    reset_edit_blink();
}

void MachineMonitor :: init(Screen *scr, Keyboard *keyb)
{
    int line_length;
    int height;

    screen = scr;
    keyboard = keyb;

    line_length = screen->get_size_x();
    height = screen->get_size_y() - 3;

    window = new Window(screen, 0, 2, line_length, height);
    window->draw_border();
    window->set_color(get_ui()->color_fg);
    content_height = window->get_size_y() - 2;
    backend->begin_session();
    state.cpu_port = normalize_cpu_mode(state.cpu_port);
    backend->set_monitor_cpu_port(state.cpu_port);
    current_vic_bank = backend->get_live_vic_bank();
    last_live_vic_bank = current_vic_bank;
    vic_bank_override = false;
    monitor_apply_goto(&state, state.current_addr);
    draw();
}

void MachineMonitor :: deinit(void)
{
    monitor_saved_state.view = state.view;
    monitor_saved_state.current_addr = state.current_addr;
    monitor_saved_state.base_addr = state.base_addr;
    monitor_saved_state.disasm_offset = 0;
    monitor_saved_state.illegal_enabled = state.illegal_enabled;
    monitor_saved_state.cpu_port = state.cpu_port;
    monitor_saved_state_valid = true;
    backend->end_session();
    if (window) {
        delete window;
        window = NULL;
    }
}

int MachineMonitor :: handle_key(int key)
{
    char buffer[96];
    char output[4096];
    uint16_t start, end, dest, address, value16;
    uint8_t byte_value, needle[80];
    int needle_len;
    MonitorError error;

    if (hunt_picker_active) {
        return hunt_picker_handle_key(key);
    }
    if (opcode_picker_active) {
        return opcode_picker_handle_key(key);
    }
    if (number_picker_active) {
        return number_picker_handle_key(key);
    }

    if (key == KEY_HELP || key == KEY_F3) {
        toggle_help();
        draw();
        return 0;
    }
    if (help_visible) {
        // Any other key dismisses the help overlay first, then continues to
        // execute as a normal monitor command (so e.g. pressing H from the
        // help screen actually launches Hunt instead of leaving help open).
        help_visible = false;
        if (key == KEY_ESCAPE) {
            draw();
            return 0;
        }
    }

    if (key == KEY_CTRL_C) {
        bool copied;
        if (range_mode) {
            copied = clipboard_copy_range();
            if (copied) {
                range_mode = false;
            }
        } else {
            copied = clipboard_copy_current();
        }
        if (!copied) {
            get_ui()->popup("?MEM", BUTTON_OK);
            redraw_full();
        }
        draw();
        return 0;
    }
    if (key == KEY_CTRL_V) {
        if (!clipboard_paste()) {
            get_ui()->popup("?CLIP", BUTTON_OK);
            redraw_full();
        }
        draw();
        return 0;
    }
    if (key == 'r' || key == 'R') {
        help_visible = false;
        toggle_range_mode();
        draw();
        return 0;
    }
    if (key == 'n' || key == 'N') {
        open_number_picker();
        draw();
        return 0;
    }

    if (edit_mode) {
        if (key == KEY_CTRL_O) {
            return 1;
        }
        if (key == KEY_BREAK || key == KEY_ESCAPE || key == KEY_CTRL_E) {
            exit_edit_mode();
            draw();
            return 0;
        }
        if (key == KEY_BACK || key == KEY_DELETE) {
            // ASM: undo the last typed character on this line first; only
            // when nothing has been typed on the current instruction does
            // DEL fall through to the unified logical delete (NOP-replace
            // and advance to the next line).
            if (state.view == MONITOR_VIEW_ASM && asm_edit_history_pop()) {
                draw();
                return 0;
            }
            // HEX: a half-typed nibble on the current byte is rolled back
            // first; otherwise the byte itself is cleared.
            if (state.view == MONITOR_VIEW_HEX && pending_hex_nibble >= 0) {
                pending_hex_nibble = -1;
                draw();
                return 0;
            }
            // BINARY: walk the bit cursor backwards across bytes so the user
            // can flip an individual bit without zeroing whole bytes.
            if (state.view == MONITOR_VIEW_BINARY) {
                move_binary_bits(-1);
                reset_edit_blink();
                draw();
                return 0;
            }
            apply_logical_delete();
            if (state.view == MONITOR_VIEW_ASM) {
                asm_edit_history_reset(state.current_addr);
            }
            draw();
            return 0;
        }
        if (state.view == MONITOR_VIEW_BINARY) {
            // Sprite/character editing input mapping. `*` and `1` set a bit;
            // `.`, `0`, and SPACE clear a bit. Anything else is ignored.
            if (key == '1' || key == '*') {
                binary_apply_bit(1);
                draw();
                return 0;
            }
            if (key == '0' || key == '.' || key == ' ') {
                binary_apply_bit(0);
                draw();
                return 0;
            }
        }
        if (state.view == MONITOR_VIEW_HEX && is_hex_char((char)key)) {
            if (key >= '0' && key <= '9') {
                apply_hex_digit((uint8_t)(key - '0'));
            } else {
                apply_hex_digit((uint8_t)(to_upper_char((char)key) - 'A' + 10));
            }
            draw();
            return 0;
        }
        if (state.view == MONITOR_VIEW_ASCII) {
            if (key >= 32 && key < 127) {
                char ch = (char)key;
                if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                apply_ascii_char(ch);
                draw();
                return 0;
            }
        }
        if (state.view == MONITOR_VIEW_SCREEN) {
            if (key >= 32 && key < 127) {
                char ch = (char)key;
                if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                apply_screen_char(ch);
                draw();
                return 0;
            }
        }
        if (state.view == MONITOR_VIEW_ASM) {
            uint8_t insn_len = disasm_length(state.current_addr);
            if (insn_len == 0) insn_len = 1;
            uint8_t part_count = asm_edit_part_count(state.current_addr);
            bool branch = asm_is_branch(state.current_addr) && insn_len == 2;
            if (asm_edit_part >= part_count) asm_edit_part = 0;
            if (key == KEY_LEFT) {
                if (asm_edit_part > 0) {
                    asm_edit_part--;
                } else {
                    step_disassembly(-1);
                    uint8_t prev_parts = asm_edit_part_count(state.current_addr);
                    asm_edit_part = (prev_parts > 0) ? (uint8_t)(prev_parts - 1) : 0;
                    asm_edit_history_reset(state.current_addr);
                }
                asm_edit_pending = 0;
                draw();
                return 0;
            }
            if (key == KEY_RIGHT) {
                if (asm_edit_part + 1 < part_count) {
                    asm_edit_part++;
                } else {
                    step_disassembly(1);
                    asm_edit_part = 0;
                    asm_edit_history_reset(state.current_addr);
                }
                asm_edit_pending = 0;
                draw();
                return 0;
            }
            if (key == KEY_UP) {
                step_disassembly(-1);
                asm_edit_part = 0;
                asm_edit_pending = 0;
                asm_edit_history_reset(state.current_addr);
                draw();
                return 0;
            }
            if (key == KEY_DOWN) {
                step_disassembly(1);
                asm_edit_part = 0;
                asm_edit_pending = 0;
                asm_edit_history_reset(state.current_addr);
                draw();
                return 0;
            }
            if (asm_edit_part > 0 && is_hex_char((char)key)) {
                uint8_t nib = (key >= '0' && key <= '9') ? (uint8_t)(key - '0')
                                                        : (uint8_t)(to_upper_char((char)key) - 'A' + 10);
                uint8_t prev_part = asm_edit_part;
                uint8_t prev_pending = asm_edit_pending;
                if (branch) {
                    // Branch: virtual two-byte target ($hh$ll) backed by a
                    // single signed offset at mem+1. Compute the new target
                    // byte from the typed nibble, derive the new offset, and
                    // only write if it still fits in -128..127.
                    uint16_t rel_addr = (uint16_t)(state.current_addr + 1);
                    uint8_t prev_rel = canonical_read(rel_addr);
                    uint16_t target = (uint16_t)(state.current_addr + 2 + (int8_t)prev_rel);
                    uint8_t hi = (uint8_t)(target >> 8);
                    uint8_t lo = (uint8_t)(target & 0xFF);
                    uint8_t *byte = (asm_edit_part == 1) ? &hi : &lo;
                    if (asm_edit_pending == 0) {
                        *byte = (uint8_t)((nib << 4) | (*byte & 0x0F));
                    } else {
                        *byte = (uint8_t)((*byte & 0xF0) | nib);
                    }
                    uint16_t new_target = (uint16_t)((hi << 8) | lo);
                    int32_t offset = (int32_t)new_target - (int32_t)(state.current_addr + 2);
                    if (offset < -128 || offset > 127) {
                        // Out of range — refuse the edit (do not corrupt the offset).
                        draw();
                        return 0;
                    }
                    asm_edit_history_push(rel_addr, prev_rel, prev_part, prev_pending);
                    canonical_write(rel_addr, (uint8_t)(offset & 0xFF));
                    if (asm_edit_pending == 0) {
                        asm_edit_pending = 1;
                    } else {
                        asm_edit_pending = 0;
                        if (asm_edit_part + 1 < part_count) {
                            asm_edit_part++;
                        } else {
                            step_disassembly(1);
                            asm_edit_part = 0;
                            asm_edit_history_reset(state.current_addr);
                        }
                    }
                    draw();
                    return 0;
                }
                // Map display-order parts to memory-order offsets:
                //   2-byte insn: part 1 -> +1
                //   3-byte insn: part 1 -> +2 (high byte, displayed first)
                //                part 2 -> +1 (low byte,  displayed second)
                uint8_t mem_off;
                if (insn_len == 3) {
                    mem_off = (asm_edit_part == 1) ? 2 : 1;
                } else {
                    mem_off = 1;
                }
                uint16_t byte_addr = (uint16_t)(state.current_addr + mem_off);
                uint8_t prev_byte = canonical_read(byte_addr);
                if (asm_edit_pending == 0) {
                    canonical_write(byte_addr, (uint8_t)((nib << 4) | (prev_byte & 0x0F)));
                    asm_edit_history_push(byte_addr, prev_byte, prev_part, prev_pending);
                    asm_edit_pending = 1;
                } else {
                    canonical_write(byte_addr, (uint8_t)((prev_byte & 0xF0) | nib));
                    asm_edit_history_push(byte_addr, prev_byte, prev_part, prev_pending);
                    asm_edit_pending = 0;
                    uint8_t new_parts = asm_edit_part_count(state.current_addr);
                    if (asm_edit_part + 1 < new_parts) {
                        asm_edit_part++;
                    } else {
                        step_disassembly(1);
                        asm_edit_part = 0;
                        asm_edit_history_reset(state.current_addr);
                    }
                }
                draw();
                return 0;
            }
            if (asm_edit_part == 0 && ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z'))) {
                opcode_picker_open((char)key);
                draw();
                return 0;
            }
        }
    }

    switch (key) {
        case KEY_BACK:
        case KEY_DELETE:
            apply_logical_delete();
            draw();
            return 0;
        case KEY_LEFT:
            if (state.view == MONITOR_VIEW_BINARY) {
                move_binary_bits(-1);
            } else {
                move_current(-1);
            }
            draw();
            return 0;
        case KEY_RIGHT:
            if (state.view == MONITOR_VIEW_BINARY) {
                move_binary_bits(1);
            } else {
                move_current(1);
            }
            draw();
            return 0;
        case KEY_UP:
            if (state.view == MONITOR_VIEW_DISASM) {
                step_disassembly(-1);
                draw();
                return 0;
            }
            move_current(-row_span());
            draw();
            return 0;
        case KEY_DOWN:
            if (state.view == MONITOR_VIEW_DISASM) {
                step_disassembly(1);
                draw();
                return 0;
            }
            move_current(row_span());
            draw();
            return 0;
        case KEY_PAGEUP:
        case KEY_F1:
        case KEY_F2:
        case KEY_CONFIG:
            if (state.view == MONITOR_VIEW_DISASM) {
                step_disassembly(-content_height);
                draw();
                return 0;
            }
            page_move(-content_height);
            draw();
            return 0;
        case KEY_PAGEDOWN:
        case KEY_F7:
            if (state.view == MONITOR_VIEW_DISASM) {
                step_disassembly(content_height);
                draw();
                return 0;
            }
            page_move(content_height);
            draw();
            return 0;
        default:
            break;
    }

    switch (key) {
        case 'm': case 'M':
            help_visible = false;
            set_view(MONITOR_VIEW_HEX);
            break;
        case 'i': case 'I':
            help_visible = false;
            set_view(MONITOR_VIEW_ASCII);
            break;
        case 'a': case 'A':
        case 'd': case 'D':
            help_visible = false;
            set_view(MONITOR_VIEW_ASM);
            break;
        case 's': case 'S':
            help_visible = false;
            handle_save_command();
            break;
        case 'l': case 'L':
            help_visible = false;
            handle_load_command();
            break;
        case 'v': case 'V':
            help_visible = false;
            set_view(MONITOR_VIEW_SCREEN);
            break;
        case 'b': case 'B':
            help_visible = false;
            set_view(MONITOR_VIEW_BINARY);
            break;
        case 'w': case 'W':
            handle_width_command();
            break;
        case 'z': case 'Z':
            help_visible = false;
            if (backend && backend->supports_freeze()) {
                backend->set_frozen(!backend->is_frozen());
            }
            break;
        case 'o':
            state.cpu_port = next_cpu_mode(state.cpu_port);
            backend->set_monitor_cpu_port(state.cpu_port);
            break;
        case 'O':
            current_vic_bank = next_vic_bank(current_vic_bank);
            last_live_vic_bank = current_vic_bank;
            vic_bank_override = true;
            break;
        case 'u': case 'U':
            state.illegal_enabled = !state.illegal_enabled;
            break;
        case '?':
            toggle_help();
            break;
        case 'x': case 'X':
        case KEY_CTRL_O:
        case KEY_BREAK:
        case KEY_ESCAPE:
            return 1;
        case 'j': case 'J':
            strcpy(buffer, "AAAA");
            if (prompt_command("Jump AAAA", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_address(buffer, &address);
                if (error == MONITOR_OK) {
                    monitor_apply_goto(&state, address);
                } else {
                    get_ui()->popup(monitor_error_text(error), BUTTON_OK);
                }
            }
            break;
        case 'e': case 'E':
            if (inline_edit_supported()) {
                enter_edit_mode();
            }
            break;
        case 'f': case 'F':
            strcpy(buffer, "AAAA-BBBB,DD");
            if (prompt_command("Fill AAAA-BBBB,DD", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_fill(buffer, &start, &end, &byte_value);
                if (error == MONITOR_OK) {
                    monitor_fill_memory(backend, start, end, byte_value);
                } else {
                    get_ui()->popup(monitor_error_text(error), BUTTON_OK);
                }
            }
            break;
        case 't': case 'T':
            strcpy(buffer, "AAAA-BBBB,CCCC");
            if (prompt_command("Transfer AAAA-BBBB,CCCC", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_transfer(buffer, &start, &end, &dest);
                if (error == MONITOR_OK) {
                    monitor_transfer_memory(backend, start, end, dest);
                } else {
                    get_ui()->popup(monitor_error_text(error), BUTTON_OK);
                }
            }
            break;
        case 'c': case 'C':
            strcpy(buffer, "AAAA-BBBB,CCCC");
            if (prompt_command("Compare AAAA-BBBB,CCCC", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_compare(buffer, &start, &end, &dest);
                if (error == MONITOR_OK) {
                    int found = monitor_compare_collect(backend, start, end, dest,
                                                       hunt_addrs, (int)(sizeof(hunt_addrs) / sizeof(hunt_addrs[0])));
                    if (found <= 0) {
                        get_ui()->popup("No differences", BUTTON_OK);
                    } else {
                        hunt_picker_open_labeled(found, "Compare results");
                    }
                } else {
                    get_ui()->popup(monitor_error_text(error), BUTTON_OK);
                }
            }
            break;
        case 'h': case 'H':
            strcpy(buffer, "0000-FFFF, ");
            if (prompt_command("Hunt AAAA-BBBB,...", buffer, sizeof(buffer) - 1, false)) {
                error = monitor_parse_hunt(buffer, &start, &end, needle, &needle_len);
                if (error == MONITOR_OK) {
                    int found = monitor_hunt_collect(backend, start, end, needle, needle_len,
                                                    hunt_addrs, (int)(sizeof(hunt_addrs) / sizeof(hunt_addrs[0])));
                    if (found <= 0) {
                        get_ui()->popup("No matches", BUTTON_OK);
                    } else {
                        hunt_picker_open(found);
                    }
                } else {
                    get_ui()->popup(monitor_error_text(error), BUTTON_OK);
                }
            }
            break;
        case 'g': case 'G':
            if (handle_goto_command()) {
                return 1;
            }
            break;
        default:
            break;
    }
    draw();
    return 0;
}

int MachineMonitor :: poll(int)
{
    int key;
    if (!keyboard) {
        return MENU_CLOSE;
    }
    key = keyboard->getch();
    key = get_ui()->keymapper(key, e_keymap_monitor);
    if (key == -1) {
        if (update_edit_blink()) {
            draw();
            return 0;
        }
        uint8_t vic_bank = backend->get_live_vic_bank();
        if (vic_bank != last_live_vic_bank) {
            last_live_vic_bank = vic_bank;
            if (!vic_bank_override) {
                current_vic_bank = vic_bank;
            }
            draw_status();
        }
        return 0;
    }
    if (key == -2) {
        return MENU_CLOSE;
    }
    return handle_key(key);
}

void MachineMonitor::handle_load_command()
{
    char path_buf[256] = "";
    char name_buf[64] = "";
    if (!monitor_io::pick_file(get_ui(), "LOAD: select file", path_buf, sizeof(path_buf),
                               name_buf, sizeof(name_buf), false)) {
        redraw_full();
        return;
    }
    redraw_full();
    if (!name_buf[0]) {
        return;
    }

    // Default prompt is the canonical "PRG,0000,AUTO" — RETURN performs a
    // standard PRG load. Last-used values are remembered if the user
    // overrode anything in a previous invocation. Empty / partial entries
    // fall back to defaults inside monitor_parse_load_params.
    char buffer[40];
    if (monitor_last_load_use_prg) {
        strcpy(buffer, "PRG,");
    } else {
        sprintf(buffer, "%04X,", monitor_last_load_start);
    }
    char tail[24];
    sprintf(tail, "%04X,", monitor_last_load_offset);
    strcat(buffer, tail);
    if (monitor_last_load_length_auto) {
        strcat(buffer, "AUTO");
    } else {
        sprintf(tail, "%X", (unsigned)monitor_last_load_length);
        strcat(buffer, tail);
    }
    if (!prompt_command("Load [PRG|AAAA],[Off],[Len|AUTO]", buffer, sizeof(buffer) - 1, true)) {
        redraw_full();
        return;
    }

    bool use_prg = true;
    uint16_t start_addr = 0;
    uint16_t offset = 0;
    bool length_auto = true;
    uint32_t length = 0;
    MonitorError error = monitor_parse_load_params(buffer, &use_prg, &start_addr,
                                                   &offset, &length_auto, &length);
    if (error != MONITOR_OK) {
        redraw_full();
        get_ui()->popup(monitor_error_text(error), BUTTON_OK);
        redraw_full();
        return;
    }

    monitor_last_load_use_prg = use_prg;
    if (!use_prg) {
        monitor_last_load_start = start_addr;
    }
    monitor_last_load_offset = offset;
    monitor_last_load_length_auto = length_auto;
    monitor_last_load_length = length;

    const char *err = monitor_io::load_into_memory(path_buf, name_buf, backend,
                                                   start_addr, use_prg,
                                                   offset, length_auto, length,
                                                   &start_addr, &length);
    redraw_full();
    if (err) {
        get_ui()->popup(err, BUTTON_OK);
        redraw_full();
        return;
    }
    show_io_confirmation("LOAD", name_buf, start_addr, length);
    redraw_full();
}

void MachineMonitor::handle_save_command()
{
    // Prompt for the memory range FIRST, so the user is not left wondering
    // what range the picker is about to write — and so we can short-circuit
    // before opening the picker on an obviously invalid range.
    char range_buf[16];
    sprintf(range_buf, "%04X-%04X", monitor_last_save_start, monitor_last_save_end);
    if (!prompt_command("Save AAAA-BBBB", range_buf, sizeof(range_buf) - 1, true)) {
        return;
    }
    uint16_t start = 0;
    uint16_t end = 0;
    MonitorError error = monitor_parse_save_params(range_buf, &start, &end);
    if (error != MONITOR_OK) {
        get_ui()->popup(monitor_error_text(error), BUTTON_OK);
        redraw_full();
        return;
    }
    monitor_last_save_start = start;
    monitor_last_save_end = end;

    char path_buf[256] = "";
    char name_buf[64] = "";
    if (!monitor_io::pick_file(get_ui(), "SAVE: pick file/dir (F5=here)", path_buf, sizeof(path_buf),
                               name_buf, sizeof(name_buf), true)) {
        redraw_full();
        return;
    }
    redraw_full();

    if (!name_buf[0]) {
        // Picker returned a directory only (F5 in pick mode); ask for filename.
        if (monitor_last_save_name[0]) {
            strncpy(name_buf, monitor_last_save_name, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = 0;
        } else {
            name_buf[0] = 0;
        }
        if (!prompt_command("Save as", name_buf, sizeof(name_buf) - 1, false)) {
            return;
        }
        if (!name_buf[0]) {
            return;
        }
    }
    strncpy(monitor_last_save_name, name_buf, sizeof(monitor_last_save_name) - 1);
    monitor_last_save_name[sizeof(monitor_last_save_name) - 1] = 0;

    const char *err = monitor_io::save_from_memory(get_ui(), path_buf, name_buf, backend, start, end);
    redraw_full();
    if (err) {
        get_ui()->popup(err, BUTTON_OK);
        redraw_full();
        return;
    }
    uint32_t bytes = (uint32_t)end - (uint32_t)start + 1;
    show_io_confirmation("SAVE", name_buf, start, bytes);
    redraw_full();
}

void MachineMonitor::show_io_confirmation(const char *op, const char *name,
                                          uint16_t start_addr, uint32_t bytes)
{
    // Single-line popup keeps the rendering simple and fits inside the
    // monitor's standard popup width on both telnet and on-device. Format
    // omits the leading path (already chosen by the user) and reports the
    // effective range so the user can verify what just hit memory or disk.
    char msg[80];
    uint32_t end_addr = (uint32_t)start_addr + (bytes ? bytes - 1 : 0);
    if (end_addr > 0xFFFF) end_addr = 0xFFFF;
    sprintf(msg, "%s %s: $%04X-$%04X (%u bytes)",
            op, name, (unsigned)start_addr, (unsigned)end_addr, (unsigned)bytes);
    get_ui()->popup(msg, BUTTON_OK);
}

void MachineMonitor::handle_width_command()
{
    if (state.view != MONITOR_VIEW_BINARY) {
        get_ui()->popup("WIDTH ONLY IN BINARY VIEW", BUTTON_OK);
        redraw_full();
        return;
    }
    char buf[8];
    sprintf(buf, "%u", (unsigned)monitor_binary_byte_stride());
    if (!prompt_command("Width 1..4 bytes/row", buf, sizeof(buf) - 1, true)) {
        return;
    }
    int v = 0;
    const char *p = buf;
    while (*p == ' ') p++;
    if (!*p) return;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 9999) break;
        p++;
    }
    while (*p == ' ') p++;
    if (*p || v < MONITOR_BINARY_MIN_BYTES_PER_ROW || v > MONITOR_BINARY_MAX_BYTES_PER_ROW) {
        get_ui()->popup("WIDTH 1..4 BYTES", BUTTON_OK);
        redraw_full();
        return;
    }
    monitor_binary_bytes_per_row = (uint8_t)v;
    binary_bit_index = 7;
    ensure_current_visible();
    draw();
}

bool MachineMonitor::handle_goto_command()
{
    char buffer[8];
    uint16_t default_addr = monitor_last_goto_valid ? monitor_last_goto_addr : state.current_addr;
    sprintf(buffer, "%04X", default_addr);
    if (!prompt_command("Go AAAA", buffer, sizeof(buffer) - 1, true)) {
        return false;
    }
    uint16_t address = 0;
    MonitorError error = monitor_parse_address(buffer, &address);
    if (error != MONITOR_OK) {
        get_ui()->popup(monitor_error_text(error), BUTTON_OK);
        return false;
    }
    monitor_last_goto_addr = address;
    monitor_last_goto_valid = true;
    // Reposition the cursor first (mirrors J), so the displayed address matches
    // what we are about to execute.
    monitor_apply_goto(&state, address);
    // Hand off to the C64: the boot-cart DMA-jump path unfreezes the FPGA C64
    // and JMP ($00AA)s to `address`. The monitor must exit so the menu
    // dismisses and user code regains control.
    monitor_io::jump_to(address);
    return true;
}
