#include "machine_monitor.h"

#include "disassembler_6502.h"
#include "editor.h"
#include "userinterface.h"
#include "screen.h"
#include "keyboard.h"

extern "C" {
#include "dump_hex.h"
#include "small_printf.h"
}

#include <string.h>

namespace {

#ifdef RUNS_ON_PC
static const uint8_t monitor_cursor_blink_idle_polls = 3;
#else
static const uint16_t monitor_cursor_blink_half_period_ms = 400;
#endif

static const char *const monitor_help_lines[] = {
    "M Hex/Asc  D Disasm  S Screen",
    "O CPU bank       . Disasm offset",
    "e Inline edit    J AAAA goto",
    "F Fill           T Transfer",
    "C Compare        H Hunt",
    "R Registers      N Evaluate",
    "* Illegal decode",
    "F3/? toggle help",
    "RUN/LTAR/ESC edit off",
    "X/RUN exits monitor",
    "Example: F C000-C0FF,00",
    NULL
};

static const uint8_t monitor_cpu_modes[] = { 0x07, 0x03, 0x06, 0x02, 0x05, 0x01, 0x04 };

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
    if (value < 26) {
        return (char)('A' + value);
    }
    if (value >= 32 && value < 58) {
        return (char)(' ' + (value - 32));
    }
    if (value >= 64 && value < 90) {
        return (char)('a' + (value - 64));
    }
    return '.';
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

static const char *cpu_a000_text(uint8_t cpu_port)
{
    return ((cpu_port & 0x03) == 0x03) ? "BASIC" : "RAM";
}

static const char *cpu_d000_text(uint8_t cpu_port)
{
    if ((cpu_port & 0x03) == 0x00) {
        return "RAM";
    }
    return (cpu_port & 0x04) ? "IO" : "CHAR";
}

static const char *cpu_e000_text(uint8_t cpu_port)
{
    return (cpu_port & 0x02) ? "KERNAL" : "RAM";
}

static int cpu_mode_number(uint8_t cpu_port)
{
    return 24 + (cpu_port & 0x07);
}

static uint8_t next_cpu_mode(uint8_t cpu_port)
{
    int i;

    cpu_port &= 0x07;
    for (i = 0; i < (int)(sizeof(monitor_cpu_modes) / sizeof(monitor_cpu_modes[0])); i++) {
        if (monitor_cpu_modes[i] == cpu_port) {
            return monitor_cpu_modes[(i + 1) % (int)(sizeof(monitor_cpu_modes) / sizeof(monitor_cpu_modes[0]))];
        }
    }
    return monitor_cpu_modes[0];
}

static uint8_t normalize_cpu_mode(uint8_t cpu_port)
{
    cpu_port &= 0x07;
    switch (cpu_port) {
        case 0x00:
            return 0x04;
        case 0x07:
        case 0x03:
        case 0x06:
        case 0x02:
        case 0x05:
        case 0x01:
        case 0x04:
            return cpu_port;
        default:
            return 0x07;
    }
}

static void format_status_line(char *line, uint8_t cpu_port, uint8_t vic_bank)
{
    uint16_t vic_base = (uint16_t)(vic_bank << 14);
    int pos;

    sprintf(line, "CPU%d %s %s %s|VIC%d ",
            cpu_mode_number(cpu_port),
            cpu_a000_text(cpu_port),
            cpu_d000_text(cpu_port),
            cpu_e000_text(cpu_port),
            vic_bank);
    pos = strlen(line);
    dump_hex_word(line, pos, vic_base);
    line[pos + 4] = '-';
    dump_hex_word(line, pos + 5, (uint16_t)(vic_base + 0x3FFF));
    line[pos + 9] = 0;
}

static bool inline_edit_view(MachineMonitorView view)
{
    return (view == MONITOR_VIEW_HEX) || (view == MONITOR_VIEW_ASCII);
}

static MachineMonitorView toggle_hex_ascii_view(MachineMonitorView view)
{
    return (view == MONITOR_VIEW_HEX) ? MONITOR_VIEW_ASCII : MONITOR_VIEW_HEX;
}

static uint16_t row_span_for_view(MachineMonitorView view)
{
    if (view == MONITOR_VIEW_ASCII || view == MONITOR_VIEW_SCREEN) {
        return MONITOR_TEXT_BYTES_PER_ROW;
    }
    return MONITOR_HEX_BYTES_PER_ROW;
}

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

void monitor_apply_goto(MachineMonitorState *state, uint16_t address)
{
    state->current_addr = address;
    state->base_addr = address & 0xFFF8;
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
    if (*end < *start) {
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

MonitorError monitor_parse_registers(const char *text, uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *sp, uint8_t *sr)
{
    const char *cursor = text;
    uint16_t parsed = 0;
    MonitorError error = parse_hex_digits(cursor, 1, 4, 0xFFFF, pc);
    if (error != MONITOR_OK) {
        return MONITOR_ADDR;
    }
    if ((error = expect_separator(cursor, ',', MONITOR_SYNTAX)) != MONITOR_OK) return error;
    if ((error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed)) != MONITOR_OK) return MONITOR_VALUE;
    *a = (uint8_t)parsed;
    if ((error = expect_separator(cursor, ',', MONITOR_SYNTAX)) != MONITOR_OK) return error;
    if ((error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed)) != MONITOR_OK) return MONITOR_VALUE;
    *x = (uint8_t)parsed;
    if ((error = expect_separator(cursor, ',', MONITOR_SYNTAX)) != MONITOR_OK) return error;
    if ((error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed)) != MONITOR_OK) return MONITOR_VALUE;
    *y = (uint8_t)parsed;
    if ((error = expect_separator(cursor, ',', MONITOR_SYNTAX)) != MONITOR_OK) return error;
    if ((error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed)) != MONITOR_OK) return MONITOR_VALUE;
    *sp = (uint8_t)parsed;
    if ((error = expect_separator(cursor, ',', MONITOR_SYNTAX)) != MONITOR_OK) return error;
    if ((error = parse_hex_digits(cursor, 1, 2, 0xFF, &parsed)) != MONITOR_OK) return MONITOR_VALUE;
    *sr = (uint8_t)parsed;
    skip_spaces(cursor);
    return *cursor ? MONITOR_SYNTAX : MONITOR_OK;
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

MachineMonitor :: MachineMonitor(UserInterface *ui, MemoryBackend *mem_backend) : UIObject(ui)
{
    backend = mem_backend;
    state.view = MONITOR_VIEW_HEX;
    state.current_addr = 0;
    state.base_addr = 0;
    state.disasm_offset = 0;
    state.illegal_enabled = false;
    registers_pc = 0;
    registers_a = 0;
    registers_x = 0;
    registers_y = 0;
    registers_sp = 0;
    registers_sr = 0;
    screen = NULL;
    keyboard = NULL;
    window = NULL;
    content_height = 0;
    status_line[0] = 0;
    cache_addr = 0;
    cache_valid = false;
    pending_hex_nibble = -1;
    edit_mode = false;
    edit_cursor_visible = true;
    help_visible = false;
    last_vic_bank = 0;
#ifdef RUNS_ON_PC
    edit_blink_polls = monitor_cursor_blink_idle_polls;
#else
    edit_blink_ms = 0;
#endif
}

void MachineMonitor :: invalidate_cache()
{
    cache_valid = false;
}

void MachineMonitor :: read_block_wrap(uint16_t address, uint8_t *dst, uint16_t len)
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

void MachineMonitor :: refresh_cache(uint16_t address)
{
    if (cache_valid && cache_addr == address) {
        return;
    }
    cache_addr = address;
    read_block_wrap(address, cache, sizeof(cache));
    cache_valid = true;
}

uint8_t MachineMonitor :: cached_byte(uint16_t address)
{
    uint16_t offset = (uint16_t)(address - cache_addr);
    return cache[offset];
}

void MachineMonitor :: set_status(const char *text)
{
    strncpy(status_line, text, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
}

void MachineMonitor :: adjust_base_for_current()
{
    uint16_t span = row_span();
    state.base_addr = (uint16_t)(state.current_addr & (uint16_t)(~(span - 1)));
}

void MachineMonitor :: ensure_current_visible()
{
    if (state.view == MONITOR_VIEW_DISASM) {
        return;
    }

    uint16_t span = row_span();
    uint16_t page_bytes = (uint16_t)(content_height * span);
    uint16_t current_row = (uint16_t)(state.current_addr & (uint16_t)(~(span - 1)));

    state.base_addr = (uint16_t)(state.base_addr & (uint16_t)(~(span - 1)));

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
    if (view == MONITOR_VIEW_DISASM) {
        state.base_addr = state.current_addr;
    } else {
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
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
    ensure_current_visible();
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
}

void MachineMonitor :: page_move(int lines)
{
    uint16_t step = (uint16_t)(lines * row_span());

    state.current_addr = (uint16_t)(state.current_addr + step);
    if (state.view == MONITOR_VIEW_DISASM) {
        state.base_addr = state.current_addr;
    } else {
        state.base_addr = (uint16_t)(state.base_addr + step);
        ensure_current_visible();
    }
    pending_hex_nibble = -1;
    if (edit_mode) {
        reset_edit_blink();
    }
}

bool MachineMonitor :: prompt_command(const char *title, char *buffer, int max_len, bool template_mode)
{
    return get_ui()->string_box(title, buffer, max_len, template_mode) > 0;
}

void MachineMonitor :: toggle_help()
{
    help_visible = !help_visible;
    set_status(help_visible ? "Help open" : "Help closed");
}

void MachineMonitor :: reset_edit_blink()
{
    edit_cursor_visible = true;
}

bool MachineMonitor :: update_edit_blink()
{
    return false;
}

void MachineMonitor :: draw_header()
{
    char line[40];
    if (help_visible) {
        strcpy(line, "HELP (F3/? closes)");
    } else {
        const char *view_name = "HEX";
        switch (state.view) {
            case MONITOR_VIEW_DISASM: view_name = "DIS"; break;
            case MONITOR_VIEW_ASCII: view_name = "ASC"; break;
            case MONITOR_VIEW_SCREEN: view_name = "SCR"; break;
            default: break;
        }
        sprintf(line, "%s $%04x %s %s", view_name, state.current_addr,
                edit_mode ? "EDIT" : "    ", state.illegal_enabled ? "ILG" : "STD");
    }
    draw_padded(window, 0, line, strlen(line));
}

void MachineMonitor :: draw_message()
{
    draw_padded(window, window->get_size_y() - 2, status_line, strlen(status_line));
}

void MachineMonitor :: draw_status()
{
    char line[40];
    uint8_t cpu_port = backend->get_monitor_cpu_port();
    uint8_t vic_bank = backend->get_live_vic_bank();

    last_vic_bank = vic_bank;
    format_status_line(line, cpu_port, vic_bank);
    draw_padded(window, window->get_size_y() - 1, line, strlen(line));
}

void MachineMonitor :: draw_help()
{
    for (int line_idx = 0; line_idx < content_height; line_idx++) {
        const char *text = monitor_help_lines[line_idx];
        if (!text) {
            text = "";
        }
        draw_padded(window, line_idx + 1, text, strlen(text));
    }
}

void MachineMonitor :: draw_hex_row(int y, uint16_t addr, const uint8_t *bytes)
{
    char line[MONITOR_HEX_ROW_CHARS + 1];
    int highlight = -1;

    monitor_format_hex_row(addr, bytes, line);
    if ((!edit_mode || edit_cursor_visible) && state.view == MONITOR_VIEW_HEX && state.current_addr >= addr && state.current_addr < (uint16_t)(addr + MONITOR_HEX_BYTES_PER_ROW)) {
        int index = state.current_addr - addr;
        highlight = 5 + (3 * index);
    }
    draw_with_highlight(window, y, line, MONITOR_HEX_ROW_CHARS, highlight, 2);
}

void MachineMonitor :: draw_text_row(int y, uint16_t addr, const uint8_t *bytes, bool screen_codes)
{
    char line[MONITOR_TEXT_ROW_CHARS + 1];
    int highlight = -1;

    monitor_format_text_row(addr, bytes, MONITOR_TEXT_BYTES_PER_ROW, screen_codes, line);
    if ((!edit_mode || edit_cursor_visible) && state.view == MONITOR_VIEW_ASCII && state.current_addr >= addr && state.current_addr < (uint16_t)(addr + MONITOR_TEXT_BYTES_PER_ROW)) {
        highlight = 5 + (state.current_addr - addr);
    }
    draw_with_highlight(window, y, line, MONITOR_TEXT_ROW_CHARS, highlight, 1);
}

void MachineMonitor :: draw_hex()
{
    int line_idx;
    refresh_cache(state.base_addr);
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_HEX_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_HEX_BYTES_PER_ROW];
        int i;

        for (i = 0; i < MONITOR_HEX_BYTES_PER_ROW; i++) {
            bytes[i] = cached_byte((uint16_t)(addr + i));
        }
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
    refresh_cache(state.base_addr);
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_TEXT_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_TEXT_BYTES_PER_ROW];
        int i;

        for (i = 0; i < MONITOR_TEXT_BYTES_PER_ROW; i++) {
            bytes[i] = cached_byte((uint16_t)(addr + i));
        }
        draw_text_row(line_idx + 1, addr, bytes, false);
    }
}

void MachineMonitor :: draw_screen_codes()
{
    int line_idx;
    refresh_cache(state.base_addr);
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        uint16_t addr = (uint16_t)(state.base_addr + line_idx * MONITOR_TEXT_BYTES_PER_ROW);
        uint8_t bytes[MONITOR_TEXT_BYTES_PER_ROW];
        int i;

        for (i = 0; i < MONITOR_TEXT_BYTES_PER_ROW; i++) {
            bytes[i] = cached_byte((uint16_t)(addr + i));
        }
        draw_text_row(line_idx + 1, addr, bytes, true);
    }
}

void MachineMonitor :: draw_disassembly()
{
    uint16_t addr = (uint16_t)(state.base_addr + state.disasm_offset);
    int line_idx;
    refresh_cache(addr);
    for (line_idx = 0; line_idx < content_height; line_idx++) {
        Disassembled6502 decoded;
        char line[MONITOR_DISASM_ROW_CHARS + 1];
        uint8_t b0 = cached_byte(addr);
        uint8_t b1 = cached_byte((uint16_t)(addr + 1));
        uint8_t b2 = cached_byte((uint16_t)(addr + 2));
        int highlight = -1;
        memset(line, ' ', sizeof(line));
        line[MONITOR_DISASM_ROW_CHARS] = 0;
        disassemble_6502(addr, &cache[(uint16_t)(addr - cache_addr)], state.illegal_enabled, &decoded);
        dump_hex_word(line, 0, addr);
        line[4] = ' ';
        dump_hex_byte(line, 5, b0);
        if (decoded.length >= 2) {
            dump_hex_byte(line, 8, b1);
        }
        if (decoded.length >= 3) {
            dump_hex_byte(line, 11, b2);
        }
        line[14] = ' ';
        strncpy(line + 15, decoded.text, MONITOR_DISASM_ROW_CHARS - 15);
        {
            const char *source = backend->source_name(addr);
            int pos = 15 + strlen(decoded.text);
            if (pos < MONITOR_DISASM_ROW_CHARS - 2) {
                line[pos++] = ' ';
                line[pos++] = '[';
                while (*source && pos < MONITOR_DISASM_ROW_CHARS - 1) {
                    line[pos++] = *source++;
                }
                if (pos < MONITOR_DISASM_ROW_CHARS) {
                    line[pos++] = ']';
                }
            }
        }
        if (state.current_addr >= addr && state.current_addr < (uint16_t)(addr + decoded.length)) {
            highlight = 0;
        }
        draw_with_highlight(window, line_idx + 1, line, MONITOR_DISASM_ROW_CHARS, highlight, MONITOR_DISASM_ROW_CHARS);
        addr = (uint16_t)(addr + decoded.length);
    }
}

void MachineMonitor :: draw()
{
    draw_header();
    if (help_visible) {
        draw_help();
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
            default:
                draw_hex();
                break;
        }
    }
    draw_message();
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
        set_status("Hex nibble pending");
    } else {
        uint8_t byte = (uint8_t)((pending_hex_nibble << 4) | value);
        backend->write(state.current_addr, byte);
        invalidate_cache();
        move_current(1);
        pending_hex_nibble = -1;
        set_status("Byte written");
    }
}

void MachineMonitor :: apply_ascii_char(char value)
{
    backend->write(state.current_addr, (uint8_t)value);
    invalidate_cache();
    move_current(1);
    set_status("Char written");
}

void MachineMonitor :: exit_edit_mode()
{
    edit_mode = false;
    edit_cursor_visible = true;
    pending_hex_nibble = -1;
    set_status("Edit off");
}

void MachineMonitor :: enter_edit_mode()
{
    help_visible = false;
    edit_mode = true;
    pending_hex_nibble = -1;
    reset_edit_blink();
    set_status(state.view == MONITOR_VIEW_HEX ? "Hex edit" : "ASCII edit");
}

void MachineMonitor :: init(Screen *scr, Keyboard *keyb)
{
    screen = scr;
    keyboard = keyb;
    screen->set_color(1);
    window = new Window(screen, 0, 2, screen->get_size_x(), screen->get_size_y() - 3);
    window->draw_border();
    window->set_color(1);
    content_height = window->get_size_y() - 4;
    backend->set_monitor_cpu_port(normalize_cpu_mode(backend->get_live_cpu_port()));
    last_vic_bank = backend->get_live_vic_bank();
    set_status("M view : edit O cpu . dis F3 help");
    draw();
}

void MachineMonitor :: deinit(void)
{
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
    uint8_t byte_value, needle[80], cpu_port;
    int needle_len;
    MonitorError error;

    if (key == KEY_HELP || key == KEY_F3) {
        toggle_help();
        draw();
        return 0;
    }

    if (edit_mode) {
        if (key == KEY_CTRL_O) {
            return 1;
        }
        if (key == KEY_LEFT || key == KEY_BREAK || key == KEY_ESCAPE || key == KEY_CTRL_E) {
            exit_edit_mode();
            draw();
            return 0;
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
                apply_ascii_char((char)key);
                draw();
                return 0;
            }
        }
    }

    switch (key) {
        case KEY_LEFT:
            move_current(-1);
            draw();
            return 0;
        case KEY_RIGHT:
            move_current(1);
            draw();
            return 0;
        case KEY_UP:
            move_current(-row_span());
            draw();
            return 0;
        case KEY_DOWN:
            move_current(row_span());
            draw();
            return 0;
        case KEY_PAGEUP:
        case KEY_F1:
        case KEY_F2:
        case KEY_CONFIG:
            page_move(-content_height);
            draw();
            return 0;
        case KEY_PAGEDOWN:
        case KEY_F7:
            page_move(content_height);
            draw();
            return 0;
        default:
            break;
    }

    switch (key) {
        case 'm': case 'M':
            help_visible = false;
            set_view(toggle_hex_ascii_view(state.view));
            break;
        case 'd': case 'D':
            help_visible = false;
            set_view(MONITOR_VIEW_DISASM);
            break;
        case 's': case 'S':
            help_visible = false;
            set_view(MONITOR_VIEW_SCREEN);
            break;
        case 'o': case 'O':
            cpu_port = next_cpu_mode(backend->get_monitor_cpu_port());
            backend->set_monitor_cpu_port(cpu_port);
            invalidate_cache();
            set_status("");
            break;
        case '.':
            state.disasm_offset = (uint8_t)((state.disasm_offset + 1) % 3);
            invalidate_cache();
            set_status("Disasm offset");
            break;
        case '*':
            state.illegal_enabled = !state.illegal_enabled;
            set_status(state.illegal_enabled ? "Illegal: ON" : "Illegal: OFF");
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
            if (prompt_command("Goto AAAA", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_address(buffer, &address);
                if (error == MONITOR_OK) {
                    monitor_apply_goto(&state, address);
                    invalidate_cache();
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 'e':
            if (inline_edit_supported()) {
                enter_edit_mode();
            } else {
                set_status("Hex/ASCII only");
            }
            break;
        case 'f': case 'F':
            strcpy(buffer, "AAAA-BBBB,DD");
            if (prompt_command("Fill AAAA-BBBB,DD", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_fill(buffer, &start, &end, &byte_value);
                if (error == MONITOR_OK) {
                    monitor_fill_memory(backend, start, end, byte_value);
                    invalidate_cache();
                    set_status("Fill done");
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 't': case 'T':
            strcpy(buffer, "AAAA-BBBB,CCCC");
            if (prompt_command("Transfer AAAA-BBBB,CCCC", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_transfer(buffer, &start, &end, &dest);
                if (error == MONITOR_OK) {
                    monitor_transfer_memory(backend, start, end, dest);
                    invalidate_cache();
                    set_status("Transfer done");
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 'c': case 'C':
            strcpy(buffer, "AAAA-BBBB,CCCC");
            if (prompt_command("Compare AAAA-BBBB,CCCC", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_compare(buffer, &start, &end, &dest);
                if (error == MONITOR_OK) {
                    monitor_compare_memory(backend, start, end, dest, output, sizeof(output));
                    get_ui()->run_editor(output, strlen(output));
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 'h': case 'H':
            strcpy(buffer, "AAAA-BBBB, ");
            if (prompt_command("Hunt AAAA-BBBB,...", buffer, sizeof(buffer) - 1, true)) {
                error = monitor_parse_hunt(buffer, &start, &end, needle, &needle_len);
                if (error == MONITOR_OK) {
                    monitor_hunt_memory(backend, start, end, needle, needle_len, output, sizeof(output));
                    get_ui()->run_editor(output, strlen(output));
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 'r': case 'R':
            sprintf(buffer, "%04x,%02x,%02x,%02x,%02x,%02x", registers_pc, registers_a, registers_x, registers_y, registers_sp, registers_sr);
            if (prompt_command("Regs PC,A,X,Y,SP,SR", buffer, sizeof(buffer) - 1)) {
                error = monitor_parse_registers(buffer, &registers_pc, &registers_a, &registers_x, &registers_y, &registers_sp, &registers_sr);
                if (error == MONITOR_OK) {
                    set_status("Registers updated");
                } else {
                    set_status(monitor_error_text(error));
                }
            }
            break;
        case 'g': case 'G':
        case 'a': case 'A':
            get_ui()->popup("Reserved", BUTTON_OK);
            break;
        case 'n': case 'N':
            buffer[0] = 0;
            if (prompt_command("Evaluate", buffer, sizeof(buffer) - 1)) {
                error = monitor_format_evaluate(buffer, output, sizeof(output));
                if (error == MONITOR_OK) {
                    get_ui()->popup(output, BUTTON_OK);
                } else {
                    set_status(monitor_error_text(error));
                }
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
        if (vic_bank != last_vic_bank) {
            draw_status();
        }
        return 0;
    }
    if (key == -2) {
        return MENU_CLOSE;
    }
    return handle_key(key);
}
