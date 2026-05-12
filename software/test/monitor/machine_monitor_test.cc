#include <stdio.h>
#include <string.h>

#include "disassembler_6502.h"
#include "monitor_init.h"
#include "machine_monitor_test_support.h"
#include "menu.h"
#include "task_menu.h"
#include "tasks_collection.h"

#define TEST_SORT_ORDER_DEVELOPER 999
#define TEST_SUBSYSID_C64 1
#define TEST_SUBSYSID_U64 9

namespace monitor_io {
void jump_to(uint16_t address);
}

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    MachineMonitor *monitor = new MachineMonitor(this, backend);
    uint16_t go_address = 0;
    monitor->init(screen, keyboard);
    int ret = 0;
    while (!ret && (!host || host->exists())) {
        ret = monitor->poll(0);
    }
    bool do_go = monitor->consume_pending_go(&go_address);
    monitor->deinit();
    delete monitor;
    if (do_go) {
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
        C64 *machine = C64::getMachine();
        if (machine && machine->is_accessible()) {
            release_host();
            machine->release_ownership();
        }
#endif
        monitor_io::jump_to(go_address);
    }
}

namespace monitor_io {
struct FakeMonitorIOState {
    bool pick_file_result;
    char pick_path[256];
    char pick_name[64];
    int pick_file_calls;
    bool last_pick_save_mode;
    bool load_called;
    char load_path[256];
    char load_name[64];
    uint16_t load_start_addr;
    bool load_use_prg;
    uint32_t load_offset;
    bool load_length_auto;
    uint32_t load_length;
    const char *load_error;
    uint8_t load_data[131072];
    uint32_t load_size;

    bool save_called;
    char save_path[256];
    char save_name[64];
    uint16_t save_start;
    uint16_t save_end;
    const char *save_error;
    uint8_t saved_data[131072];
    uint32_t saved_size;

    bool jump_called;
    uint16_t jump_address;
};

} // namespace monitor_io

static SubsysResultCode_e test_monitor_action_callback(SubsysCommand *)
{
    return SSRET_OK;
}

static Action *g_test_machine_monitor_actions[256];

Action *register_machine_monitor_task(int subsys_id, actionFunction_t callback, int function_id)
{
    Action *monitor = new Action("Machine Code Monitor", callback, function_id);
    TaskCategory *dev = TasksCollection::getCategory("Developer", TEST_SORT_ORDER_DEVELOPER);
    dev->prepend(monitor);
    if ((subsys_id >= 0) && (subsys_id < 256)) {
        g_test_machine_monitor_actions[subsys_id] = monitor;
    }
    return monitor;
}

Action *get_machine_monitor_task(int subsys_id)
{
    if ((subsys_id < 0) || (subsys_id >= 256)) {
        return NULL;
    }
    return g_test_machine_monitor_actions[subsys_id];
}

namespace monitor_io {

static FakeMonitorIOState g_monitor_io;

static void reset_fake_monitor_io(void)
{
    memset(&g_monitor_io, 0, sizeof(g_monitor_io));
}

bool pick_file(UserInterface *, const char *, char *path_out, int path_max, char *name_out, int name_max, bool save_mode)
{
    g_monitor_io.pick_file_calls++;
    g_monitor_io.last_pick_save_mode = save_mode;
    if (!g_monitor_io.pick_file_result) {
        return false;
    }
    if (path_out && path_max > 0) {
        strncpy(path_out, g_monitor_io.pick_path, path_max - 1);
        path_out[path_max - 1] = 0;
    }
    if (name_out && name_max > 0) {
        strncpy(name_out, g_monitor_io.pick_name, name_max - 1);
        name_out[name_max - 1] = 0;
    }
    return true;
}

const char *load_into_memory(const char *path, const char *name, MemoryBackend *backend,
                             uint16_t start_addr, bool use_prg_addr, uint32_t offset, bool length_auto, uint32_t length,
                             uint16_t *out_start_addr, uint32_t *out_bytes)
{
    g_monitor_io.load_called = true;
    strncpy(g_monitor_io.load_path, path ? path : "", sizeof(g_monitor_io.load_path) - 1);
    g_monitor_io.load_path[sizeof(g_monitor_io.load_path) - 1] = 0;
    strncpy(g_monitor_io.load_name, name ? name : "", sizeof(g_monitor_io.load_name) - 1);
    g_monitor_io.load_name[sizeof(g_monitor_io.load_name) - 1] = 0;
    g_monitor_io.load_start_addr = start_addr;
    g_monitor_io.load_use_prg = use_prg_addr;
    g_monitor_io.load_offset = offset;
    g_monitor_io.load_length_auto = length_auto;
    g_monitor_io.load_length = length;
    if (g_monitor_io.load_error) {
        return g_monitor_io.load_error;
    }

    uint32_t header_skip = 0;
    uint32_t size = g_monitor_io.load_size;
    if (use_prg_addr) {
        if (size < 2) {
            return "NOT A PRG";
        }
        start_addr = (uint16_t)(g_monitor_io.load_data[0] | (g_monitor_io.load_data[1] << 8));
        header_skip = 2;
        size -= 2;
    }

    uint32_t effective = 0;
    MonitorError err = monitor_validate_load_size(size, offset, length_auto, length, &effective);
    if (err != MONITOR_OK) {
        return monitor_error_text(err);
    }

    backend->begin_session();
    for (uint32_t i = 0; i < effective; i++) {
        backend->write((uint16_t)(start_addr + i), g_monitor_io.load_data[header_skip + offset + i]);
    }
    backend->end_session();

    if (out_start_addr) {
        *out_start_addr = start_addr;
    }
    if (out_bytes) {
        *out_bytes = effective;
    }
    return NULL;
}

const char *save_from_memory(UserInterface *, const char *path, const char *name, MemoryBackend *backend,
                             uint16_t start, uint16_t end)
{
    g_monitor_io.save_called = true;
    strncpy(g_monitor_io.save_path, path ? path : "", sizeof(g_monitor_io.save_path) - 1);
    g_monitor_io.save_path[sizeof(g_monitor_io.save_path) - 1] = 0;
    strncpy(g_monitor_io.save_name, name ? name : "", sizeof(g_monitor_io.save_name) - 1);
    g_monitor_io.save_name[sizeof(g_monitor_io.save_name) - 1] = 0;
    g_monitor_io.save_start = start;
    g_monitor_io.save_end = end;
    if (g_monitor_io.save_error) {
        return g_monitor_io.save_error;
    }

    uint32_t total = (uint32_t)end - (uint32_t)start + 1;
    g_monitor_io.saved_data[0] = (uint8_t)(start & 0xFF);
    g_monitor_io.saved_data[1] = (uint8_t)((start >> 8) & 0xFF);
    backend->begin_session();
    for (uint32_t i = 0; i < total; i++) {
        g_monitor_io.saved_data[2 + i] = backend->read((uint16_t)(start + i));
    }
    backend->end_session();
    g_monitor_io.saved_size = total + 2;
    return NULL;
}

void jump_to(uint16_t address)
{
    g_monitor_io.jump_called = true;
    g_monitor_io.jump_address = address;
}
} // namespace monitor_io

class TaskActionProvider : public ObjectWithMenu
{
public:
    Action *monitor_action;
    bool update_called;
    bool last_writable;

    TaskActionProvider() : monitor_action(NULL), update_called(false), last_writable(false)
    {
    }

    void create_task_items(void)
    {
        monitor_action = register_machine_monitor_task(TEST_SUBSYSID_U64, test_monitor_action_callback, 12);
        g_test_machine_monitor_actions[TEST_SUBSYSID_C64] = monitor_action;
    }

    void update_task_items(bool writablePath)
    {
        update_called = true;
        last_writable = writablePath;
    }
};

static int test_disassembler(void)
{
    Disassembled6502 decoded;

    const uint8_t lda_imm[] = { 0xA9, 0x01, 0x00 };
    disassemble_6502(0xC000, lda_imm, false, &decoded);
    if (expect(decoded.length == 2 && strcmp(decoded.text, "LDA #$01") == 0, "LDA immediate disassembly failed.")) return 1;

    const uint8_t jsr_abs[] = { 0x20, 0x0F, 0xBC };
    disassemble_6502(0xC000, jsr_abs, false, &decoded);
    if (expect(decoded.length == 3 && strcmp(decoded.text, "JSR $BC0F") == 0, "JSR absolute disassembly failed.")) return 1;

    const uint8_t bne_rel[] = { 0xD0, 0x05, 0x00 };
    disassemble_6502(0x1000, bne_rel, false, &decoded);
    if (expect(decoded.length == 2 && strcmp(decoded.text, "BNE $1007") == 0, "BNE branch target disassembly failed.")) return 1;

    const uint8_t brk[] = { 0x00, 0x00, 0x00 };
    disassemble_6502(0x2000, brk, false, &decoded);
    if (expect(decoded.length == 1 && strcmp(decoded.text, "BRK") == 0, "BRK disassembly failed.")) return 1;

    const uint8_t illegal[] = { 0x07, 0x44, 0x00 };
    disassemble_6502(0x3000, illegal, false, &decoded);
    if (expect(strcmp(decoded.text, "???") == 0, "Illegal opcode disable failed.")) return 1;
    disassemble_6502(0x3000, illegal, true, &decoded);
    if (expect(strcmp(decoded.text, "SLO $44") == 0, "Illegal opcode enable failed.")) return 1;

    const uint8_t lda_absy[] = { 0xB9, 0x99, 0xFF };
    disassemble_6502(0x9010, lda_absy, false, &decoded);
    if (expect(decoded.valid && decoded.length == 3 && decoded.operand_bytes == 2 && strcmp(decoded.text, "LDA $FF99,Y") == 0,
               "Absolute indexed disassembly must expose a 2-byte operand.")) return 1;

    const uint8_t asl_acc[] = { 0x0A, 0xEA, 0x00 };
    disassemble_6502(0x4000, asl_acc, false, &decoded);
    if (expect(decoded.valid && decoded.length == 1 && decoded.operand_bytes == 0 && strcmp(decoded.text, "ASL A") == 0,
               "Accumulator disassembly must remain a 1-byte no-operand instruction.")) return 1;

    const uint8_t illegal_zpx[] = { 0x54, 0x44, 0x00 };
    disassemble_6502(0x3000, illegal_zpx, true, &decoded);
    if (expect(strcmp(decoded.text, "NOP $44,X") == 0, "Illegal zeropage,X disassembly failed.")) return 1;

    return 0;
}

static int test_memory_helpers(void)
{
    FakeMemoryBackend backend;
    BackendMachineMonitor monitor(NULL, &backend);
    char output[512];
    uint16_t value;
    uint8_t needle[32];

    monitor_fill_memory(&backend, 0xC000, 0xC00F, 0xAA);
    if (expect(backend.read(0xC000) == 0xAA && backend.read(0xC00F) == 0xAA && backend.read(0xC010) == 0x00,
               "Fill command memory range failed.")) return 1;

    for (value = 0; value < 16; value++) {
        backend.write((uint16_t)(0xC100 + value), (uint8_t)value);
    }
    monitor_transfer_memory(&backend, 0xC100, 0xC110, 0xC101);
    for (value = 0; value < 16; value++) {
        if (expect(backend.read((uint16_t)(0xC101 + value)) == (uint8_t)value, "Transfer overlap-safe copy failed.")) return 1;
    }

    backend.write(0xC200, 0x01);
    backend.write(0xC201, 0x02);
    backend.write(0xC300, 0x01);
    backend.write(0xC301, 0x09);
    monitor_compare_memory(&backend, 0xC200, 0xC202, 0xC300, output, sizeof(output));
    if (expect(strstr(output, "$C201") != NULL && strstr(output, "$C200") == NULL, "Compare differences output failed.")) return 1;

    backend.write(0xC400, 0xDE);
    backend.write(0xC401, 0xAD);
    backend.write(0xC410, 'T');
    backend.write(0xC411, 'E');
    backend.write(0xC412, 'S');
    backend.write(0xC413, 'T');
    needle[0] = 0xDE;
    needle[1] = 0xAD;
    monitor_hunt_memory(&backend, 0xC400, 0xC420, needle, 2, output, sizeof(output));
    if (expect(strstr(output, "$C400") != NULL, "Hunt byte sequence failed.")) return 1;
    monitor_hunt_memory(&backend, 0xC400, 0xC420, (const uint8_t *)"TEST", 4, output, sizeof(output));
    if (expect(strstr(output, "$C410") != NULL, "Hunt string failed.")) return 1;

    return 0;
}

static int test_parsers_and_formatters(void)
{
    FakeMemoryBackend backend;
    char output[512];
    char row[MONITOR_HEX_ROW_CHARS + 1];
    char text_row[MONITOR_TEXT_ROW_CHARS + 1];
    char status_line[40];
    uint16_t value;
    uint16_t start;
    uint16_t end;
    uint16_t dest;
    uint8_t byte;
    uint8_t needle[32];
    int needle_len;
    MachineMonitorState state;
    uint8_t ascii_bytes[MONITOR_TEXT_BYTES_PER_ROW];

    if (expect(monitor_parse_expression("$1234", &value) == MONITOR_OK && value == 0x1234, "Hex expression parse failed.")) return 1;
    if (expect(monitor_parse_expression("0x1234", &value) == MONITOR_OK && value == 0x1234, "0x expression parse failed.")) return 1;
    if (expect(monitor_parse_expression("4660", &value) == MONITOR_OK && value == 0x1234, "Decimal expression parse failed.")) return 1;
    if (expect(monitor_parse_expression("%0001001000110100", &value) == MONITOR_OK && value == 0x1234, "Binary expression parse failed.")) return 1;

    state.view = MONITOR_VIEW_HEX;
    state.current_addr = 0;
    state.base_addr = 0;
    state.disasm_offset = 2;
    state.illegal_enabled = false;
    state.screen_charset = MONITOR_SCREEN_CHARSET_UPPER_GRAPHICS;
    monitor_apply_go(&state, 0x1235);
    if (expect(state.current_addr == 0x1235 && state.base_addr == 0x1230 && state.disasm_offset == 0, "Goto state alignment failed.")) return 1;

    if (expect(monitor_parse_address("XYZ", &value) == MONITOR_ADDR, "Address validator failure.")) return 1;
    if (expect(monitor_parse_fill("C100-C000,00", &start, &end, &byte) == MONITOR_RANGE, "Range validator failure.")) return 1;
    if (expect(monitor_parse_hunt("C100-C200, ", &start, &end, needle, &needle_len) == MONITOR_SYNTAX, "Hunt validator failure.")) return 1;
    if (expect(monitor_parse_hunt("C100-C200,\"Hello\"", &start, &end, needle, &needle_len) == MONITOR_OK &&
               needle_len == 5 && memcmp(needle, "Hello", 5) == 0,
               "Hunt quoted ASCII must preserve mixed-case bytes.")) return 1;
    if (expect(monitor_parse_transfer("C000-C010,C100", &start, &end, &dest) == MONITOR_OK, "Transfer parser failed.")) return 1;
    if (expect(start == 0xC000 && end == 0xC010 && dest == 0xC100, "Transfer parser values failed.")) return 1;
    if (expect(monitor_parse_transfer("C000-C000,C100", &start, &end, &dest) == MONITOR_RANGE,
               "Transfer parser should reject zero-length ranges.")) return 1;
    if (expect(monitor_parse_compare("C000-C000,C100", &start, &end, &dest) == MONITOR_RANGE,
               "Compare parser should reject zero-length ranges.")) return 1;

    if (expect(monitor_format_evaluate("$00ff", output, sizeof(output)) == MONITOR_OK && output[0] == '$',
               "Evaluate formatter failed.")) return 1;

    {
        const uint8_t hex_bytes[MONITOR_HEX_BYTES_PER_ROW] = { 0x41, 0x00, 0x7A, 0x1F, 0x20, 0x30, 0x31, 0x32 };
        monitor_format_hex_row(0x0000, hex_bytes, row);
        if (expect(strcmp(row, "0000 41 00 7A 1F 20 30 31 32 A.z. 012") == 0, "Hex row format mismatch.")) return 1;
    }

    {
        for (int i = 0; i < MONITOR_TEXT_BYTES_PER_ROW; i++) {
            ascii_bytes[i] = (uint8_t)('A' + (i % 26));
        }
        monitor_format_text_row(0x1000, ascii_bytes, MONITOR_TEXT_BYTES_PER_ROW, false, text_row);
        if (expect(strcmp(text_row, "1000 ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF") == 0, "ASCII row format mismatch.")) return 1;
        monitor_format_text_row(0x1000, ascii_bytes, MONITOR_TEXT_BYTES_PER_ROW, true, text_row);
        if (expect((int)strlen(text_row) == MONITOR_TEXT_ROW_CHARS, "Screen-code row width mismatch.")) return 1;
    }

    {
        const uint8_t printable_ascii[MONITOR_TEXT_BYTES_PER_ROW] = {
            0x41, 0x61, 0x20, 0x7E, 0x1F, 0x80
        };
        monitor_format_text_row(0x1100, printable_ascii, 6, false, text_row);
        if (expect(strncmp(text_row, "1100 Aa ~..", 11) == 0,
                   "ASCII row formatter must preserve case and dot non-printables.")) return 1;
    }

    {
        const uint8_t screen_bytes[MONITOR_TEXT_BYTES_PER_ROW] = {
            0x00, 0x01, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
            0x20, 0x21, 0x2A, 0x2B, 0x30, 0x3A, 0x3F, 0x40,
            0x49, 0x4A, 0x4B, 0x55, 0x5A, 0x5B, 0x5D, 0x60,
            0x7F, 0x80, 0x81, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E
        };
        monitor_format_text_row(0x2000, screen_bytes, MONITOR_TEXT_BYTES_PER_ROW, true, text_row);
        if (expect(strncmp(text_row, "2000 ", 5) == 0, "Screen-code row prefix mismatch.")) return 1;
        if (expect(text_row[5] == '@', "Screen code $00 should map to '@'.")) return 1;
        if (expect(text_row[6] == 'A', "Screen code $01 should map to 'A'.")) return 1;
        if (expect(text_row[7] == 'Z', "Screen code $1A should map to 'Z'.")) return 1;
        if (expect(text_row[8] == '[', "Screen code $1B should map to '['.")) return 1;
        if (expect(text_row[9] == '#', "Screen code $1C should map to the pound-sign fallback.")) return 1;
        if (expect(text_row[10] == ']', "Screen code $1D should map to ']'.")) return 1;
        if (expect(text_row[11] == '^', "Screen code $1E should map to the up-arrow fallback.")) return 1;
        if (expect(text_row[12] == '<', "Screen code $1F should map to the left-arrow fallback.")) return 1;
        if (expect(text_row[13] == ' ' && text_row[14] == '!' && text_row[15] == '*' && text_row[16] == '+',
                   "Printable screen-code punctuation mapping is incorrect.")) return 1;
        if (expect(text_row[17] == '0' && text_row[18] == ':' && text_row[19] == '?',
                   "Screen-code digit/punctuation positions are incorrect.")) return 1;
        if (expect((unsigned char)text_row[20] == CHR_HORIZONTAL_LINE, "Screen code $40 should map to the horizontal-line glyph.")) return 1;
        if (expect((unsigned char)text_row[21] == CHR_ROUNDED_UPPER_RIGHT, "Screen code $49 should map to the upper-right rounded corner glyph.")) return 1;
        if (expect((unsigned char)text_row[22] == CHR_ROUNDED_LOWER_LEFT, "Screen code $4A should map to the lower-left rounded corner glyph.")) return 1;
        if (expect((unsigned char)text_row[23] == CHR_ROUNDED_LOWER_RIGHT, "Screen code $4B should map to the lower-right rounded corner glyph.")) return 1;
        if (expect((unsigned char)text_row[24] == CHR_ROUNDED_UPPER_LEFT, "Screen code $55 should map to the upper-left rounded corner glyph.")) return 1;
        if (expect((unsigned char)text_row[25] == CHR_DIAMOND, "Screen code $5A should map to the diamond glyph.")) return 1;
        if (expect(text_row[26] == '+' && (unsigned char)text_row[27] == CHR_VERTICAL_LINE,
                   "Screen-code cross/vertical-line mapping is incorrect.")) return 1;
        if (expect(text_row[28] == '.' && text_row[29] == '.',
               "Non-space screen-code range $60-$7F should render as visible placeholders.")) return 1;
        if (expect(text_row[30] == '@' && text_row[31] == 'A' && text_row[32] == 'Z' && text_row[33] == '[',
                   "Reverse screen codes should map to the same visible glyph positions as their base codes.")) return 1;
        if (expect(text_row[34] == '#' && text_row[35] == ']' && text_row[36] == '^',
                   "Reverse punctuation/arrow screen-code mapping is incorrect.")) return 1;
    }

    {
        static const struct {
            uint8_t port01;
            const char *expected_cpu;
        } cpu_cases[] = {
            { 0x00, "CPU0 $A:RAM $D:RAM $E:RAM" },
            { 0x01, "CPU1 $A:RAM $D:CHR $E:RAM" },
            { 0x02, "CPU2 $A:RAM $D:CHR $E:KRN" },
            { 0x03, "CPU3 $A:BAS $D:CHR $E:KRN" },
            { 0x04, "CPU4 $A:RAM $D:RAM $E:RAM" },
            { 0x05, "CPU5 $A:RAM $D:I/O $E:RAM" },
            { 0x06, "CPU6 $A:RAM $D:I/O $E:KRN" },
            { 0x07, "CPU7 $A:BAS $D:I/O $E:KRN" },
        };

        for (unsigned int i = 0; i < sizeof(cpu_cases) / sizeof(cpu_cases[0]); i++) {
            monitor_format_status_line(status_line, cpu_cases[i].port01, 0);
            if (expect(strncmp(status_line, cpu_cases[i].expected_cpu, strlen(cpu_cases[i].expected_cpu)) == 0,
                       "CPU status formatter mapping mismatch.")) return 1;
        }
    }

    {
        static const struct {
            uint8_t vic_bank;
            const char *expected_vic;
        } vic_cases[] = {
            { 0, "VIC0 $0000" },
            { 1, "VIC1 $4000" },
            { 2, "VIC2 $8000" },
            { 3, "VIC3 $C000" },
        };

        for (unsigned int i = 0; i < sizeof(vic_cases) / sizeof(vic_cases[0]); i++) {
            const char *vic_text;

            monitor_format_status_line(status_line, 0x07, vic_cases[i].vic_bank);
            vic_text = strstr(status_line, "VIC");
            if (expect(vic_text != NULL && strcmp(vic_text, vic_cases[i].expected_vic) == 0,
                       "VIC status formatter mapping mismatch.")) return 1;
        }
    }

    {
        static const struct {
            uint8_t port01;
            uint8_t vic_bank;
            const char *expected;
        } combined_cases[] = {
            { 0x07, 0, "CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000" },
            { 0x00, 3, "CPU0 $A:RAM $D:RAM $E:RAM VIC3 $C000" },
            { 0x01, 1, "CPU1 $A:RAM $D:CHR $E:RAM VIC1 $4000" },
            { 0x06, 2, "CPU6 $A:RAM $D:I/O $E:KRN VIC2 $8000" },
            { 0x05, 0, "CPU5 $A:RAM $D:I/O $E:RAM VIC0 $0000" },
        };

        for (unsigned int i = 0; i < sizeof(combined_cases) / sizeof(combined_cases[0]); i++) {
            monitor_format_status_line(status_line, combined_cases[i].port01, combined_cases[i].vic_bank);
            if (expect(strcmp(status_line, combined_cases[i].expected) == 0,
                       "Combined CPU/VIC status formatter mismatch.")) return 1;
        }
    }

    {
        char bank_28[40];
        char bank_30[40];
        char bank_29[40];
        char bank_31[40];
        char bank_27[40];
        char bank_37[40];

        monitor_format_status_line(bank_28, 0x28, 0);
        monitor_format_status_line(bank_30, 0x30, 0);
        if (expect(strcmp(bank_28, "CPU0 $A:RAM $D:RAM $E:RAM VIC0 $0000") == 0,
                   "Port $28 should map to effective CPU bank 0.")) return 1;
        if (expect(strcmp(bank_28, bank_30) == 0,
                   "High-bit differences must not change the displayed CPU0 map.")) return 1;

        monitor_format_status_line(bank_29, 0x29, 0);
        monitor_format_status_line(bank_31, 0x31, 0);
        if (expect(strcmp(bank_29, "CPU1 $A:RAM $D:CHR $E:RAM VIC0 $0000") == 0,
                   "Port $29 should map to effective CPU bank 1.")) return 1;
        if (expect(strcmp(bank_29, bank_31) == 0,
                   "High-bit differences must not change the displayed CPU1 map.")) return 1;

        monitor_format_status_line(bank_27, 0x27, 0);
        monitor_format_status_line(bank_37, 0x37, 0);
        if (expect(strcmp(bank_27, "CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000") == 0,
                   "Port $27 should map to effective CPU bank 7.")) return 1;
        if (expect(strcmp(bank_27, bank_37) == 0,
                   "High-bit differences must not change the displayed CPU7 map.")) return 1;
    }

    for (uint8_t cpu_bank = 0; cpu_bank < 8; cpu_bank++) {
        for (uint8_t vic_bank = 0; vic_bank < 4; vic_bank++) {
            monitor_format_status_line(status_line, cpu_bank, vic_bank);
            if (expect(strlen(status_line) <= 38, "Status line width exceeded 38 characters.")) return 1;
            if (expect(strchr(status_line, '|') == NULL, "Status line must not contain a pipe separator.")) return 1;
        }
    }

    return 0;
}

static int test_banked_backend(void)
{
    FakeBankedMemoryBackend backend;

    backend.ram[0xA000] = 0xA1;
    backend.ram[0xD000] = 0xD1;
    backend.ram[0xE000] = 0xE1;
    backend.basic[0] = 0xBA;
    backend.charrom[0] = 0xC3;
    backend.kernal[0] = 0x4E;
    backend.io[0] = 0x10;

    backend.set_monitor_cpu_port(0x07);
    if (expect(backend.read(0xA000) == 0xBA, "BASIC ROM read mapping failed.")) return 1;
    if (expect(backend.read(0xD000) == 0x10, "I/O read mapping failed.")) return 1;
    if (expect(backend.read(0xE000) == 0x4E, "KERNAL ROM read mapping failed.")) return 1;

    backend.set_monitor_cpu_port(0x03);
    if (expect(backend.read(0xD000) == 0xC3, "CHAR ROM read mapping failed.")) return 1;

    backend.set_monitor_cpu_port(0x00);
    if (expect(backend.read(0xA000) == 0xA1 && backend.read(0xD000) == 0xD1 && backend.read(0xE000) == 0xE1,
               "RAM read mapping failed.")) return 1;

    backend.set_monitor_cpu_port(0x07);
    backend.write(0xA000, 0x55);
    backend.write(0xE000, 0x66);
    if (expect(backend.ram[0xA000] == 0x55 && backend.basic[0] == 0xBA, "Write-under-BASIC semantics failed.")) return 1;
    if (expect(backend.ram[0xE000] == 0x66 && backend.kernal[0] == 0x4E, "Write-under-KERNAL semantics failed.")) return 1;

    backend.set_monitor_cpu_port(0x03);
    backend.write(0xD000, 0x77);
    if (expect(backend.ram[0xD000] == 0x77 && backend.charrom[0] == 0xC3, "Write-under-CHAR semantics failed.")) return 1;

    backend.set_monitor_cpu_port(0x07);
    backend.write(0xD000, 0x88);
    if (expect(backend.io[0] == 0x88 && backend.ram[0xD000] == 0x77, "I/O write semantics failed.")) return 1;
    if (expect(backend.get_live_vic_bank() == 2, "Live VIC bank calculation failed.")) return 1;
    backend.live_dd00 = 0xA9;
    backend.set_monitor_cpu_port(0x00);
    backend.set_live_vic_bank(1);
    if (expect(backend.live_dd00 == 0xAA, "set_live_vic_bank must preserve unrelated CIA-2 bits while updating VIC bank bits.")) return 1;
    if (expect(backend.get_monitor_cpu_port() == 0x00, "set_live_vic_bank must restore the selected monitor CPU bank after touching DD00.")) return 1;
    backend.set_monitor_cpu_port(0x07);
    if (expect(strcmp(backend.source_name(0xA000), "BASIC") == 0, "BASIC source name failed.")) return 1;
    if (expect(strcmp(backend.source_name(0xD000), "IO") == 0, "I/O source name failed.")) return 1;
    if (expect(strcmp(backend.source_name(0xE000), "KERNAL") == 0, "KERNAL source name failed.")) return 1;

    backend.set_monitor_cpu_port(0x00);
    if (expect(strcmp(backend.source_name(0xA000), "RAM") == 0, "A000 RAM source name failed.")) return 1;
    if (expect(strcmp(backend.source_name(0xD000), "RAM") == 0, "D000 RAM source name failed.")) return 1;
    if (expect(strcmp(backend.source_name(0xE000), "RAM") == 0, "E000 RAM source name failed.")) return 1;

    return 0;
}

static int test_frozen_banked_backend(void)
{
    // Mirrors the freeze-mode invariants from
    // doc/research/machine-monitor/freeze-support/plan.md §Tests:
    // monitor_cpu_port is authoritative; ROM overlays and write-under-ROM
    // behave correctly; $0400-$0FFF reads from the freezer backup region;
    // reads are stable across redraws; IO remains reachable when the bank
    // exposes it.
    FakeBankedMemoryBackend backend;

    backend.frozen = true;
    backend.live_cpu_port = 0x00; // Live latch is unobservable while frozen — must not influence routing.
    backend.live_cpu_ddr = 0x00;

    backend.basic[0] = 0xBA;
    backend.kernal[0] = 0x4E;
    backend.kernal[1] = 0x22;
    backend.charrom[0] = 0xC3;
    backend.io[0xD020 - 0xD000] = 0x00;
    backend.ram[0xA000] = 0xAA;
    backend.ram[0xE000] = 0x11;
    backend.ram[0xE001] = 0x22;
    backend.screen_backup[0x20] = 0x42; // $0420 sentinel — proves backup redirect is used.
    backend.ram[0x0420] = 0xFF;         // Underlying DRAM differs to guarantee the redirect fired.

    // (1) KERNAL visible at $E000 when (monitor_cpu_port & 0x02).
    backend.set_monitor_cpu_port(0x07);
    if (expect(backend.read(0xE000) == 0x4E, "Frozen: KERNAL must be visible at $E000 with port=$07.")) return 1;

    // (1b) RAM otherwise.
    backend.set_monitor_cpu_port(0x05); // & 0x02 == 0 -> KERNAL hidden.
    if (expect(backend.read(0xE000) == 0x11, "Frozen: $E000 must read RAM when KERNAL is hidden.")) return 1;

    // (2) BASIC visible at $A000 when (port & 0x03) == 0x03.
    backend.set_monitor_cpu_port(0x07);
    if (expect(backend.read(0xA000) == 0xBA, "Frozen: BASIC must be visible at $A000 with port=$07.")) return 1;
    backend.set_monitor_cpu_port(0x06); // & 0x03 != 0x03 -> BASIC hidden.
    if (expect(backend.read(0xA000) == 0xAA, "Frozen: $A000 must read RAM when BASIC is hidden.")) return 1;

    // (3) $0400-$0FFF returns the screen_backup byte, not live DRAM.
    if (expect(backend.read(0x0420) == 0x42, "Frozen: $0420 must come from the screen_backup region.")) return 1;

    // (4) Two consecutive reads at $E000 return the same byte.
    backend.set_monitor_cpu_port(0x07);
    uint8_t first_read = backend.read(0xE000);
    uint8_t second_read = backend.read(0xE000);
    if (expect(first_read == second_read, "Frozen: two consecutive $E000 reads must agree (no scroll drift).")) return 1;

    // (5) Write at $E000 while KERNAL is visible, switch to RAM-only bank,
    //     read $E000 returns the written byte.
    backend.set_monitor_cpu_port(0x07);
    backend.write(0xE000, 0x99);
    if (expect(backend.kernal[0] == 0x4E, "Frozen: write under KERNAL must not modify ROM.")) return 1;
    backend.set_monitor_cpu_port(0x05); // KERNAL hidden.
    if (expect(backend.read(0xE000) == 0x99, "Frozen: write-under-ROM must persist as RAM and survive bank switch.")) return 1;

    // (6) Frozen + IO-selected bank: source_name reports IO and IO is reachable.
    backend.set_monitor_cpu_port(0x07);
    if (expect(strcmp(backend.source_name(0xD020), "IO") == 0,
               "Frozen: source_name($D020) must remain IO with IO bank selected.")) return 1;
    backend.write(0xD020, 0x05);
    if (expect(backend.io[0xD020 - 0xD000] == 0x05, "Frozen: IO write must reach the IO region.")) return 1;
    if (expect(backend.read(0xD020) == 0x05, "Frozen: IO read must return the freshly written byte.")) return 1;

    return 0;
}

static int test_kernal_disassembly_mapping(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char line[39];
    monitor_reset_saved_state();

    const uint8_t kernal_bytes[] = {
        0x85, 0x56, 0x20, 0x0F, 0xBC, 0xA5, 0x61, 0xC9,
        0x88, 0x90, 0x03, 0x20, 0xD4, 0xBA, 0x20, 0xCC,
        0xBC, 0xA5, 0x07
    };
    const uint8_t ram_bytes[] = {
        0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF,
        0x18, 0x00, 0x00
    };
    const int keys[] = { 'J', 'D', 'o', KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);

    memset(backend.ram + 0xE000, 0x00, 0x40);
    memcpy(backend.kernal, kernal_bytes, sizeof(kernal_bytes));
    memcpy(backend.ram + 0xE000, ram_bytes, sizeof(ram_bytes));

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto to E000 failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Disassembly view switch failed.")) return 1;

    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E000 85 56") == line, "Visible KERNAL bytes at E000 are incorrect.")) return 1;
    if (expect(strstr(line, "STA $56") != NULL, "Visible KERNAL disassembly at E000 is incorrect.")) return 1;
    if (expect(strstr(line, "[KERNAL]") != NULL, "Visible KERNAL source annotation missing.")) return 1;
    if (expect(screen.reverse_chars[4][1], "Disassembly view did not highlight the selected instruction.")) return 1;

    screen.get_slice(1, 5, 38, line);
    if (expect(strstr(line, "E002 20 0F BC") == line, "Visible KERNAL bytes at E002 are incorrect.")) return 1;
    if (expect(strstr(line, "JSR $BC0F") != NULL, "Visible KERNAL disassembly at E002 is incorrect.")) return 1;
    if (expect(strstr(line, "[KERNAL]") != NULL, "E002 KERNAL source annotation missing.")) return 1;

    screen.get_slice(1, 6, 38, line);
    if (expect(strstr(line, "E005 A5 61") == line, "Visible KERNAL bytes at E005 are incorrect.")) return 1;
    if (expect(strstr(line, "LDA $61") != NULL, "Visible KERNAL disassembly at E005 is incorrect.")) return 1;

    screen.get_slice(1, 7, 38, line);
    if (expect(strstr(line, "E007 C9 88") == line, "Visible KERNAL bytes at E007 are incorrect.")) return 1;
    if (expect(strstr(line, "CMP #$88") != NULL, "Visible KERNAL disassembly at E007 is incorrect.")) return 1;

    screen.get_slice(1, 8, 38, line);
    if (expect(strstr(line, "E009 90 03") == line, "Visible KERNAL bytes at E009 are incorrect.")) return 1;
    if (expect(strstr(line, "BCC $E00E") != NULL, "Visible KERNAL disassembly at E009 is incorrect.")) return 1;

    screen.get_slice(1, 9, 38, line);
    if (expect(strstr(line, "E00B 20 D4 BA") == line, "Visible KERNAL bytes at E00B are incorrect.")) return 1;
    if (expect(strstr(line, "JSR $BAD4") != NULL, "Visible KERNAL disassembly at E00B is incorrect.")) return 1;

    screen.get_slice(1, 10, 38, line);
    if (expect(strstr(line, "E00E 20 CC BC") == line, "Visible KERNAL bytes at E00E are incorrect.")) return 1;
    if (expect(strstr(line, "JSR $BCCC") != NULL, "Visible KERNAL disassembly at E00E is incorrect.")) return 1;

    screen.get_slice(1, 11, 38, line);
    if (expect(strstr(line, "E011 A5 07") == line, "Visible KERNAL bytes at E011 are incorrect.")) return 1;
    if (expect(strstr(line, "LDA $07") != NULL, "Visible KERNAL disassembly at E011 is incorrect.")) return 1;

    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU0 failed.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E000 00") == line, "RAM-under-ROM bytes at E000 are incorrect.")) return 1;
    if (expect(strstr(line, "BRK") != NULL, "RAM-under-ROM disassembly at E000 is incorrect.")) return 1;
    if (expect(strstr(line, "[RAM]") != NULL, "RAM-under-ROM source annotation missing.")) return 1;

    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after KERNAL mapping test.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_disassembly_instruction_stepping(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char line[39];
    monitor_reset_saved_state();

    const uint8_t kernal_bytes[] = {
        0x85, 0x56, 0x20, 0x0F, 0xBC, 0xA5, 0x61, 0xC9,
        0x88, 0x90, 0x03, 0x20, 0xD4, 0xBA, 0x20, 0xCC,
        0xBC, 0xA5, 0x07
    };
    const int keys[] = { 'J', 'D', KEY_DOWN, KEY_DOWN, KEY_UP, KEY_UP, KEY_BREAK };
    FakeKeyboard keyboard(keys, 7);

    memcpy(backend.kernal, kernal_bytes, sizeof(kernal_bytes));
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto to E000 failed for disassembly stepping test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Disassembly view switch failed for stepping test.")) return 1;

    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASM $E000") == line, "Disassembly view must start at the goto address.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E000 85 56") == line, "Initial disassembly row mismatch at E000.")) return 1;

    if (expect(monitor.poll(0) == 0, "First disassembly down-step failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASM $E002") == line, "Disassembly header must follow the active cursor after one down-step.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E000 85 56") == line, "First disassembly down-step must keep the previous row visible.")) return 1;
    screen.get_slice(1, 5, 38, line);
    if (expect(strstr(line, "E002 20 0F BC") == line, "First disassembly down-step landed on the wrong instruction.")) return 1;

    if (expect(monitor.poll(0) == 0, "Second disassembly down-step failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASM $E005") == line, "Disassembly header must follow the active cursor after two down-steps.")) return 1;
    screen.get_slice(1, 6, 38, line);
    if (expect(strstr(line, "E005 A5 61") == line, "Second disassembly down-step landed on the wrong instruction.")) return 1;

    if (expect(monitor.poll(0) == 0, "First disassembly up-step failed.")) return 1;
    screen.get_slice(1, 5, 38, line);
    if (expect(strstr(line, "E002 20 0F BC") == line, "First disassembly up-step landed on the wrong instruction.")) return 1;

    if (expect(monitor.poll(0) == 0, "Second disassembly up-step failed.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E000 85 56") == line, "Second disassembly up-step landed on the wrong instruction.")) return 1;

    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after disassembly stepping test.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_disassembly_boundary_cutover(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char header[39];
    char line[39];
    int keys[32];
    int key_count = 0;
    int row_before_wrap = -1;
    int row_after_wrap = -1;
    int row_before_up = -1;
    int row_after_up = -1;

    keys[key_count++] = 'J';
    keys[key_count++] = 'A';
    for (int i = 0; i < 17; i++) {
        keys[key_count++] = KEY_DOWN;
    }
    keys[key_count++] = KEY_DOWN;
    keys[key_count++] = KEY_UP;
    keys[key_count++] = KEY_BREAK;
    FakeKeyboard keyboard(keys, key_count);

    monitor_reset_saved_state();
    memset(backend.memory, 0xEA, sizeof(backend.memory));
    for (uint32_t address = 0xFFEE; address <= 0xFFFF; address++) {
        backend.write((uint16_t)address, 0xEA);
    }
    backend.write(0xFFFE, 0x48);
    backend.write(0xFFFF, 0x4C);
    backend.write(0x0000, 0xEA);
    backend.write(0x0001, 0xEA);

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("FFEE", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Disassembly boundary test: goto failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Disassembly boundary test: ASM view switch failed.")) return 1;

    for (int i = 0; i < 17; i++) {
        if (expect(monitor.poll(0) == 0, "Disassembly boundary test: step toward $FFFF failed.")) return 1;
    }
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $FFFF") == header,
               "Scrolling down near the boundary must land on $FFFF before cutover.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_before_wrap),
               "Disassembly boundary test: highlighted row not found at $FFFF.")) return 1;
    if (expect(row_before_wrap == 21,
               "The $FFFF line must appear on the bottom visible row before cutover.")) return 1;
    screen.get_slice(1, row_before_wrap, 38, line);
    if (expect(strstr(line, "FFFF 4C") == line,
               "The $FFFF row must render only the local tail byte before cutover.")) return 1;
    if (expect(strstr(line, "???") != NULL,
               "The $FFFF row must not decode across the $FFFF/$0000 boundary.")) return 1;

    if (expect(monitor.poll(0) == 0, "Disassembly boundary test: forward cutover failed.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $0000") == header,
               "Stepping past $FFFF must cut over to $0000.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_after_wrap),
               "Disassembly boundary test: highlighted row not found after forward cutover.")) return 1;
    if (expect(row_after_wrap == 4,
               "Forward cutover must redraw with $0000 on the first visible row.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "0000 EA") == line,
               "The first visible row after forward cutover must start at $0000.")) return 1;

    if (expect(find_highlighted_cell(screen, NULL, &row_before_up),
               "Disassembly boundary test: highlighted row not found before reverse cutover.")) return 1;
    if (expect(row_before_up == 4,
               "Reverse cutover must start from the first visible row at $0000.")) return 1;

    if (expect(monitor.poll(0) == 0, "Disassembly boundary test: reverse cutover failed.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $FFFF") == header,
               "Stepping up from $0000 must cut over to $FFFF.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_after_up),
               "Disassembly boundary test: highlighted row not found after reverse cutover.")) return 1;
    if (expect(row_after_up == 21,
               "Reverse cutover must redraw with $FFFF on the bottom visible row.")) return 1;
    screen.get_slice(1, row_after_up, 38, line);
    if (expect(strstr(line, "FFFF 4C") == line,
               "Reverse cutover must restore the $FFFF tail row at the bottom.")) return 1;

    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after disassembly boundary test.")) return 1;
    monitor.deinit();

    return 0;
}

static int test_disassembly_reverse_cutover_keeps_ffff_at_bottom(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char header[39];
    char line[39];
    int row = -1;
    const int keys[] = { 'A', 'J', KEY_UP, KEY_BREAK };
    FakeKeyboard keyboard(keys, sizeof(keys) / sizeof(keys[0]));

    monitor_reset_saved_state();
    memset(backend.memory, 0xEA, sizeof(backend.memory));
    for (uint16_t address = 0xFFEE; address <= 0xFFFC; address = (uint16_t)(address + 3)) {
        backend.write(address, 0x20);
        backend.write((uint16_t)(address + 1), 0x00);
        backend.write((uint16_t)(address + 2), 0x00);
    }
    backend.write(0xFFFD, 0xEA);
    backend.write(0xFFFE, 0x48);
    backend.write(0xFFFF, 0x4C);
    backend.write(0x0000, 0xEA);
    backend.write(0x0001, 0xEA);

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("0000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Disassembly reverse boundary test: ASM view switch failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Disassembly reverse boundary test: goto failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Disassembly reverse boundary test: step up failed.")) return 1;

    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $FFFF") == header,
               "Reverse cutover from $0000 must land on $FFFF in ASM view.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row),
               "Disassembly reverse boundary test: highlighted row not found after wrap.")) return 1;
    if (expect(row == 21,
               "ASM reverse cutover must keep $FFFF on the bottom visible row even when earlier rows are multi-byte instructions.")) return 1;

    screen.get_slice(1, 19, 38, line);
    if (expect(strstr(line, "FFFD EA") == line,
               "ASM reverse cutover must keep the tail byte rows directly above $FFFF.")) return 1;
    screen.get_slice(1, 20, 38, line);
    if (expect(strstr(line, "FFFE 48") == line,
               "ASM reverse cutover must show $FFFE immediately above $FFFF.")) return 1;
    screen.get_slice(1, 21, 38, line);
    if (expect(strstr(line, "FFFF 4C") == line,
               "ASM reverse cutover must leave $FFFF on the bottom row without drawing wrapped rows below it.")) return 1;

    if (expect(monitor.poll(0) == 1, "Disassembly reverse boundary test: exit failed.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_hex_reverse_wrap_keeps_tail_row_at_bottom(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char header[39];
    char line[39];
    int row = -1;
    const int keys[] = { 'J', KEY_LEFT, KEY_BREAK };
    FakeKeyboard keyboard(keys, sizeof(keys) / sizeof(keys[0]));

    monitor_reset_saved_state();
    memset(backend.memory, 0xEA, sizeof(backend.memory));
    backend.write(0xFFFF, 0x4C);

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("0000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "HEX reverse wrap test: goto failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "HEX reverse wrap test: left wrap failed.")) return 1;

    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR HEX $FFFF") == header,
               "HEX reverse wrap must land on $FFFF.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row),
               "HEX reverse wrap test: highlighted cell not found after wrap.")) return 1;
    if (expect(row == 21,
               "HEX reverse wrap must keep the tail memory row on the bottom of the screen.")) return 1;
    screen.get_slice(1, 21, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "FFF8") == line,
               "HEX reverse wrap must show the final memory row at the bottom after wrapping to $FFFF.")) return 1;

    if (expect(monitor.poll(0) == 1, "HEX reverse wrap test: exit failed.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_last_address_row_highlight(void)
{
    struct ViewHighlightCase {
        const char *name;
        const char *expected_header;
        const int *keys;
        int key_count;
    };

    static const int hex_keys[] = { 'J', KEY_BREAK };
    static const int ascii_keys[] = { 'I', 'J', KEY_BREAK };
    static const int screen_keys[] = { 'V', 'J', KEY_BREAK };
    static const int binary_keys[] = { 'B', 'J', KEY_BREAK };
    static const int asm_keys[] = { 'A', 'J', KEY_BREAK };
    static const ViewHighlightCase cases[] = {
        { "HEX", "MONITOR HEX $FFFF", hex_keys, (int)(sizeof(hex_keys) / sizeof(hex_keys[0])) },
        { "ASC", "MONITOR ASC $FFFF", ascii_keys, (int)(sizeof(ascii_keys) / sizeof(ascii_keys[0])) },
        { "SCR", "MONITOR SCR U/G $FFFF", screen_keys, (int)(sizeof(screen_keys) / sizeof(screen_keys[0])) },
        { "BIN", "MONITOR BIN $FFFF/7", binary_keys, (int)(sizeof(binary_keys) / sizeof(binary_keys[0])) },
        { "ASM", "MONITOR ASM $FFFF", asm_keys, (int)(sizeof(asm_keys) / sizeof(asm_keys[0])) },
    };

    char message[128];
    char header[39];

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        FakeKeyboard keyboard(cases[i].keys, cases[i].key_count);
        int row = 0;

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        ui.set_prompt("FFFF", 1);

        monitor_reset_saved_state();
        backend.write(0xFFFF, 0x4C);
        backend.write(0x0000, 0xEA);
        backend.write(0x0001, 0xEA);

        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        for (int step = 0; step < cases[i].key_count - 1; step++) {
            sprintf(message, "%s $FFFF highlight test: command step %d failed.", cases[i].name, step + 1);
            if (expect(monitor.poll(0) == 0, message)) return 1;
        }

        screen.get_slice(1, 3, 38, header);
        sprintf(message, "%s $FFFF highlight test: header must stay on the active address.", cases[i].name);
        if (expect(strstr(header, cases[i].expected_header) == header, message)) return 1;
        sprintf(message, "%s $FFFF highlight test: highlighted cell not found.", cases[i].name);
        if (expect(find_highlighted_cell(screen, NULL, &row), message)) return 1;
        sprintf(message, "%s $FFFF highlight test: highlight must remain on the first visible row after goto.", cases[i].name);
        if (expect(row == 4, message)) return 1;

        sprintf(message, "%s $FFFF highlight test: exit failed.", cases[i].name);
        if (expect(monitor.poll(0) == 1, message)) return 1;
        monitor.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'J', 'e', KEY_BREAK, KEY_BREAK };
        FakeKeyboard keyboard(keys, sizeof(keys) / sizeof(keys[0]));
        int row = 0;

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        ui.set_prompt("FFFF", 1);

        monitor_reset_saved_state();
        backend.write(0xFFFF, 0x4C);
        backend.write(0x0000, 0xEA);
        backend.write(0x0001, 0xEA);

        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);
        if (expect(monitor.poll(0) == 0, "HEX edit $FFFF cursor test: goto failed.")) return 1;
        if (expect(monitor.poll(0) == 0, "HEX edit $FFFF cursor test: edit mode entry failed.")) return 1;
        if (expect(find_highlighted_cell(screen, NULL, &row),
                   "HEX edit $FFFF cursor test: highlighted cell not found.")) return 1;
        if (expect(row == 4,
                   "HEX edit $FFFF cursor test: highlighted byte must remain visible on the first row.")) return 1;
        if (expect(screen.reverse_chars[4][27] && screen.reverse_chars[4][28],
                   "HEX edit $FFFF cursor test: both digits of the active byte must stay highlighted.")) return 1;
        if (expect(monitor.poll(0) == 0, "HEX edit $FFFF cursor test: first RUN/STOP should leave edit mode.")) return 1;
        if (expect(monitor.poll(0) == 1, "HEX edit $FFFF cursor test: second RUN/STOP should exit the monitor.")) return 1;
        monitor.deinit();
    }

    return 0;
}

static int test_template_cursor(void)
{
    char template_buffer[8] = "AAAA";
    const int keys[] = { 'E', KEY_RETURN };
    FakeKeyboard keyboard(keys, 2);
    CaptureScreen screen;
    CaptureWindow window(&screen, 8);
    UIStringEdit edit(template_buffer, 7, true);

    edit.init(&window, &keyboard, 0, 0, 8);
    if (expect(window.last_x == 0 && window.last_y == 0, "Template cursor did not start at first editable character.")) return 1;
    if (expect(edit.poll(0) == 0, "Template edit returned early.")) return 1;
    if (expect(strcmp(template_buffer, "E") == 0, "Template input did not replace placeholder immediately.")) return 1;
    if (expect(edit.poll(0) == 1, "Template edit did not accept return.")) return 1;
    return 0;
}

static int test_task_action_lookup(void)
{
    TaskActionProvider provider;

    TaskMenu::ensure_task_actions_created(true);
    if (expect(provider.update_called && provider.last_writable, "Task action update hook was not called.")) return 1;
    if (expect(provider.monitor_action != NULL, "Task action provider did not create the monitor action.")) return 1;
    if (expect(provider.monitor_action->isPersistent(), "Task action should be made persistent.")) return 1;
    if (expect(get_machine_monitor_task(TEST_SUBSYSID_U64) == provider.monitor_action,
               "U64 task action lookup failed.")) return 1;
    if (expect(get_machine_monitor_task(TEST_SUBSYSID_C64) == provider.monitor_action,
               "C64 task action lookup failed.")) return 1;
    return 0;
}

static int test_monitor_renders_window_border(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    const int keys[] = { KEY_BREAK };
    FakeKeyboard keyboard(keys, 1);
    monitor_reset_saved_state();

    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);
    if (expect(backend.session_begin_count == 1, "Monitor must begin a backend session when it opens.")) return 1;
    if (expect((unsigned char)screen.chars[2][0] == BORD_LOWER_RIGHT_CORNER, "Monitor must draw the same shared upper-left border corner as the hex editor.")) return 1;
    if (expect((unsigned char)screen.chars[2][39] == BORD_LOWER_LEFT_CORNER, "Monitor must draw the same shared upper-right border corner as the hex editor.")) return 1;
    if (expect((unsigned char)screen.chars[23][0] == BORD_UPPER_RIGHT_CORNER, "Monitor must draw the shared lower-left border corner.")) return 1;
    if (expect((unsigned char)screen.chars[23][39] == BORD_UPPER_LEFT_CORNER, "Monitor must draw the shared lower-right border corner.")) return 1;
    for (int y = 3; y < 23; y++) {
        if (expect((unsigned char)screen.chars[y][0] == CHR_VERTICAL_LINE, "Monitor left border must be drawn for every row inside the frame.")) return 1;
        if (expect((unsigned char)screen.chars[y][39] == CHR_VERTICAL_LINE, "Monitor right border must be drawn for every row inside the frame.")) return 1;
    }
    if (expect(monitor.poll(0) == 1, "Monitor must close on RUN/STOP after border verification.")) return 1;
    monitor.deinit();
    if (expect(backend.session_end_count == 1, "Monitor must end the backend session when it closes.")) return 1;
    return 0;
}

static int test_monitor_byte_to_address_invariant(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    monitor_reset_saved_state();

    for (uint32_t addr = 0; addr < 0x10000; addr++) {
        backend.write((uint16_t)addr, (uint8_t)((addr >> 3) & 0xFF));
    }

    const int keys[] = { KEY_PAGEDOWN, KEY_PAGEDOWN, KEY_PAGEDOWN, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);

    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    for (int step = 0; step < 3; step++) {
        if (expect(monitor.poll(0) == 0, "Page-down failed during byte-to-address invariant scan.")) return 1;
        for (int row = 0; row < 16; row++) {
            char line[MONITOR_HEX_ROW_CHARS + 1];
            screen.get_slice(1, 4 + row, MONITOR_HEX_ROW_CHARS, line);
            uint16_t row_addr = 0;
            for (int n = 0; n < 4; n++) {
                char c = line[n];
                uint16_t digit = 0;
                if (c >= '0' && c <= '9') digit = (uint16_t)(c - '0');
                else if (c >= 'a' && c <= 'f') digit = (uint16_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = (uint16_t)(c - 'A' + 10);
                row_addr = (uint16_t)((row_addr << 4) | digit);
            }
            for (int b = 0; b < MONITOR_HEX_BYTES_PER_ROW; b++) {
                int hi_pos = 5 + (3 * b);
                int lo_pos = hi_pos + 1;
                char hi = line[hi_pos];
                char lo = line[lo_pos];
                uint8_t parsed = 0;
                uint8_t hi_v = (hi >= '0' && hi <= '9') ? (hi - '0') : ((hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10) : (hi - 'A' + 10));
                uint8_t lo_v = (lo >= '0' && lo <= '9') ? (lo - '0') : ((lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10) : (lo - 'A' + 10));
                parsed = (uint8_t)((hi_v << 4) | lo_v);
                uint16_t cell_addr = (uint16_t)(row_addr + b);
                uint8_t expected = backend.read(cell_addr);
                if (expect(parsed == expected, "DISPLAY(addr) must equal CANONICAL_READ(addr) for every visible cell.")) return 1;
            }
        }
    }
    if (expect(monitor.poll(0) == 1, "Monitor must close on RUN/STOP after byte invariant scan.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_cursor_header_and_scroll(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    enum { visible_rows = 18 };
    char before_rows[visible_rows][MONITOR_HEX_ROW_CHARS + 1];
    char after_rows[visible_rows][MONITOR_HEX_ROW_CHARS + 1];
    char header[39];
    int keys[visible_rows + 1];
    monitor_reset_saved_state();

    for (uint32_t addr = 0; addr < 0x10000; addr++) {
        backend.write((uint16_t)addr, (uint8_t)((addr >> 3) & 0xFF));
    }
    for (int i = 0; i < visible_rows; i++) {
        keys[i] = KEY_DOWN;
    }
    keys[visible_rows] = KEY_BREAK;

    FakeKeyboard keyboard(keys, visible_rows + 1);
    ui.screen = &screen;
    ui.keyboard = &keyboard;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR HEX $0000") == header, "Header must report the active cursor address.")) return 1;

    for (int i = 0; i < visible_rows - 1; i++) {
        if (expect(monitor.poll(0) == 0, "Down navigation failed before viewport scroll.")) return 1;
    }
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR HEX $0088") == header, "Header must follow the cursor before the viewport scrolls.")) return 1;
    for (int row = 0; row < visible_rows; row++) {
        screen.get_slice(1, 4 + row, MONITOR_HEX_ROW_CHARS, before_rows[row]);
    }
    if (expect(strstr(before_rows[visible_rows - 1], "0088 ") == before_rows[visible_rows - 1] ||
               strstr(before_rows[visible_rows - 1], "0088") == before_rows[visible_rows - 1],
               "Hex view should use the reclaimed row above the CPU/VIC status line.")) return 1;

    if (expect(monitor.poll(0) == 0, "Down navigation failed at the viewport boundary.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR HEX $0090") == header, "Header must follow the cursor when the viewport scrolls.")) return 1;
    for (int row = 0; row < visible_rows; row++) {
        screen.get_slice(1, 4 + row, MONITOR_HEX_ROW_CHARS, after_rows[row]);
    }
    for (int row = 1; row < visible_rows; row++) {
        if (expect(strcmp(after_rows[row - 1], before_rows[row]) == 0,
                   "Scrolling down must shift each rendered row up by exactly one row span.")) return 1;
    }
    if (expect(monitor.poll(0) == 1, "Monitor must close on RUN/STOP after viewport scroll verification.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_interaction(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    FakeBankedMemoryBackend banked_backend;
    char line[MONITOR_DISASM_ROW_CHARS + 1];
    char status[39];
    static const char *expected_help_lines[] = {
        "",
        "M Memory    I ASCII     V Screen",
        "A Assembly  B Binary    U Undoc/Case",
        "J Jump      G Go",
        "",
        "E Edit      F Fill      T Transfer",
        "C Compare   H Hunt      N Number",
        "W Width     R Range     P Poll",
        "Z Freeze    O CPU Bank  Sh+O VIC",
        "L Load      S Save",
        "",
        "Bookmarks:",
        "C=+B List   C=+0-9 Jump",
        "",
        "Open monitor:  C=+O",
        "Close monitor: C=+O / ESC",
        "Leave edit:    C=+E / ESC",
        "Copy/Paste:    C=+C / C=+V",
        NULL
    };
    monitor_reset_saved_state();

    for (int i = 0; i < MONITOR_TEXT_BYTES_PER_ROW * 18; i++) {
        backend.write((uint16_t)i, (uint8_t)('A' + ((i / MONITOR_TEXT_BYTES_PER_ROW) % 26)));
    }

    banked_backend.basic[0] = 0xBA;
    banked_backend.ram[0xA000] = 0xAA;
    banked_backend.io[0xD000 - 0xD000] = 0x10;
    banked_backend.kernal[0] = 0x4C;

    const int help_keys[] = { 'J', 'o', 'o', KEY_F3, KEY_F3, KEY_BREAK };
    FakeKeyboard help_keyboard(help_keys, 6);
    ui.screen = &screen;
    ui.keyboard = &help_keyboard;
    ui.set_prompt("A000", 1);

    BackendMachineMonitor help_monitor(&ui, &banked_backend);
    help_monitor.init(&screen, &help_keyboard);
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR HEX $0000") == line && strstr(line, "CPU MAP") == NULL,
               "Header must show only the view type and cursor address.")) return 1;
    if (expect(screen.reverse_chars[4][6] && screen.reverse_chars[4][7], "Hex view did not highlight the selected byte.")) return 1;
    if (expect(help_monitor.poll(0) == 0, "Goto to A000 failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR HEX $A000") == line && strstr(line, "CPU MAP") == NULL,
               "Goto must update the header to the cursor address without CPU map text.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 ba", 7) == 0 || strncmp(line, "A000 BA", 7) == 0, "CPU banked BASIC view mismatch.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000") == status,
               "Initial CPU/VIC status line mismatch.")) return 1;

    if (expect(help_monitor.poll(0) == 0, "CPU bank cycle to CPU0 failed.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 aa", 7) == 0 || strncmp(line, "A000 AA", 7) == 0, "CPU0 should expose RAM at A000.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000") == status,
               "CPU0 status did not update after O.")) return 1;

    if (expect(help_monitor.poll(0) == 0, "CPU bank cycle to CPU1 failed.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 aa", 7) == 0 || strncmp(line, "A000 AA", 7) == 0, "CPU1 should keep A000 in RAM.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU1 $A:RAM $D:CHR $E:RAM VIC2 $8000") == status,
               "CPU1 status did not update after O.")) return 1;

    if (expect(help_monitor.poll(0) == 0, "F3 help open failed.")) return 1;
    screen.get_slice(1, 3, 38, status);
    if (expect(strstr(status, "HELP") == status, "Help header should replace the normal view header.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "Paging:         F1/Sh+Sp  F7/Space") == status,
               "Help view should show the paging shortcuts on the footer row.")) return 1;
    {
        for (int i = 0; expected_help_lines[i]; i++) {
            screen.get_slice(1, 4 + i, 38, line);
            if (expect(strncmp(line, expected_help_lines[i], strlen(expected_help_lines[i])) == 0,
                       "Help view text did not match the requested layout.")) return 1;
        }
    }

    if (expect(help_monitor.poll(0) == 0, "F3 help close failed.")) return 1;
    screen.get_slice(1, 3, 38, status);
    if (expect(strstr(status, "MONITOR HEX $A000") == status, "Closing help should restore the normal header.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 aa", 7) == 0 || strncmp(line, "A000 AA", 7) == 0, "Help toggle did not restore the monitor view.")) return 1;

    {
        const int esc_help_keys[] = { KEY_F3, KEY_ESCAPE, KEY_ESCAPE };
        FakeKeyboard esc_help_keyboard(esc_help_keys, 3);
        ui.keyboard = &esc_help_keyboard;
        screen.clear();
        monitor_reset_saved_state();

        BackendMachineMonitor esc_help_monitor(&ui, &backend);
        esc_help_monitor.init(&screen, &esc_help_keyboard);
        if (expect(esc_help_monitor.poll(0) == 0, "F3 should open help before ESC handling is tested.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, "Paging:         F1/Sh+Sp  F7/Space") == status,
                   "Help must show the paging shortcuts before ESC closes it.")) return 1;
        for (int i = 0; expected_help_lines[i]; i++) {
            screen.get_slice(1, 4 + i, 38, line);
            if (expect(strncmp(line, expected_help_lines[i], strlen(expected_help_lines[i])) == 0,
                       "Help must be visible before ESC closes it.")) return 1;
        }
        if (expect(esc_help_monitor.poll(0) == 0, "ESC should close help without exiting the monitor.")) return 1;
        screen.get_slice(1, 3, 38, line);
        if (expect(strstr(line, "MONITOR HEX $0000") == line, "ESC should restore the normal monitor header after closing help.")) return 1;
        if (expect(esc_help_monitor.poll(0) == 1, "ESC should exit the monitor only after help is already closed.")) return 1;
        esc_help_monitor.deinit();
    }

    ui.set_prompt("E013", 1);
    const int goto_disasm_keys[] = { 'J', 'D', KEY_BREAK };
    FakeKeyboard goto_disasm_keyboard(goto_disasm_keys, 3);
    ui.keyboard = &goto_disasm_keyboard;
    screen.clear();
    monitor_reset_saved_state();

    BackendMachineMonitor goto_disasm_monitor(&ui, &banked_backend);
    goto_disasm_monitor.init(&screen, &goto_disasm_keyboard);
    if (expect(goto_disasm_monitor.poll(0) == 0, "Goto to E013 failed for disassembly anchoring test.")) return 1;
    if (expect(goto_disasm_monitor.poll(0) == 0, "Disassembly view switch failed for E013 anchoring test.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "E013 18") != line, "Disassembly view should anchor the top row at the goto address.")) return 1;
    if (expect(goto_disasm_monitor.poll(0) == 1, "RUN/STOP exit failed after disassembly anchoring test.")) return 1;
    goto_disasm_monitor.deinit();

    if (expect(help_monitor.poll(0) == 1, "RUN/STOP exit failed.")) return 1;
    help_monitor.deinit();

    {
        FakeKeyboard idle_keyboard(NULL, 0);
        ui.keyboard = &idle_keyboard;
        screen.clear();
        banked_backend.live_cpu_port = 0x00;
        banked_backend.live_cpu_ddr = 0x07;
        banked_backend.live_dd00 = 0x01;
        monitor_reset_saved_state();

        BackendMachineMonitor vic_monitor(&ui, &banked_backend);
        vic_monitor.init(&screen, &idle_keyboard);
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000") == status,
                   "Idle VIC refresh test should preserve the selected default CPU7 bank.")) return 1;
        banked_backend.live_dd00 = 0x00;
        if (expect(vic_monitor.poll(0) == 0, "VIC idle refresh poll failed.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC3 $C000") == status,
                   "VIC status did not refresh after DD00 change.")) return 1;
        vic_monitor.deinit();
    }

    {
        const int live_bank_keys[] = { 'J' };
        FakeKeyboard live_bank_keyboard(live_bank_keys, 1);
        ui.keyboard = &live_bank_keyboard;
        ui.set_prompt("A000", 1);
        screen.clear();
        banked_backend.live_cpu_port = 0x01;
        banked_backend.live_cpu_ddr = 0x00;
        banked_backend.live_dd00 = 0x01;
        monitor_reset_saved_state();

        BackendMachineMonitor live_bank_monitor(&ui, &banked_backend);
        live_bank_monitor.init(&screen, &live_bank_keyboard);
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000") == status,
                   "Fresh monitor sessions should still default to CPU7 even when the live machine is banked differently.")) return 1;
        if (expect(live_bank_monitor.poll(0) == 0, "Goto to A000 failed for default CPU7 test.")) return 1;
        screen.get_slice(1, 4, 8, line);
        if (expect(strncmp(line, "a000 ba", 7) == 0 || strncmp(line, "A000 BA", 7) == 0,
                   "Default CPU7 should keep BASIC visible at A000.")) return 1;
        live_bank_monitor.deinit();
    }

    const int blink_keys[] = { 'I', 'e' };
    FakeKeyboard blink_keyboard(blink_keys, 2);
    ui.keyboard = &blink_keyboard;
    screen.clear();
    monitor_reset_saved_state();

    BackendMachineMonitor blink_monitor(&ui, &backend);
    blink_monitor.init(&screen, &blink_keyboard);
    if (expect(blink_monitor.poll(0) == 0, "ASCII view command failed for blink test.")) return 1;
    if (expect(blink_monitor.poll(0) == 0, "ASCII edit mode entry via 'e' failed.")) return 1;
    if (expect(screen.reverse_chars[4][6], "ASCII edit did not initially show the active cursor.")) return 1;
    if (expect(blink_monitor.poll(0) == 0 && blink_monitor.poll(0) == 0 && blink_monitor.poll(0) == 0,
               "Idle highlight polling failed.")) return 1;
    if (expect(screen.reverse_chars[4][6], "ASCII edit highlight should stay deterministic during idle redraws.")) return 1;
    blink_monitor.deinit();

    const int view_keys[] = { 'I', KEY_DOWN, KEY_UP, 'e', 'Z', KEY_LEFT, 'Y', KEY_BREAK, KEY_BREAK };
    FakeKeyboard view_keyboard(view_keys, 9);
    ui.keyboard = &view_keyboard;
    ui.popup_count = 0;
    ui.navmode = 1;
    screen.clear();
    monitor_reset_saved_state();

    BackendMachineMonitor view_monitor(&ui, &backend);
    view_monitor.init(&screen, &view_keyboard);

    if (expect(view_monitor.poll(0) == 0, "ASCII view command failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASC $0000") == line, "ASCII header must start at the cursor address.")) return 1;
    screen.get_slice(1, 4, MONITOR_TEXT_ROW_CHARS, line);
    if (expect(strcmp(line, "0000 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == 0, "ASCII first-row rendering mismatch.")) return 1;
    screen.get_slice(1, 12, MONITOR_TEXT_ROW_CHARS, line);
    if (expect(strcmp(line, "0100 IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII") == 0, "ASCII later-row rendering mismatch.")) return 1;
    screen.get_slice(1, 21, MONITOR_TEXT_ROW_CHARS, line);
    if (expect(strcmp(line, "0220 RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR") == 0, "ASCII bottom-row rendering should use the reclaimed rows.")) return 1;
    if (expect((unsigned char)screen.chars[4][39] == CHR_VERTICAL_LINE, "ASCII view overwrote the right border.")) return 1;

    if (expect(view_monitor.poll(0) == 0, "ASCII down navigation failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASC $0020") == line, "ASCII header must follow the cursor within the viewport.")) return 1;
    if (expect(screen.reverse_chars[5][6], "ASCII down should move the cursor one 32-byte row without scrolling.")) return 1;
    if (expect(view_monitor.poll(0) == 0, "ASCII up navigation failed.")) return 1;
    if (expect(screen.reverse_chars[4][6], "ASCII up should move the cursor back one 32-byte row without scrolling.")) return 1;

    if (expect(view_monitor.poll(0) == 0, "ASCII edit mode entry via 'e' failed.")) return 1;
    if (expect(screen.reverse_chars[4][6], "ASCII edit did not highlight the active character.")) return 1;

    if (expect(view_monitor.poll(0) == 0, "ASCII edit write failed.")) return 1;
    if (expect(backend.read(0x0000) == 'Z', "ASCII edit did not write memory.")) return 1;
    if (expect(screen.reverse_chars[4][7], "ASCII edit did not advance the highlighted cursor.")) return 1;

    if (expect(view_monitor.poll(0) == 0, "ASCII left-arrow should navigate without leaving edit mode.")) return 1;
    if (expect(backend.read(0x0001) == 'A', "ASCII left-arrow should not be written as data.")) return 1;
    if (expect(screen.reverse_chars[4][6], "ASCII left-arrow must move the edit cursor to the previous byte.")) return 1;
    if (expect(view_monitor.poll(0) == 0, "ASCII edit must remain active after left-arrow navigation.")) return 1;
    if (expect(backend.read(0x0000) == 'Y', "ASCII edit should keep writing after moving left in edit mode.")) return 1;
    if (expect(screen.reverse_chars[4][7], "ASCII edit should continue to advance after writing post-left-arrow.")) return 1;
    if (expect(view_monitor.poll(0) == 0, "RUN/STOP should leave ASCII edit mode before closing the monitor.")) return 1;
    if (expect(view_monitor.poll(0) == 1, "RUN/STOP monitor exit failed after ASCII edit navigation.")) return 1;
    view_monitor.deinit();

    const int hex_keys[] = { 'e', 'A', 'B', KEY_LEFT, 'C', 'D', KEY_BREAK, KEY_BREAK };
    FakeKeyboard hex_keyboard(hex_keys, 8);
    ui.keyboard = &hex_keyboard;
    screen.clear();
    backend.write(0x0000, 0x00);
    monitor_reset_saved_state();

    BackendMachineMonitor hex_monitor(&ui, &backend);
    hex_monitor.init(&screen, &hex_keyboard);
    if (expect(hex_monitor.poll(0) == 0, "Hex edit mode entry via 'e' failed.")) return 1;
    if (expect(screen.reverse_chars[4][6] && screen.reverse_chars[4][7], "Hex edit did not highlight both byte digits.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "First hex nibble failed.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "0000 a0") == line || strstr(line, "0000 A0") == line,
               "First hex nibble did not update the visible byte immediately.")) return 1;
    if (expect(backend.read(0x0000) == 0x00, "First hex nibble should not commit memory yet.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "Second hex nibble failed.")) return 1;
    if (expect(backend.read(0x0000) == 0xAB, "Hex edit did not write the byte.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "0000 ab") == line || strstr(line, "0000 AB") == line,
               "Hex edit did not redraw the changed byte immediately.")) return 1;
    if (expect(screen.reverse_chars[4][9] && screen.reverse_chars[4][10], "Hex edit did not advance to the next byte.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "Left-arrow should navigate inside hex edit mode.")) return 1;
    if (expect(screen.reverse_chars[4][6] && screen.reverse_chars[4][7], "Hex left-arrow must move the edit cursor back to the previous byte.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "Hex edit must remain active after left-arrow navigation.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "Hex edit second byte write after left-arrow failed.")) return 1;
    if (expect(backend.read(0x0000) == 0xCD, "Hex edit should keep writing after moving left in edit mode.")) return 1;
    if (expect(hex_monitor.poll(0) == 0, "RUN/STOP should leave hex edit mode before closing the monitor.")) return 1;
    if (expect(hex_monitor.poll(0) == 1, "RUN/STOP exit after hex edit failed.")) return 1;
    hex_monitor.deinit();

    {
        const int rom_write_keys[] = { 'J', 'e', '5', '5', KEY_BREAK, KEY_BREAK };
        FakeKeyboard rom_write_keyboard(rom_write_keys, 6);
        ui.keyboard = &rom_write_keyboard;
        ui.set_prompt("A000", 1);
        screen.clear();
        banked_backend.basic[0] = 0xBA;
        banked_backend.ram[0xA000] = 0xAA;
        banked_backend.set_monitor_cpu_port(0x07);
        monitor_reset_saved_state();

        BackendMachineMonitor rom_write_monitor(&ui, &banked_backend);
        rom_write_monitor.init(&screen, &rom_write_keyboard);
        if (expect(rom_write_monitor.poll(0) == 0, "Goto to A000 failed for ROM-visible write test.")) return 1;
        if (expect(rom_write_monitor.poll(0) == 0, "ROM-visible edit mode entry failed.")) return 1;
        if (expect(rom_write_monitor.poll(0) == 0, "ROM-visible first nibble failed.")) return 1;
        if (expect(rom_write_monitor.poll(0) == 0, "ROM-visible second nibble failed.")) return 1;
        if (expect(banked_backend.ram[0xA000] == 0x55, "ROM-visible write must update the underlying RAM just like the canonical backend path.")) return 1;
        if (expect(banked_backend.basic[0] == 0xBA, "ROM-visible write must not overwrite BASIC ROM content.")) return 1;
        if (expect(rom_write_monitor.poll(0) == 0, "RUN/STOP should leave edit mode after ROM-visible write.")) return 1;
        if (expect(rom_write_monitor.poll(0) == 1, "RUN/STOP exit failed after ROM-visible write test.")) return 1;
        rom_write_monitor.deinit();
    }

    {
        const int exit_keys[] = { 'e', KEY_F3, KEY_F3, 'A', 'B', KEY_BREAK, KEY_BREAK };
        FakeKeyboard exit_keyboard(exit_keys, 7);
        ui.keyboard = &exit_keyboard;
        screen.clear();
        backend.write(0x0000, 0x00);
        monitor_reset_saved_state();

        BackendMachineMonitor exit_monitor(&ui, &backend);
        exit_monitor.init(&screen, &exit_keyboard);
        if (expect(exit_monitor.poll(0) == 0, "F3 edit-help setup via 'e' failed.")) return 1;
        if (expect(exit_monitor.poll(0) == 0, "F3 should toggle help while keeping edit mode.")) return 1;
        for (int i = 0; expected_help_lines[i]; i++) {
            screen.get_slice(1, 4 + i, 38, line);
            if (expect(strncmp(line, expected_help_lines[i], strlen(expected_help_lines[i])) == 0,
                       "F3 did not open help while editing.")) return 1;
        }
        if (expect(exit_monitor.poll(0) == 0, "F3 should close help while keeping edit mode.")) return 1;
        if (expect(exit_monitor.poll(0) == 0, "F3-preserved edit mode first nibble failed.")) return 1;
        if (expect(exit_monitor.poll(0) == 0, "F3-preserved edit mode second nibble failed.")) return 1;
        if (expect(backend.read(0x0000) == 0xAB, "F3 terminated edit mode instead of preserving it.")) return 1;
        if (expect(exit_monitor.poll(0) == 0, "RUN/STOP should exit edit mode before monitor.")) return 1;
        if (expect(exit_monitor.poll(0) == 1, "RUN/STOP monitor exit failed after edit-exit checks.")) return 1;
        exit_monitor.deinit();
    }

    {
        const int nav_keys[] = {
            KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
            KEY_PAGEUP, KEY_PAGEDOWN, KEY_F2, KEY_F7,
            KEY_F3, KEY_LEFT, KEY_PAGEUP, KEY_PAGEDOWN,
            KEY_BREAK
        };
        FakeKeyboard nav_keyboard(nav_keys, 13);
        ui.keyboard = &nav_keyboard;
        screen.clear();
        monitor_reset_saved_state();

        BackendMachineMonitor nav_monitor(&ui, &backend);
        nav_monitor.init(&screen, &nav_keyboard);
        for (int i = 0; i < 12; i++) {
            if (expect(nav_monitor.poll(0) == 0, "Global navigation key exited the monitor.")) return 1;
        }
        if (expect(nav_monitor.poll(0) == 1, "RUN/STOP exit failed after global navigation checks.")) return 1;
        nav_monitor.deinit();
    }

    {
        const int toggle_keys[] = { KEY_CTRL_O };
        FakeKeyboard toggle_keyboard(toggle_keys, 1);
        ui.keyboard = &toggle_keyboard;
        screen.clear();
        monitor_reset_saved_state();

        BackendMachineMonitor toggle_monitor(&ui, &backend);
        toggle_monitor.init(&screen, &toggle_keyboard);
        if (expect(toggle_monitor.poll(0) == 1, "CBM+O should close the monitor directly.")) return 1;
        toggle_monitor.deinit();
    }

    const int ignored_keys[] = { 'D', '.', 'R', ':', 'Q', 'P', KEY_BREAK };
    FakeKeyboard ignored_keyboard(ignored_keys, 7);
    ui.keyboard = &ignored_keyboard;
    screen.clear();
    ui.popup_count = 0;
    backend.write(0x0000, 0x00);
    monitor_reset_saved_state();

    BackendMachineMonitor ignored_monitor(&ui, &backend);
    ignored_monitor.init(&screen, &ignored_keyboard);
    if (expect(ignored_monitor.poll(0) == 0, "Disassembly view command failed before ignored-key checks.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASM $0000") == line, "Disassembly header setup failed before ignored-key checks.")) return 1;
    if (expect(ignored_monitor.poll(0) == 0, "'.' should be ignored now that left/right already handle byte stepping.")) return 1;
    screen.get_slice(1, 3, 38, status);
    if (expect(strstr(status, "MONITOR ASM $0000") == status, "'.' should not change the disassembly viewport.")) return 1;
    if (expect(ignored_monitor.poll(0) == 0, "'R' should be ignored now that register editing is unsupported.")) return 1;
    if (expect(ignored_monitor.poll(0) == 0, "':' should be ignored after 'E' takes over edit entry.")) return 1;
    if (expect(ignored_monitor.poll(0) == 0, "Q should be ignored while not editing.")) return 1;
    if (expect(ignored_monitor.poll(0) == 0, "P should be ignored, not handled as a command.")) return 1;
    if (expect(ui.popup_count == 0, "Removed peek/poke commands should not show popups.")) return 1;
    if (expect(backend.read(0x0000) == 0x00, "Removed edit commands should not write memory.")) return 1;
    if (expect(ignored_monitor.poll(0) == 1, "RUN/STOP exit failed on ignored-command monitor.")) return 1;
    ignored_monitor.deinit();

    for (int y = 3; y < 23; y++) {
        if (expect((unsigned char)screen.chars[y][39] == CHR_VERTICAL_LINE, "Monitor output overwrote the right border.")) return 1;
    }

    return 0;
}

static int test_monitor_default_cpu_bank_and_vic_shortcuts(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char line[MONITOR_HEX_ROW_CHARS + 1];
    char status[39];
    const int keys[] = {
        'J',
        'o', '.',
        'o', 'o', 'o', 'o', 'o', 'o', 'o', 'o',
        'O', 'O', 'O', 'O',
        KEY_BREAK
    };
    FakeKeyboard keyboard(keys, sizeof(keys) / sizeof(keys[0]));
    static const char *cpu_cycle_status[] = {
        "CPU1 $A:RAM $D:CHR $E:RAM VIC2 $8000",
        "CPU2 $A:RAM $D:CHR $E:KRN VIC2 $8000",
        "CPU3 $A:BAS $D:CHR $E:KRN VIC2 $8000",
        "CPU4 $A:RAM $D:RAM $E:RAM VIC2 $8000",
        "CPU5 $A:RAM $D:I/O $E:RAM VIC2 $8000",
        "CPU6 $A:RAM $D:I/O $E:KRN VIC2 $8000",
        "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000",
        "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000",
    };
    static const char *vic_cycle_status[] = {
        "CPU0 $A:RAM $D:RAM $E:RAM VIC3 $C000",
        "CPU0 $A:RAM $D:RAM $E:RAM VIC0 $0000",
        "CPU0 $A:RAM $D:RAM $E:RAM VIC1 $4000",
        "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000",
    };
    static const uint8_t vic_cycle_dd00[] = {
        0xA8, 0xAB, 0xAA, 0xA9,
    };

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("A000", 1);
    monitor_io::reset_fake_monitor_io();
    monitor_reset_saved_state();
    monitor_invalidate_saved_state();

    backend.live_cpu_port = 0x00;
    backend.live_cpu_ddr = 0x07;
    backend.live_dd00 = 0xA9;
    backend.basic[0] = 0xBA;
    backend.ram[0xA000] = 0xAA;

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000") == status,
               "Fresh monitor sessions must default to CPU7 regardless of the live machine bank.")) return 1;

    if (expect(monitor.poll(0) == 0, "Jump command failed for CPU/VIC shortcut test.")) return 1;
    if (expect(!monitor_io::g_monitor_io.jump_called, "J must change the monitor address without executing code.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 ba", 7) == 0 || strncmp(line, "A000 BA", 7) == 0,
               "Default CPU7 must show BASIC at A000.")) return 1;

    ui.popup_count = 0;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle from CPU7 to CPU0 failed.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000") == status,
               "Plain O must cycle from CPU7 to CPU0.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 aa", 7) == 0 || strncmp(line, "A000 AA", 7) == 0,
               "CPU0 must expose RAM at A000.")) return 1;
    if (expect(ui.popup_count == 0, "Plain O must not open a popup.")) return 1;

    if (expect(monitor.poll(0) == 0, "Unmapped key should be ignored after plain O.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000") == status,
               "Unmapped keys must not be captured for CPU bank selection.")) return 1;
    if (expect(ui.popup_count == 0, "Unmapped keys must not trigger CPU bank popups.")) return 1;

    for (unsigned int i = 0; i < sizeof(cpu_cycle_status) / sizeof(cpu_cycle_status[0]); i++) {
        if (expect(monitor.poll(0) == 0, "Ascending CPU bank cycle failed.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, cpu_cycle_status[i]) == status, "CPU bank cycle order must be CPU0..CPU7 in ascending order.")) return 1;
    }

    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "a000 aa", 7) == 0 || strncmp(line, "A000 AA", 7) == 0,
               "CPU0 must remain distinct and re-reachable after a full cycle.")) return 1;

    for (unsigned int i = 0; i < sizeof(vic_cycle_status) / sizeof(vic_cycle_status[0]); i++) {
        if (expect(monitor.poll(0) == 0, "Shift+O VIC bank cycle failed.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, vic_cycle_status[i]) == status, "Shift+O must cycle VIC0..VIC3 in ascending order.")) return 1;
        if (expect(backend.live_dd00 == vic_cycle_dd00[i], "Shift+O must write the actual DD00 VIC bank bits without clobbering unrelated CIA-2 state.")) return 1;
    }

    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after CPU/VIC shortcut test.")) return 1;
    monitor.deinit();
    monitor_reset_saved_state();
    return 0;
}

static int test_monitor_freeze_mode_vic_shortcut_override(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeFrozenVicBackend backend;
    char status[39];
    const int keys[] = { 'O', 'O', 'O', 'O', KEY_BREAK };
    FakeKeyboard keyboard(keys, sizeof(keys) / sizeof(keys[0]));
    static const char *vic_cycle_status[] = {
        "CPU7 $A:BAS $D:I/O $E:KRN VIC1 $4000",
        "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000",
        "CPU7 $A:BAS $D:I/O $E:KRN VIC3 $C000",
        "CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000",
    };
    static const uint8_t requested_vic_bank[] = { 1, 2, 3, 0 };

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    monitor_reset_saved_state();
    monitor_invalidate_saved_state();

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000") == status,
               "Freeze-mode VIC shortcut test must start from the freezer's VIC0 bank.")) return 1;

    for (unsigned int i = 0; i < sizeof(vic_cycle_status) / sizeof(vic_cycle_status[0]); i++) {
        if (expect(monitor.poll(0) == 0, "Freeze-mode Shift+O VIC bank cycle failed.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, vic_cycle_status[i]) == status,
                   "Freeze-mode Shift+O must keep rotating through the requested VIC banks even when live readback stays pinned.")) return 1;
        if (expect(backend.requested_vic_bank == requested_vic_bank[i],
                   "Freeze-mode Shift+O must still request the next VIC bank from the backend.")) return 1;
        if (expect(backend.set_live_vic_bank_calls == (int)i + 1,
                   "Freeze-mode Shift+O must invoke set_live_vic_bank on every keypress.")) return 1;
        if (expect(backend.get_live_vic_bank() == 0,
                   "Freeze-mode VIC override test assumes the backend readback remains pinned to VIC0.")) return 1;
    }

    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after freeze-mode VIC shortcut test.")) return 1;
    monitor.deinit();
    monitor_reset_saved_state();
    return 0;
}

static int test_monitor_reopen_restores_state(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[MONITOR_DISASM_ROW_CHARS + 1];
    char status[39];

    monitor_reset_saved_state();

    backend.write(0xC123, 0x07);
    backend.write(0xC124, 0x44);
    backend.write(0xC125, 0x00);

    {
        const int first_keys[] = { 'J', 'D', 'U', 'o', KEY_BREAK };
        FakeKeyboard first_keyboard(first_keys, 5);
        ui.screen = &screen;
        ui.keyboard = &first_keyboard;
        ui.set_prompt("C123", 1);

        BackendMachineMonitor first_monitor(&ui, &backend);
        first_monitor.init(&screen, &first_keyboard);
        if (expect(first_monitor.poll(0) == 0, "First monitor goto failed for reopen-state test.")) return 1;
        if (expect(first_monitor.poll(0) == 0, "First monitor view switch failed for reopen-state test.")) return 1;
        if (expect(first_monitor.poll(0) == 0, "First monitor undocumented-op toggle failed for reopen-state test.")) return 1;
        if (expect(first_monitor.poll(0) == 0, "First monitor CPU-bank cycle failed for reopen-state test.")) return 1;
        if (expect(first_monitor.poll(0) == 1, "First monitor exit failed for reopen-state test.")) return 1;
        first_monitor.deinit();
    }

    screen.clear();

    {
        const int second_keys[] = { KEY_BREAK };
        FakeKeyboard second_keyboard(second_keys, 1);
        ui.keyboard = &second_keyboard;

        BackendMachineMonitor second_monitor(&ui, &backend);
        second_monitor.init(&screen, &second_keyboard);
        screen.get_slice(1, 3, 38, line);
        if (expect(strstr(line, "MONITOR ASM $C123") == line, "Reopened monitor did not restore the disassembly view and address.")) return 1;
        screen.get_slice(1, 4, 38, line);
        if (expect(strstr(line, "C123 07 44") == line, "Reopened monitor did not restore the saved current address.")) return 1;
        if (expect(strstr(line, "SLO $44") != NULL, "Reopened monitor did not restore the illegal-opcodes setting.")) return 1;
        screen.get_slice(1, 22, 38, status);
        if (expect(strstr(status, "CPU0 ") == status, "Reopened monitor did not restore the selected CPU bank.")) return 1;
        if (expect(second_monitor.poll(0) == 1, "Second monitor exit failed for reopen-state test.")) return 1;
        second_monitor.deinit();
    }

    monitor_reset_saved_state();
    return 0;
}

static int test_monitor_kernal_bank_switch_and_ram_interaction(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char line[MONITOR_HEX_ROW_CHARS + 1];
    char status[39];

    monitor_reset_saved_state();

    backend.kernal[0] = 0x4E;
    backend.kernal[1] = 0x22;
    backend.ram[0xE000] = 0xAA;
    backend.ram[0xE001] = 0xBB;

    const int keys[] = {
        'J', 'e', '5', '5', KEY_BREAK,
        'o', 'o', 'o', 'o', 'o', 'o',
        'e', '6', '6', KEY_BREAK,
        'o', 'o',
        KEY_BREAK
    };
    FakeKeyboard keyboard(keys, 18);

    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.set_prompt("E000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto to E000 failed for KERNAL banking test.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "e000 4e 22") == line || strstr(line, "E000 4E 22") == line,
               "Initial KERNAL-visible bytes at E000 are incorrect.")) return 1;

    if (expect(monitor.poll(0) == 0, "Edit mode entry failed for KERNAL-visible RAM write test.")) return 1;
    if (expect(monitor.poll(0) == 0, "First KERNAL-visible nibble failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Second KERNAL-visible nibble failed.")) return 1;
    if (expect(backend.ram[0xE000] == 0x55, "KERNAL-visible write did not update underlying RAM at E000.")) return 1;
    if (expect(backend.kernal[0] == 0x4E, "KERNAL-visible write incorrectly modified KERNAL ROM content.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "e000 4e 22") == line || strstr(line, "E000 4E 22") == line,
               "KERNAL-visible monitor view should still show ROM after write-under-ROM.")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave edit mode after KERNAL-visible write.")) return 1;

    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU0 failed for KERNAL banking test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU1 failed for KERNAL banking test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU2 failed for KERNAL banking test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU3 failed for KERNAL banking test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU4 failed for KERNAL banking test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU5 failed for KERNAL banking test.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU5 $A:RAM $D:I/O $E:RAM VIC2 $8000") == status,
               "CPU5 status did not expose RAM at E000.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "e000 55 bb") == line || strstr(line, "E000 55 BB") == line,
               "Bank switching to RAM did not reveal the underlying bytes at E000.")) return 1;

    if (expect(monitor.poll(0) == 0, "Edit mode entry failed for RAM-visible write test.")) return 1;
    if (expect(monitor.poll(0) == 0, "First RAM-visible nibble failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Second RAM-visible nibble failed.")) return 1;
    if (expect(backend.ram[0xE001] == 0x66, "RAM-visible write did not update underlying RAM at E001.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "e000 55 66") == line || strstr(line, "E000 55 66") == line,
               "RAM-visible monitor view did not redraw the edited underlying RAM byte.")) return 1;
    if (expect(monitor.poll(0) == 0, "RUN/STOP should leave edit mode after RAM-visible write.")) return 1;

    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU6 failed for KERNAL restore test.")) return 1;
    if (expect(monitor.poll(0) == 0, "CPU bank cycle to CPU7 failed for KERNAL restore test.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU7 $A:BAS $D:I/O $E:KRN VIC2 $8000") == status,
               "CPU7 status did not restore KERNAL visibility.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, line);
    if (expect(strstr(line, "e000 4e 22") == line || strstr(line, "E000 4E 22") == line,
               "Switching back to KERNAL did not restore the ROM-visible bytes at E000.")) return 1;
    if (expect(monitor.poll(0) == 1, "RUN/STOP exit failed after KERNAL banking interaction test.")) return 1;
    monitor.deinit();

    return 0;
}

#include "assembler_6502.h"

static int test_assembler_encoding(void)
{
    AsmInsn insn;
    MonitorError err = MONITOR_OK;

    if (expect(monitor_assemble_line("LDA #$12", false, 0xC000, &insn, &err), "LDA #$12 must encode")) return 1;
    if (expect(insn.length == 2 && insn.bytes[0] == 0xA9 && insn.bytes[1] == 0x12, "LDA #$12 -> A9 12")) return 1;

    if (expect(monitor_assemble_line("JMP $C000", false, 0xC000, &insn, &err), "JMP $C000 must encode")) return 1;
    if (expect(insn.length == 3 && insn.bytes[0] == 0x4C && insn.bytes[1] == 0x00 && insn.bytes[2] == 0xC0, "JMP $C000 -> 4C 00 C0")) return 1;

    if (expect(monitor_assemble_line("BNE $C005", false, 0xC000, &insn, &err), "BNE $C005 must encode")) return 1;
    if (expect(insn.length == 2 && insn.bytes[0] == 0xD0 && insn.bytes[1] == 0x03, "BNE $C005 from $C000 -> D0 03")) return 1;

    if (expect(monitor_assemble_line("NOP", false, 0xC000, &insn, &err), "NOP must encode")) return 1;
    if (expect(insn.length == 1 && insn.bytes[0] == 0xEA, "NOP -> EA")) return 1;

    if (expect(monitor_assemble_line("STA $0400,X", false, 0xC000, &insn, &err), "STA $0400,X must encode")) return 1;
    if (expect(insn.length == 3 && insn.bytes[0] == 0x9D && insn.bytes[1] == 0x00 && insn.bytes[2] == 0x04, "STA $0400,X -> 9D 00 04")) return 1;

    // Illegal opcode rejected when toggle off.
    if (expect(!monitor_assemble_line("SLO $12", false, 0xC000, &insn, &err), "SLO must be rejected when illegal=OFF")) return 1;
    // ...accepted when on.
    if (expect(monitor_assemble_line("SLO $12", true, 0xC000, &insn, &err), "SLO $12 must encode when illegal=ON")) return 1;
    if (expect(insn.length == 2 && insn.bytes[0] == 0x07 && insn.bytes[1] == 0x12, "SLO $12 -> 07 12")) return 1;

    return 0;
}

static int test_screen_code_reverse(void)
{
    if (expect(monitor_screen_code_for_char('A') == 0x01, "screen-code 'A' must be 0x01")) return 1;
    if (expect(monitor_screen_code_for_char('Z') == 0x1A, "screen-code 'Z' must be 0x1A")) return 1;
    if (expect(monitor_screen_code_for_char('@') == 0x00, "screen-code '@' must be 0x00")) return 1;
    if (expect(monitor_screen_code_for_char(' ') == 0x20, "screen-code ' ' must be 0x20")) return 1;
    if (expect(monitor_screen_code_for_char('1') == 0x31, "screen-code '1' must be 0x31")) return 1;
    if (expect(monitor_screen_code_for_char('a') == 0x01, "lowercase 'a' must map to uppercase 'A' screen code")) return 1;
    if (expect(monitor_screen_code_for_char('A', MONITOR_SCREEN_CHARSET_LOWER_UPPER) == 0x41,
               "L/U screen-code 'A' must be 0x41")) return 1;
    if (expect(monitor_screen_code_for_char('Z', MONITOR_SCREEN_CHARSET_LOWER_UPPER) == 0x5A,
               "L/U screen-code 'Z' must be 0x5A")) return 1;
    if (expect(monitor_screen_code_for_char('#', MONITOR_SCREEN_CHARSET_LOWER_UPPER) == 0x23,
               "Screen-code '#' must remain literal in L/U mode")) return 1;
    if (expect(monitor_screen_code_for_char('<', MONITOR_SCREEN_CHARSET_LOWER_UPPER) == 0x3C,
               "Screen-code '<' must remain literal in L/U mode")) return 1;
    if (expect(monitor_screen_code_for_char('[', MONITOR_SCREEN_CHARSET_UPPER_GRAPHICS) == 0xFF,
               "Display aliases must not be accepted as screen-code input.")) return 1;
    return 0;
}

static int test_logical_delete_per_view(void)
{
    // HEX: DEL on a non-zero byte must clear it to 0x00 and step cursor RIGHT.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        backend.write(0x0400, 0xAB);
        backend.write(0x0401, 0xCD);
        const int keys[] = { 'J', 'E', KEY_DELETE, KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys)/sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < (int)(sizeof(keys)/sizeof(keys[0])) - 1; i++) {
            (void)mon.poll(0);
        }
        // First DEL clears 0400 and moves to 0401; second DEL clears 0401.
        if (expect(backend.read(0x0400) == 0x00 && backend.read(0x0401) == 0x00,
                   "HEX DEL must clear current byte to 0x00 and advance cursor RIGHT")) return 1;
    }

    // ASC: DEL on byte must set to 0x20 and step cursor LEFT.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        backend.write(0x03FF, 0xAA);
        backend.write(0x0400, 0xBB);
        const int keys[] = { 'J', 'I', 'E', KEY_DELETE, KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys)/sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < (int)(sizeof(keys)/sizeof(keys[0])) - 1; i++) {
            (void)mon.poll(0);
        }
        // First DEL clears 0400 and moves to 03FF; second DEL clears 03FF.
        if (expect(backend.read(0x0400) == 0x20 && backend.read(0x03FF) == 0x20,
                   "ASC DEL must clear byte to 0x20 and advance cursor LEFT")) return 1;
    }

    // SCR: DEL on byte must set to screen-code space (0x20) and step cursor LEFT.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        backend.write(0x03FF, 0x11);
        backend.write(0x0400, 0x55);
        const int keys[] = { 'J', 'V', 'E', KEY_DELETE, KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys)/sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < (int)(sizeof(keys)/sizeof(keys[0])) - 1; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0x0400) == 0x20 && backend.read(0x03FF) == 0x20,
                   "SCR DEL must clear byte to screen-code space and advance cursor LEFT")) return 1;
    }

    // ASM: DEL on $C000 = A9 12 (LDA #$12, 2 bytes) must fill EA EA.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        backend.write(0xC000, 0xA9);
        backend.write(0xC001, 0x12);
        const int keys[] = { 'J', 'A', 'E', KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys)/sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C000", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < (int)(sizeof(keys)/sizeof(keys[0])) - 1; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0xC000) == 0xEA && backend.read(0xC001) == 0xEA,
                   "ASM DEL must replace LDA #$12 (2 bytes) with EA EA")) return 1;
    }

    // DEL must also work in non-edit mode for HEX/ASC/SCR/ASM.
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        backend.write(0x0400, 0xAB);
        const int keys[] = { 'J', KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys)/sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < (int)(sizeof(keys)/sizeof(keys[0])) - 1; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0x0400) == 0x00,
                   "HEX DEL must work outside edit mode too")) return 1;
    }
    return 0;
}

static int test_scr_edit_writes_screen_code(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        monitor_reset_saved_state();
        backend.write(0x0400, 0x00);
        const int keys[] = { 'J', 'V', 'E', 'A', KEY_BREAK };
        FakeKeyboard kb(keys, 5);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 5; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0x0400) == 0x01, "SCR edit: typing 'A' must write screen code 0x01 in U/G")) return 1;
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        monitor_reset_saved_state();
        backend.write(0x0400, 0x00);
        const int keys[] = { 'J', 'V', 'U', 'E', 'A', KEY_BREAK };
        FakeKeyboard kb(keys, 6);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 6; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0x0400) == 0x41, "SCR edit: typing 'A' must write screen code 0x41 in L/U")) return 1;
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        monitor_reset_saved_state();
        backend.write(0x0400, 0x00);
        backend.write(0x0401, 0x00);
        backend.write(0x0402, 0x00);
        backend.write(0x0403, 0x00);
        const int keys[] = { 'J', 'V', 'E', 'a', 'A', '#', '<', KEY_BREAK };
        FakeKeyboard kb(keys, 8);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 8; i++) {
            (void)mon.poll(0);
        }
        if (expect(backend.read(0x0400) == 0x01, "SCR edit: typing 'a' must write screen code 0x01")) return 1;
        if (expect(backend.read(0x0401) == 0x01, "SCR edit: typing 'A' must write screen code 0x01 in U/G")) return 1;
        if (expect(backend.read(0x0402) == 0x23, "SCR edit: typing '#' must write literal screen code 0x23")) return 1;
        if (expect(backend.read(0x0403) == 0x3C, "SCR edit: typing '<' must write literal screen code 0x3C")) return 1;
    }

    return 0;
}

static int test_screen_charset_toggle_and_header(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'V', 'U', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Screen charset header test: Screen view switch failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR SCR U/G $0000") == header,
                   "Screen view header must show U/G by default.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen charset header test: toggle failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR SCR L/U $0000") == header,
                   "Screen view header must show L/U after toggle.")) return 1;
        if (expect(mon.poll(0) == 1, "Screen charset header test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'A', 'U', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0x0000, 0x07);
        backend.write(0x0001, 0x44);
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM U test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM U test: undocumented toggle failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "Undoc") != NULL,
                   "ASM U must keep toggling undocumented opcodes.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM U test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'M', 'U', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Memory U test: Memory view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Memory U test: popup branch failed.")) return 1;
        if (expect(ui.popup_count == 1 && strcmp(ui.last_popup, "UNDOC IN ASM, CASE IN SCR") == 0,
                   "U outside ASM/SCR must show the refined invalid-context popup.")) return 1;
        if (expect(mon.poll(0) == 1, "Memory U test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_screen_charset_display_and_memory_ascii_pane(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[MONITOR_TEXT_ROW_CHARS + 1];
    char row[MONITOR_HEX_ROW_CHARS + 1];
    const int keys[] = { 'V', 'U', 'M', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    backend.write(0x0000, 0x01);
    backend.write(0x0001, 0x1A);
    backend.write(0x0002, 0x41);
    backend.write(0x0003, 0x5A);
    backend.write(0x0004, 0x20);
    backend.write(0x0005, 0x61);
    backend.write(0x0006, 0x80);

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);

    if (expect(mon.poll(0) == 0, "Screen display test: Screen view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Screen display test: charset toggle failed.")) return 1;
    screen.get_slice(1, 4, MONITOR_TEXT_ROW_CHARS, line);
    if (expect(strncmp(line, "0000 azAZ .@", 12) == 0,
               "L/U Screen view must show lowercase for $01-$1A, uppercase for $41-$5A, visible placeholders for non-space unmapped codes, and base-glyph rendering for high-bit values.")) return 1;

    if (expect(mon.poll(0) == 0, "Screen display test: Memory view switch failed.")) return 1;
    screen.get_slice(1, 4, MONITOR_HEX_ROW_CHARS, row);
    if (expect(strcmp(row, "0000 01 1A 41 5A 20 61 80 00 ..AZ a..") == 0,
               "Memory view ASCII pane must stay literal ASCII and ignore Screen charset mode.")) return 1;
    if (expect(mon.poll(0) == 1, "Screen display test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_number_shortcut_routing(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[19];
        const int keys[] = { 'A', 'E', 'N', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM N-routing test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM N-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM N-routing test: typing N failed.")) return 1;
        if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "ASM edit N must not open the framed Number popup.")) return 1;
        screen.get_slice(16, 4, 18, line);
        if (expect(strstr(line, " N_") == line,
                   "ASM edit N must be routed into mnemonic entry instead of Number.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM N-routing test: RUN/STOP should close the opcode picker first.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM N-routing test: RUN/STOP should leave edit mode next.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM N-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'E', KEY_RIGHT, 'N', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0xC000, 0xA9);
        backend.write(0xC001, 0x12);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C000", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM operand N test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: moving to operand failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C001 BYTE") == line,
                   "ASM operand context must open Number on the operand byte without leaving edit mode.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM operand N test: exit edit mode failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM operand N test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[19];
        const int keys[] = { 'J', 'A', 'E', 'L', 'N', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C100", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: first mnemonic letter failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: second mnemonic letter failed.")) return 1;
        if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "Opcode completion popup must keep N as mnemonic input instead of opening Number.")) return 1;
        screen.get_slice(16, 4, 18, line);
        if (expect(strstr(line, " LN_") == line,
                   "While the opcode picker is active, N must continue editing the mnemonic prefix.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: RUN/STOP should close the picker first.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM popup N-routing test: RUN/STOP should leave edit mode next.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM popup N-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'E', 'L', 'D', 'A', KEY_RETURN, 'N', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C200", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 7; i++) {
            if (expect(mon.poll(0) == 0, "ASM post-opcode N test: mnemonic commit failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "ASM post-opcode N test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C201 BYTE") == line,
                   "After opcode entry advances to operand context, N must open Number without leaving edit mode.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM post-opcode N test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM post-opcode N test: exit edit mode failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM post-opcode N test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'I', 'E', 'N', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASCII N-routing test: ASCII view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII N-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII N-routing test: typing N failed.")) return 1;
        if (expect(backend.read(0x0000) == 'N',
                   "ASCII edit N must write the typed character instead of opening Number.")) return 1;
        if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "ASCII edit N must not open the framed Number popup.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII N-routing test: RUN/STOP should leave edit mode first.")) return 1;
        if (expect(mon.poll(0) == 1, "ASCII N-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'V', 'E', 'N', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Screen N-routing test: Screen view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen N-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen N-routing test: typing N failed.")) return 1;
        if (expect(backend.read(0x0000) == monitor_screen_code_for_char('N'),
                   "Screen edit N must write the typed screen code instead of opening Number.")) return 1;
        if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "Screen edit N must not open the framed Number popup.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen N-routing test: RUN/STOP should leave edit mode first.")) return 1;
        if (expect(mon.poll(0) == 1, "Screen N-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'A', 'N', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM non-edit N test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM non-edit N test: Number popup open failed.")) return 1;
        if (expect(find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "ASM non-edit N must still open the framed Number popup.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM non-edit N test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM non-edit N test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'E', 'N', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "HEX edit N test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "HEX edit N test: Number popup open failed.")) return 1;
        if (expect(find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "HEX edit N must keep opening the framed Number popup.")) return 1;
        if (expect(mon.poll(0) == 0, "HEX edit N test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 0, "HEX edit N test: RUN/STOP should leave edit mode first.")) return 1;
        if (expect(mon.poll(0) == 1, "HEX edit N test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_range_shortcut_routing(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'I', 'E', 'R', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASCII R-routing test: ASCII view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII R-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII R-routing test: typing R failed.")) return 1;
        if (expect(backend.read(0x0000) == 'R',
                   "ASCII edit R must write the typed character instead of toggling Range.")) return 1;
        screen.get_slice(1, 22, 38, header);
        if (expect(strstr(header, "Range") == NULL,
                   "ASCII edit R must not enable range mode.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII R-routing test: RUN/STOP should leave edit mode first.")) return 1;
        if (expect(mon.poll(0) == 1, "ASCII R-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'V', 'E', 'R', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Screen R-routing test: Screen view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen R-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen R-routing test: typing R failed.")) return 1;
        if (expect(backend.read(0x0000) == monitor_screen_code_for_char('R'),
                   "Screen edit R must write the typed screen code instead of toggling Range.")) return 1;
        screen.get_slice(1, 22, 38, header);
        if (expect(strstr(header, "Range") == NULL,
                   "Screen edit R must not enable range mode.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen R-routing test: RUN/STOP should leave edit mode first.")) return 1;
        if (expect(mon.poll(0) == 1, "Screen R-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[19];
        char header[39];
        const int keys[] = { 'A', 'E', 'R', KEY_BREAK, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM R-routing test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM R-routing test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM R-routing test: typing R failed.")) return 1;
        if (expect(!find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "ASM edit R must not open a framed popup.")) return 1;
        screen.get_slice(16, 4, 18, line);
        if (expect(strstr(line, " R_") == line,
                   "ASM edit R must be routed into mnemonic entry instead of Range.")) return 1;
        screen.get_slice(1, 22, 38, header);
        if (expect(strstr(header, "Range") == NULL,
                   "ASM mnemonic edit R must not enable range mode.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM R-routing test: RUN/STOP should close the picker first.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM R-routing test: RUN/STOP should leave edit mode next.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM R-routing test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_asm_number_popup_targets_operands(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'N', '1', '2', '3', '4', KEY_RETURN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0x9010, 0xB9);
        backend.write(0x9011, 0x99);
        backend.write(0x9012, 0xFF);
        backend.write(0x9013, 0xEA);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("9010", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM word Number test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM word Number test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM word Number test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $9011 WORD") == line,
                   "ASM Number popup must target the two-byte operand, not the opcode byte.")) return 1;
        get_popup_line(screen, 1, line, sizeof(line));
        if (expect(strstr(line, "Hex      $FF99") == line,
                   "ASM Number popup must preload the operand word value.")) return 1;
        get_popup_line(screen, 6, line, sizeof(line));
        if (expect(strstr(line, "Write    $9011/$9012 LE") == line,
                   "ASM Number popup must show the operand write-back range.")) return 1;
        get_popup_line(screen, 7, line, sizeof(line));
        if (expect(strstr(line, "Bytes    99 FF") == line,
                   "ASM Number popup must show the operand bytes in little-endian order.")) return 1;
        for (int i = 0; i < 5; i++) {
            if (expect(mon.poll(0) == 0, "ASM word Number test: editing/commit failed.")) return 1;
        }
        if (expect(backend.read(0x9010) == 0xB9,
                   "ASM word Number commit must leave the opcode byte unchanged.")) return 1;
        if (expect(backend.read(0x9011) == 0x34 && backend.read(0x9012) == 0x12,
                   "ASM word Number commit must rewrite only the operand bytes in little-endian order.")) return 1;
        if (expect(backend.read(0x9013) == 0xEA,
                   "ASM word Number commit must not overrun beyond the operand bytes.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM word Number test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM word Number test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'N', '3', '4', KEY_RETURN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0xC000, 0xA9);
        backend.write(0xC001, 0x12);
        backend.write(0xC002, 0xEA);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C000", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM byte Number test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM byte Number test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM byte Number test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C001 BYTE") == line,
                   "ASM Number popup must target the one-byte operand address.")) return 1;
        get_popup_line(screen, 1, line, sizeof(line));
        if (expect(strstr(line, "Hex      $12") == line,
                   "ASM Number popup must preload the one-byte operand value.")) return 1;
        for (int i = 0; i < 3; i++) {
            if (expect(mon.poll(0) == 0, "ASM byte Number test: editing/commit failed.")) return 1;
        }
        if (expect(backend.read(0xC000) == 0xA9,
                   "ASM byte Number commit must leave the opcode byte unchanged.")) return 1;
        if (expect(backend.read(0xC001) == 0x34,
                   "ASM byte Number commit must rewrite the one-byte operand.")) return 1;
        if (expect(backend.read(0xC002) == 0xEA,
                   "ASM byte Number commit must not touch the following byte.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM byte Number test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM byte Number test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'N', '4', '2', KEY_RETURN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0xC100, 0x60);
        backend.write(0xC101, 0xEA);
        backend.write(0xC102, 0x99);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C100", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM no-operand Number test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM no-operand Number test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM no-operand Number test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C101 BYTE") == line,
                   "No-operand ASM Number popup must fall back to the byte after the instruction.")) return 1;
        for (int i = 0; i < 3; i++) {
            if (expect(mon.poll(0) == 0, "ASM no-operand Number test: editing/commit failed.")) return 1;
        }
        if (expect(backend.read(0xC100) == 0x60,
                   "No-operand ASM Number popup must not overwrite the opcode.")) return 1;
        if (expect(backend.read(0xC101) == 0x42,
                   "No-operand ASM Number popup must write to the deterministic fallback address.")) return 1;
        if (expect(backend.read(0xC102) == 0x99,
                   "No-operand ASM Number popup must not overrun past the fallback byte.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM no-operand Number test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM no-operand Number test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_asm_number_popup_illegal_and_invalid_rows(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'N', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0xC300, 0x07);
        backend.write(0xC301, 0x44);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C300", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM invalid-row Number test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM invalid-row Number test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM invalid-row Number test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C300 BYTE") == line,
                   "Disabled undocumented opcodes must fall back to the safe current-byte Number target.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM invalid-row Number test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM invalid-row Number test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'J', 'A', 'U', 'N', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        backend.write(0xC320, 0x07);
        backend.write(0xC321, 0x44);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C320", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM undocumented Number test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM undocumented Number test: ASM view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM undocumented Number test: undocumented toggle failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM undocumented Number test: Number popup open failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $C321 BYTE") == line,
                   "Enabled undocumented opcodes must still target the decoded operand byte.")) return 1;
        get_popup_line(screen, 1, line, sizeof(line));
        if (expect(strstr(line, "Hex      $44") == line,
                   "Enabled undocumented-opcode Number popup must preload the decoded operand value.")) return 1;
        if (expect(mon.poll(0) == 0, "ASM undocumented Number test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASM undocumented Number test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_asm_edit_assemble_at_cursor(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    // ASM edit no longer goes through string_box. Pressing a letter on the
    // mnemonic part opens the in-monitor opcode picker; further letters
    // filter, UP/DOWN navigate, ENTER selects. After selection the cursor
    // moves to the operand byte and hex digits write it directly.
    //
    //   J / "C000" / enter           -- jump to $C000
    //   A                            -- switch to ASM
    //   E                            -- enter edit mode
    //   L D A                        -- open picker, narrow to LDA variants
    //   ENTER                        -- deterministic ordering now places the
    //                                   simplest LDA form first
    //   4 2                          -- type the operand byte at $C001
    const int keys[] = {
        'J', 'A', 'E', 'L', 'D', 'A', KEY_RETURN, '4', '2', KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xA9 && backend.read(0xC001) == 0x42,
               "ASM edit must write A9 42 for LDA # via opcode picker")) return 1;
    return 0;
}

static int test_asm_edit_direct_typing(void)
{
    // Power-user direct-typing path: type the full instruction without
    // navigating the picker. "LDA $1000" -> AD 00 10 (absolute).
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    //   J / "C000" / enter           -- jump to $C000
    //   A                            -- switch to ASM
    //   E                            -- enter edit mode
    //   L D A SPACE 1 0 0 0 ENTER    -- type the whole instruction
    const int keys[] = {
        'J', 'A', 'E', 'L', 'D', 'A', KEY_SPACE, '1', '0', '0', '0', KEY_RETURN, KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xAD && backend.read(0xC001) == 0x00 && backend.read(0xC002) == 0x10,
               "ASM direct typing 'LDA 1000' must encode AD 00 10")) return 1;
    return 0;
}

static int test_space_edit_behavior_preserved(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        CaptureScreen snapshot;
        FakeMemoryBackend backend;
        const int keys[] = { 'E', KEY_SPACE };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        backend.write(0x0000, 0xAB);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "HEX edit Space test: edit-mode entry failed.")) return 1;
        snapshot = screen;
        if (expect(mon.poll(0) == 0, "HEX edit Space test: space handling failed.")) return 1;
        if (expect_screens_equal(snapshot, screen,
                                 "HEX edit Space must remain a non-paging no-op.")) return 1;
        if (expect(backend.read(0x0000) == 0xAB,
                   "HEX edit Space must not modify the current byte.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'I', 'E', KEY_SPACE };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        backend.write(0x0000, 'A');
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASCII edit Space test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII edit Space test: edit-mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII edit Space test: typing space failed.")) return 1;
        if (expect(backend.read(0x0000) == ' ',
                   "ASCII edit Space must keep writing a literal space.")) return 1;
        if (expect(screen.reverse_chars[4][7],
                   "ASCII edit Space must advance to the next character instead of paging.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'V', 'E', KEY_SPACE };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        backend.write(0x0000, 0x01);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Screen edit Space test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen edit Space test: edit-mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen edit Space test: typing space failed.")) return 1;
        if (expect(backend.read(0x0000) == monitor_screen_code_for_char(' ', MONITOR_SCREEN_CHARSET_UPPER_GRAPHICS),
                   "Screen edit Space must keep writing a screen-code space.")) return 1;
        if (expect(screen.reverse_chars[4][7],
                   "Screen edit Space must advance to the next character instead of paging.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'J', 'B', 'E', KEY_SPACE };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        backend.write(0x0400, 0xAA);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary edit Space test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary edit Space test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary edit Space test: edit-mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary edit Space test: typing space failed.")) return 1;
        if (expect(backend.read(0x0400) == 0x2A,
                   "Binary edit Space must keep clearing the selected bit.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0400/6") == header,
                   "Binary edit Space must advance exactly one bit instead of paging.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_shift_space_does_not_page_in_edit_mode(void)
{
    static const struct {
        int view_key;
        const char *message;
    } cases[] = {
        { 0,   "Shift+Space must not page up in Memory edit mode." },
        { 'I', "Shift+Space must not page up in ASCII edit mode." },
        { 'V', "Shift+Space must not page up in Screen edit mode." },
        { 'B', "Shift+Space must not page up in Binary edit mode." },
        { 'A', "Shift+Space must not page up in Assembly edit mode." },
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TestUserInterface ui;
        CaptureScreen screen;
        CaptureScreen snapshot;
        FakeMemoryBackend backend;
        int keys[4];
        int key_count = 0;

        for (uint32_t addr = 0; addr < 0x10000UL; addr++) {
            backend.write((uint16_t)addr, (uint8_t)(addr & 0xFF));
        }

        keys[key_count++] = 'J';
        if (cases[i].view_key) {
            keys[key_count++] = cases[i].view_key;
        }
        keys[key_count++] = 'E';
        keys[key_count++] = KEY_SHIFT_SP;
        FakeKeyboard kb(keys, key_count);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("4000", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int step = 0; step < key_count - 1; step++) {
            if (expect(mon.poll(0) == 0, "Shift+Space edit test setup failed.")) return 1;
        }
        snapshot = screen;
        if (expect(mon.poll(0) == 0, "Shift+Space edit test key handling failed.")) return 1;
        if (expect_screens_equal(snapshot, screen, cases[i].message)) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_asm_edit_direct_typing_immediate(void)
{
    // Direct typing with immediate addressing: "LDA #FF" -> A9 FF.
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    const int keys[] = {
        'J', 'A', 'E', 'L', 'D', 'A', '#', 'F', 'F', KEY_RETURN, KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xA9 && backend.read(0xC001) == 0xFF,
               "ASM direct typing 'LDA #FF' must encode A9 FF")) return 1;
    return 0;
}

static int test_asm_edit_branch_two_parts(void)
{
    // Branch instructions encode opcode + 1-byte signed offset but render
    // opcode + 4-hex absolute target. The asm-edit cursor must expose both
    // halves of the displayed target as separately editable parts and
    // translate the typed value back into a relative offset.
    //
    //   Memory at $C000: F0 10  ->  BEQ $C012
    //   J / "C000" / enter           -- jump to $C000
    //   A                            -- switch to ASM
    //   E                            -- enter edit mode
    //   RIGHT                        -- move cursor to high byte ($C0)
    //   C 0                          -- type $C0 (no change to high byte)
    //   RIGHT(implicit)              -- after 2nd nibble, advances to low
    //   2 0                          -- type $20  -> target $C020
    //   BREAK
    // Expected: rel offset = $C020 - ($C000 + 2) = $1E -> memory F0 1E.
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    backend.write(0xC000, 0xF0);
    backend.write(0xC001, 0x10);
    const int keys[] = {
        'J', 'A', 'E', KEY_RIGHT, 'C', '0', '2', '0', KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xF0 && backend.read(0xC001) == 0x1E,
               "ASM branch edit must rewrite rel offset to land on typed target")) return 1;
    return 0;
}

static int test_asm_cpu_bank_cycle_preserves_screen_row(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char header[39];
    char status[39];
    int row_before = -1;
    int row_after = -1;
    const int keys[] = { 'J', 'A', KEY_DOWN, KEY_DOWN, 'o', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    backend.basic[0] = 0x20;
    backend.basic[1] = 0x34;
    backend.basic[2] = 0x12;
    backend.basic[3] = 0x20;
    backend.basic[4] = 0x78;
    backend.basic[5] = 0x56;
    backend.basic[6] = 0xEA;
    backend.basic[7] = 0xEA;
    for (uint16_t addr = 0xA000; addr < 0xA010; addr++) {
        backend.ram[addr] = 0xEA;
    }

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("A000", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "ASM bank-anchor test: goto failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM bank-anchor test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM bank-anchor test: first down failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM bank-anchor test: second down failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_before),
               "ASM bank-anchor test: highlighted row not found before CPU bank cycle.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $A006") == header,
               "ASM bank-anchor test must land on the third visible instruction before cycling banks.")) return 1;

    if (expect(mon.poll(0) == 0, "ASM bank-anchor test: CPU bank cycle failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_after),
               "ASM bank-anchor test: highlighted row not found after CPU bank cycle.")) return 1;
    if (expect(row_after == row_before,
               "CPU bank cycling in ASM view must preserve the visible cursor row.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strstr(header, "MONITOR ASM $A006") == header,
               "CPU bank cycling in ASM view must preserve the logical cursor address when possible.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU0 ") == status,
               "CPU bank cycling test must actually advance the visible CPU bank status.")) return 1;
    if (expect(mon.poll(0) == 1, "ASM bank-anchor test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_asm_page_up_keeps_screen_row(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int keys[16];
    int key_count = 0;
    int row_before = -1;
    int row_after = -1;
    uint16_t addr = 0xC000;

    for (int i = 0; i < 96; i++) {
        if ((i % 3) == 0) {
            backend.write(addr++, 0x20);
            backend.write(addr++, 0x00);
            backend.write(addr++, 0x10);
        } else if ((i % 3) == 1) {
            backend.write(addr++, 0xA9);
            backend.write(addr++, 0x01);
        } else {
            backend.write(addr++, 0xEA);
        }
    }

    keys[key_count++] = 'J';
    keys[key_count++] = 'A';
    for (int i = 0; i < 5; i++) keys[key_count++] = KEY_DOWN;
    keys[key_count++] = KEY_PAGEDOWN;
    keys[key_count++] = KEY_PAGEUP;
    keys[key_count++] = KEY_BREAK;
    FakeKeyboard kb(keys, key_count);

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "ASM page-up test: goto failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM page-up test: ASM view switch failed.")) return 1;
    for (int i = 0; i < 5; i++) {
        if (expect(mon.poll(0) == 0, "ASM page-up test: initial down navigation failed.")) return 1;
    }
    if (expect(mon.poll(0) == 0, "ASM page-up test: page-down failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_before),
               "ASM page-up test: highlighted row not found before page-up.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM page-up test: page-up failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_after),
               "ASM page-up test: highlighted row not found after page-up.")) return 1;
    if (expect(row_after == row_before,
               "ASM page-up must preserve the visible cursor row like page-down.")) return 1;
    if (expect(mon.poll(0) == 1, "ASM page-up test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_space_shortcuts_match_existing_paging(void)
{
    static const struct {
        int view_key;
        const char *page_down_message;
        const char *page_up_message;
    } cases[] = {
        { 0,   "Space must match F7 page-down behavior in Memory view.",
               "Shift+Space must match F1 page-up behavior in Memory view." },
        { 'I', "Space must match F7 page-down behavior in ASCII view.",
               "Shift+Space must match F1 page-up behavior in ASCII view." },
        { 'V', "Space must match F7 page-down behavior in Screen view.",
               "Shift+Space must match F1 page-up behavior in Screen view." },
        { 'B', "Space must match F7 page-down behavior in Binary view.",
               "Shift+Space must match F1 page-up behavior in Binary view." },
        { 'A', "Space must match F7 page-down behavior in Assembly view.",
               "Shift+Space must match F1 page-up behavior in Assembly view." },
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (int direction = 0; direction < 2; direction++) {
            TestUserInterface ui_a;
            TestUserInterface ui_b;
            CaptureScreen screen_a;
            CaptureScreen screen_b;
            FakeMemoryBackend backend_a;
            FakeMemoryBackend backend_b;
            int keys_a[4];
            int keys_b[4];
            int key_count = 0;
            const int page_key = direction ? KEY_F1 : KEY_F7;
            const int shortcut_key = direction ? KEY_SHIFT_SP : KEY_SPACE;
            const char *message = direction ? cases[i].page_up_message : cases[i].page_down_message;

            for (uint32_t addr = 0; addr < 0x10000UL; addr++) {
                uint8_t value = (uint8_t)((addr >> 8) ^ addr);
                backend_a.write((uint16_t)addr, value);
                backend_b.write((uint16_t)addr, value);
            }

            keys_a[key_count] = 'J';
            keys_b[key_count++] = 'J';
            if (cases[i].view_key) {
                keys_a[key_count] = cases[i].view_key;
                keys_b[key_count++] = cases[i].view_key;
            }
            keys_a[key_count] = page_key;
            keys_b[key_count++] = shortcut_key;

            FakeKeyboard kb_a(keys_a, key_count);
            FakeKeyboard kb_b(keys_b, key_count);
            ui_a.screen = &screen_a;
            ui_a.keyboard = &kb_a;
            ui_a.set_prompt("4000", 1);
            ui_b.screen = &screen_b;
            ui_b.keyboard = &kb_b;
            ui_b.set_prompt("4000", 1);

            monitor_reset_saved_state();
            BackendMachineMonitor mon_a(&ui_a, &backend_a);
            mon_a.init(&screen_a, &kb_a);
            for (int step = 0; step < key_count; step++) {
                if (expect(mon_a.poll(0) == 0, "Paging parity test setup A failed.")) return 1;
            }

            monitor_reset_saved_state();
            BackendMachineMonitor mon_b(&ui_b, &backend_b);
            mon_b.init(&screen_b, &kb_b);
            for (int step = 0; step < key_count; step++) {
                if (expect(mon_b.poll(0) == 0, "Paging parity test setup B failed.")) return 1;
            }

            if (expect_screens_equal(screen_a, screen_b, message)) return 1;
            mon_a.deinit();
            mon_b.deinit();
        }
    }

    return 0;
}

static int test_opcode_picker_refilters_live(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[39];
    const int keys[] = { 'A', 'E', 'J', 'S', KEY_DELETE, KEY_BREAK, KEY_BREAK, KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: edit mode entry failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: first letter failed.")) return 1;
    screen.get_slice(1, 6, 38, line);
    if (expect(strstr(line, "JMP") != NULL || strstr(line, "JSR") != NULL || strstr(line, "JAM") != NULL,
               "Opcode picker refilter test must start with multiple visible suggestions for prefix J.")) return 1;

    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: second letter failed.")) return 1;
    screen.get_slice(1, 5, 38, line);
    if (expect(strstr(line, "JSR") != NULL,
               "Opcode picker must immediately shrink to the matching mnemonic list as the prefix narrows.")) return 1;
    screen.get_slice(1, 6, 38, line);
    if (expect(strstr(line, "JSR") == NULL && strstr(line, "JMP") == NULL && strstr(line, "JAM") == NULL,
               "Opcode picker must clear stale suggestion rows when the candidate list shrinks.")) return 1;

    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: delete failed.")) return 1;
    screen.get_slice(1, 6, 38, line);
    if (expect(strstr(line, "JSR") != NULL || strstr(line, "JMP") != NULL || strstr(line, "JAM") != NULL,
               "Opcode picker must expand the suggestion list again when the prefix is deleted.")) return 1;

    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: RUN/STOP should close the picker first.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker refilter test: RUN/STOP should leave edit mode next.")) return 1;
    if (expect(mon.poll(0) == 1, "Opcode picker refilter test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_opcode_picker_filters_orders_and_commits_on_enter(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char first_row[39];
        char second_row[39];
        const int keys[] = { 'J', 'A', 'E', 'N', 'O', 'P', KEY_RETURN };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C000", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 6; i++) {
            if (expect(mon.poll(0) == 0, "Opcode picker NOP test: command sequence failed before ENTER.")) return 1;
        }
        screen.get_slice(1, 5, 38, first_row);
        screen.get_slice(1, 6, 38, second_row);
        if (expect(strstr(first_row, "NOP") != NULL,
                   "Documented-only NOP completion must offer the documented NOP variant.")) return 1;
        if (expect(strstr(second_row, "NOP") == NULL,
                   "Documented-only NOP completion must not show undocumented NOP variants.")) return 1;
        if (expect(backend.read(0xC000) == 0x00,
                   "Typing a mnemonic in the opcode picker must not write memory before ENTER.")) return 1;
        if (expect(mon.poll(0) == 0, "Opcode picker NOP test: ENTER commit failed.")) return 1;
        if (expect(backend.read(0xC000) == 0xEA,
                   "ENTER on documented-only NOP completion must encode the documented $EA NOP.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char first_row[39];
        char second_row[39];
        const int keys[] = { 'J', 'A', 'U', 'E', 'N', 'O', 'P', KEY_RETURN };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C100", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 7; i++) {
            if (expect(mon.poll(0) == 0, "Opcode picker illegal NOP test: command sequence failed before ENTER.")) return 1;
        }
        screen.get_slice(1, 5, 38, first_row);
        screen.get_slice(1, 6, 38, second_row);
        if (expect(strstr(first_row, "NOP") != NULL && strstr(first_row, "$") == NULL && strstr(first_row, "#") == NULL,
                   "Undocumented-enabled NOP completion must place the no-operand NOP first.")) return 1;
        if (expect(strstr(second_row, "NOP") != NULL,
                   "Undocumented-enabled NOP completion must still expose additional NOP variants.")) return 1;
        if (expect(backend.read(0xC100) == 0x00,
                   "Undocumented-enabled picker typing must still wait for ENTER before writing memory.")) return 1;
        if (expect(mon.poll(0) == 0, "Opcode picker illegal NOP test: ENTER commit failed.")) return 1;
        if (expect(backend.read(0xC100) == 0xEA,
                   "ENTER must default to the first no-operand NOP variant when undocumented opcodes are enabled.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char first_row[39];
        const int keys[] = { 'J', 'A', 'E', 'L', 'S', 'R', KEY_RETURN };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C200", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 6; i++) {
            if (expect(mon.poll(0) == 0, "Opcode picker LSR test: command sequence failed before ENTER.")) return 1;
        }
        screen.get_slice(1, 5, 38, first_row);
        if (expect(strstr(first_row, "LSR A") != NULL,
                   "Mnemonic variants with an addressing-free form must list that simplest variant first.")) return 1;
        if (expect(backend.read(0xC200) == 0x00,
                   "Typing LSR in the picker must not write memory before ENTER.")) return 1;
        if (expect(mon.poll(0) == 0, "Opcode picker LSR test: ENTER commit failed.")) return 1;
        if (expect(backend.read(0xC200) == 0x4A,
                   "ENTER must commit the first accumulator/implied variant when it is the simplest form.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_opcode_picker_browsing_does_not_mutate_frozen_charset_backup(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    const int keys[] = { 'J', 'A', 'E', 'C', 'L', 'D', KEY_DELETE, KEY_BREAK, KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    const uint8_t sentinel[] = { 0x54, 0x18, 0xEA, 0x18, 0x18, 0x58, 0x00, 0x00 };

    backend.set_frozen(true);
    for (int i = 0; i < (int)sizeof(sentinel); i++) {
        backend.ram_backup[0x10 + i] = sentinel[i];
    }

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("0810", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (expect(mon.poll(0) == 0 || i == (int)(sizeof(keys) / sizeof(keys[0])) - 1,
                   "Freeze charset safety test: command sequence failed.")) return 1;
    }
    for (int i = 0; i < (int)sizeof(sentinel); i++) {
        if (expect(backend.ram_backup[0x10 + i] == sentinel[i],
                   "Browsing and filtering opcode suggestions in freeze mode must not mutate the active charset backup bytes.")) return 1;
    }
    mon.deinit();
    return 0;
}

static int test_opcode_picker_near_bottom_refresh_stays_inside_content(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int keys[32];
    int n = 0;

    for (uint16_t addr = 0x0801; addr < 0x0820; addr++) {
        backend.write(addr, 0xEA);
    }

    keys[n++] = 'A';
    keys[n++] = 'E';
    for (int i = 0; i < 16; i++) keys[n++] = KEY_DOWN;
    keys[n++] = 'L';
    keys[n++] = 'D';
    keys[n++] = KEY_BREAK;
    keys[n++] = KEY_BREAK;

    FakeKeyboard kb(keys, n);
    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: edit mode entry failed.")) return 1;
    for (int i = 0; i < 16; i++) {
        if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: cursor movement failed.")) return 1;
    }
    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: popup open failed.")) return 1;

    screen.reset_write_counts();
    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: popup refilter failed.")) return 1;
    if (expect(screen.count_writes_outside_rect(1, 4, 38, 21) == 0,
               "Lower-border opcode popup refresh must stay inside the monitor content area.")) return 1;

    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: RUN/STOP should close the picker first.")) return 1;
    if (expect(mon.poll(0) == 0, "Lower-border popup bounds test: RUN/STOP should leave edit mode next.")) return 1;
    mon.deinit();
    return 0;
}

static int test_opcode_picker_selection_near_bottom_preserves_live_charset_page(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeBankedMemoryBackend backend;
    char header[39];
    const char *addr_text = NULL;
    unsigned int current_addr = 0;
    int keys[48];
    int n = 0;

    backend.set_frozen(true);
    for (uint16_t addr = 0x0801; addr < 0x0820; addr++) {
        backend.ram_backup[addr - 0x0800] = 0xEA;
        backend.ram[addr] = 0x40;
    }

    keys[n++] = 'J';
    keys[n++] = 'A';
    keys[n++] = 'E';
    for (int i = 0; i < 15; i++) keys[n++] = KEY_DOWN;
    keys[n++] = 'J';
    keys[n++] = 'S';
    keys[n++] = 'R';
    keys[n++] = KEY_RETURN;
    keys[n++] = KEY_BREAK;

    FakeKeyboard kb(keys, n);
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("0801", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Lower-border freeze write test: goto failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Lower-border freeze write test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Lower-border freeze write test: edit mode entry failed.")) return 1;
    for (int i = 0; i < 15; i++) {
        if (expect(mon.poll(0) == 0, "Lower-border freeze write test: cursor movement failed.")) return 1;
    }
    screen.get_slice(1, 3, 38, header);
    addr_text = strchr(header, '$');
    if (expect(addr_text != NULL && sscanf(addr_text + 1, "%x", &current_addr) == 1,
               "Lower-border freeze write test must expose the current ASM address in the header.")) return 1;
    if (expect(current_addr >= 0x0801 && current_addr < 0x0820,
               "Lower-border freeze write test must stay inside the frozen $0800 page.")) return 1;
    for (int i = 0; i < 3; i++) {
        if (expect(mon.poll(0) == 0, "Lower-border freeze write test: popup interaction failed before commit.")) return 1;
    }
    if (expect(mon.poll(0) == 0, "Lower-border freeze write test: opcode commit failed.")) return 1;

    if (expect(backend.read((uint16_t)current_addr) == 0x20,
               "Lower-border popup selection must still commit the selected opcode to the intended frozen RAM address.")) return 1;
    if (expect(backend.ram_backup[current_addr - 0x0800] == 0x20,
               "Frozen opcode selection must update the saved RAM buffer for the selected address.")) return 1;
    if (expect(backend.ram[current_addr] == 0x40,
               "Frozen opcode selection near the lower border must not overwrite the live $0800 charset page.")) return 1;

    if (expect(mon.poll(0) == 0, "Lower-border freeze write test: exit edit mode failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_opcode_picker_pauses_poll_mode(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    CaptureScreen snapshot;
    FakeMemoryBackend backend;
    char line[19];
    int total_writes = 0;
    const int keys[] = { 'A', 'P', 'E', 'N' };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();
    set_fake_ms_timer(0);

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Opcode picker poll test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker poll test: poll mode enable failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker poll test: edit mode entry failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Opcode picker poll test: picker open failed.")) return 1;
    screen.get_slice(16, 4, 18, line);
    if (expect(strstr(line, " N_") == line,
               "Opcode picker poll test must open the mnemonic overlay before idle polling.")) return 1;

    snapshot = screen;
    screen.reset_write_counts();
    set_fake_ms_timer(20);
    if (expect(mon.poll(0) == 0, "Opcode picker poll test: idle poll failed.")) return 1;
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 40; x++) {
            total_writes += screen.write_counts[y][x];
        }
    }
    if (expect(total_writes == 0,
               "Poll mode must not redraw the opcode picker overlay while it is open.")) return 1;
    if (expect_screens_equal(snapshot, screen,
                             "Idle poll must leave the opcode picker overlay screen unchanged.")) return 1;

    mon.deinit();
    return 0;
}

static int test_cross_view_sync(void)
{
    // Edit in HEX, view memory unchanged; Edit in SCR via 'A' key should be visible
    // through a direct memory read (which represents what every other view renders).
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    const int keys[] = { 'J', 'V', 'E', 'B', KEY_BREAK };
    FakeKeyboard kb(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("0400", 1);
    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < 5; i++) (void)mon.poll(0);
    // Now switch to HEX and verify the same byte reads back.
    if (expect(backend.read(0x0400) == 0x02, "Cross-view: SCR 'B' edit must store 0x02 visible to all views")) return 1;
    return 0;
}

static int test_binary_bit_navigation_and_width(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[38];
        const int keys[] = {
            'J', 'B',
            KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
            KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
            KEY_BREAK
        };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        backend.write(0x0400, 0x80);
        backend.write(0x0401, 0x01);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary bit test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary bit test: view switch failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0400/7") == header,
                   "Binary header must show cursor address and MSB bit index.")) return 1;
        screen.get_slice(1, 4, 13, row);
        if (expect(strcmp(row, "0400 *.......") == 0,
                   "Binary width=1 must render exactly 8 bits per row.")) return 1;
        if (expect(screen.reverse_chars[4][6] && !screen.reverse_chars[4][7],
                   "Binary cursor must select exactly one bit at MSB.")) return 1;

        for (int i = 0; i < 7; i++) {
            if (expect(mon.poll(0) == 0, "Binary bit test: right navigation failed before byte boundary.")) return 1;
        }
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0400/0") == header,
                   "Binary cursor must reach LSB as bit index 0.")) return 1;
        if (expect(screen.reverse_chars[4][13] && !screen.reverse_chars[4][12],
                   "Binary cursor must select exactly one bit at LSB.")) return 1;

        if (expect(mon.poll(0) == 0, "Binary bit test: right navigation across byte boundary failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0401/7") == header,
                   "Binary cursor must cross to next byte at MSB.")) return 1;
        if (expect(screen.reverse_chars[5][6],
                   "Binary cursor must move to the next row's MSB after crossing byte boundary.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary bit test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[40];
         const int keys[] = { 'B', 'W', 'W', 'W', 'W', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        backend.write(0x0000, 0x80);
        backend.write(0x0001, 0x01);
        backend.write(0x0002, 0xFF);
         backend.write(0x0003, 0x55);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary width test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary width test: cycle to width 2 failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary width test: cycle to width 3 failed.")) return 1;
         if (expect(mon.poll(0) == 0, "Binary width test: cycle to width 3S failed.")) return 1;
         if (expect(mon.poll(0) == 0, "Binary width test: cycle to width 4 failed.")) return 1;
         screen.get_slice(1, 4, 37, row);
         if (expect(strcmp(row, "0000 *..............*********.*.*.*.*") == 0,
             "Binary width cycling must reach width 4 without a hex preview.")) return 1;
         if (expect(strstr(row, "80") == NULL && strstr(row, "01") == NULL &&
                 strstr(row, "FF") == NULL && strstr(row, "55") == NULL,
                 "Binary width 4 must omit the trailing hex bytes.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "W=") == NULL && strstr(header, "bits") == NULL,
                   "Binary width must not be displayed in the header.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary width test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_binary_row_formats(void)
{
    static const struct {
        int width_presses;
        const char *expected_row;
        int expected_len;
    } cases[] = {
        { 0, "0000 ...*..*. 12", 16 },
        { 1, "0000 ...*..*. ..**.*.. 12 34", 28 },
        { 2, "0000 ...*..*. ..**.*.. .*.*.**. 123456", 38 },
        { 3, "0000 ...*..*...**.*...*.*.**. 12 34 56", 38 },
        { 4, "0000 ...*..*...**.*...*.*.**..****...", 37 },
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char row[40];
        int keys[6];
        int key_count = 0;

        backend.write(0x0000, 0x12);
        backend.write(0x0001, 0x34);
        backend.write(0x0002, 0x56);
        backend.write(0x0003, 0x78);
        monitor_reset_saved_state();

        keys[key_count++] = 'B';
        for (int press = 0; press < cases[i].width_presses; press++) {
            keys[key_count++] = 'W';
        }
        keys[key_count++] = KEY_BREAK;
        FakeKeyboard kb(keys, key_count);
        ui.screen = &screen;
        ui.keyboard = &kb;

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary row format test: binary view switch failed.")) return 1;
        for (int press = 0; press < cases[i].width_presses; press++) {
            if (expect(mon.poll(0) == 0, "Binary row format test: width cycling failed.")) return 1;
        }
        screen.get_slice(1, 4, cases[i].expected_len, row);
        if (expect(strcmp(row, cases[i].expected_row) == 0,
                   "Binary row format test: rendered row mismatch.")) return 1;
        if (expect((int)strlen(row) == cases[i].expected_len,
                   "Binary row format test: rendered row length mismatch.")) return 1;
        if (expect(cases[i].expected_len <= 38,
                   "Binary row format test: row must fit within 38 columns.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary row format test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_memory_row_width_cycle(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char row8[38];
    char row16[39];
    const int keys[] = { 'W', 'I', 'M', 'W', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    for (int i = 0; i < 16; i++) {
        backend.write((uint16_t)i, (uint8_t)i);
    }
    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);

    screen.get_slice(1, 4, 37, row8);
    if (expect(strcmp(row8, "0000 00 01 02 03 04 05 06 07 ........") == 0,
               "Memory view must default to width 8 with ASCII on the right.")) return 1;

    if (expect(mon.poll(0) == 0, "Memory width test: cycle to width 16 failed.")) return 1;
    screen.get_slice(1, 4, 38, row16);
    if (expect(strcmp(row16, "0000 0001020304050607 08090A0B0C0D0E0F") == 0,
               "Memory width cycling must render the packed 16-byte row.")) return 1;

    if (expect(mon.poll(0) == 0, "Memory width test: ASCII view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Memory width test: return to memory view failed.")) return 1;
    screen.get_slice(1, 4, 38, row16);
    if (expect(strcmp(row16, "0000 0001020304050607 08090A0B0C0D0E0F") == 0,
               "Memory view must remember the chosen width when re-entering the view.")) return 1;

    if (expect(mon.poll(0) == 0, "Memory width test: cycle back to width 8 failed.")) return 1;
    screen.get_slice(1, 4, 37, row8);
    if (expect(strcmp(row8, "0000 00 01 02 03 04 05 06 07 ........") == 0,
               "Memory width cycling must wrap back to width 8.")) return 1;

    if (expect(mon.poll(0) == 1, "Memory width test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_binary_delete_behavior(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[14];
        const int keys[] = { 'J', 'B', KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        backend.write(0x0400, 0xAA);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary DEL set-bit test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL set-bit test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL set-bit test: delete failed.")) return 1;
        if (expect(backend.read(0x0400) == 0x2A,
                   "Binary DEL must clear only the selected set bit.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0400/6") == header,
                   "Binary DEL must advance the cursor by exactly one bit.")) return 1;
        screen.get_slice(1, 4, 13, row);
        if (expect(strcmp(row, "0400 ..*.*.*.") == 0,
                   "Binary DEL must render the cleared bit as '.' without changing the other bits.")) return 1;
        if (expect(screen.reverse_chars[4][7] && !screen.reverse_chars[4][6],
                   "Binary DEL must leave the highlight on the next bit, not the original one.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary DEL set-bit test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[14];
        const int keys[] = { 'J', 'B', 'E', KEY_DELETE, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        backend.write(0x0400, 0x2A);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary DEL clear-bit test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL clear-bit test: view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL clear-bit test: edit entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL clear-bit test: delete failed.")) return 1;
        if (expect(backend.read(0x0400) == 0x2A,
                   "Binary DEL on an already-clear bit must leave the byte unchanged.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0400/6") == header,
                   "Binary DEL must still advance one bit while in edit mode.")) return 1;
        screen.get_slice(1, 4, 13, row);
        if (expect(strcmp(row, "0400 ..*.*.*.") == 0,
                   "Binary DEL on a clear bit must preserve the rendered binary row.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL clear-bit test: leave edit mode failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary DEL clear-bit test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'J', 'B', KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
                             KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0400", 1);
        backend.write(0x0400, 0x01);
        backend.write(0x0401, 0x80);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary DEL boundary test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL boundary test: view switch failed.")) return 1;
        for (int i = 0; i < 7; i++) {
            if (expect(mon.poll(0) == 0, "Binary DEL boundary test: cursor advance to bit 0 failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "Binary DEL boundary test: delete failed.")) return 1;
        if (expect(backend.read(0x0400) == 0x00,
                   "Binary DEL must clear the final bit in the byte without touching the next byte.")) return 1;
        if (expect(backend.read(0x0401) == 0x80,
                   "Binary DEL byte-boundary advance must not modify the next byte.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0401/7") == header,
                   "Binary DEL must advance to the next byte's MSB after bit 0.")) return 1;
        if (expect(screen.reverse_chars[5][6],
                   "Binary DEL across a byte boundary must highlight the next row's first bit.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary DEL boundary test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[14];
        const int keys[] = { 'J', 'B', KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
                             KEY_RIGHT, KEY_RIGHT, KEY_RIGHT, KEY_DELETE, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("FFFF", 1);
        backend.write(0xFFFF, 0x01);
        backend.write(0x0000, 0x80);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary DEL wrap test: goto failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary DEL wrap test: view switch failed.")) return 1;
        for (int i = 0; i < 7; i++) {
            if (expect(mon.poll(0) == 0, "Binary DEL wrap test: cursor advance to bit 0 failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "Binary DEL wrap test: delete failed.")) return 1;
        if (expect(backend.read(0xFFFF) == 0x00,
                   "Binary DEL must clear the selected bit at $FFFF before wrapping.")) return 1;
        if (expect(backend.read(0x0000) == 0x80,
                   "Binary DEL wraparound must not modify the wrapped-to byte.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR BIN $0000/7") == header,
                   "Binary DEL at $FFFF bit 0 must wrap to $0000 bit 7.")) return 1;
        screen.get_slice(1, 4, 13, row);
        if (expect(strcmp(row, "0000 *.......") == 0,
                   "Binary DEL wraparound must render the wrapped-to byte using the existing binary row layout.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary DEL wrap test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_clipboard_number_and_range(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int keys[16];
        int n = 0;
        keys[n++] = 'N';
        keys[n++] = KEY_DOWN;   // Decimal representation.
        keys[n++] = KEY_CTRL_C;
        keys[n++] = 'B';
        for (int i = 0; i < 8; i++) keys[n++] = KEY_RIGHT;
        keys[n++] = KEY_CTRL_V;
        keys[n++] = KEY_BREAK;
        FakeKeyboard kb(keys, n);
        ui.screen = &screen;
        ui.keyboard = &kb;
        backend.write(0x0000, 65);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Number inspector did not open.")) return 1;
        if (expect(mon.poll(0) == 0, "Number inspector decimal navigation failed.")) return 1;
         if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Number inspector must open as a framed popup inside the monitor content area.")) return 1;
        if (expect(screen.reverse_chars[top + 3][left + 1],
               "Number inspector must allow navigating to the decimal representation.")) return 1;
        if (expect(mon.poll(0) == 0, "Number inspector CBM-C copy failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary view switch after number copy failed.")) return 1;
        for (int i = 0; i < 8; i++) {
            if (expect(mon.poll(0) == 0, "Binary cursor move before paste failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "CBM-V paste into binary mode failed.")) return 1;
        if (expect(backend.read(0x0001) == 65,
                   "Number clipboard paste must write canonical byte value in binary mode.")) return 1;
        if (expect(mon.poll(0) == 1, "Number clipboard test exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = {
            'R', KEY_RIGHT, KEY_RIGHT, KEY_RIGHT,
            KEY_CTRL_C, KEY_RIGHT, KEY_CTRL_V, KEY_CTRL_V, KEY_BREAK
        };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        for (int i = 0; i < 4; i++) {
            backend.write((uint16_t)i, (uint8_t)(0xA0 + i));
        }
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Range mode entry failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "Range") != NULL,
                   "Header must show Range while range mode is active.")) return 1;
        if (expect(mon.poll(0) == 0 && mon.poll(0) == 0 && mon.poll(0) == 0,
                   "Range cursor movement failed.")) return 1;
        if (expect(screen.reverse_chars[4][6] && screen.reverse_chars[4][9] &&
                   screen.reverse_chars[4][12] && screen.reverse_chars[4][15],
                   "Range selection must visibly invert every selected byte.")) return 1;
        if (expect(mon.poll(0) == 0, "Range CBM-C copy failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "Range") == NULL,
                   "CBM-C must exit range mode after copying.")) return 1;
        if (expect(mon.poll(0) == 0, "Range paste target movement failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Range paste failed.")) return 1;
        for (int i = 0; i < 4; i++) {
            if (expect(backend.read((uint16_t)(4 + i)) == (uint8_t)(0xA0 + i),
                       "Range paste must reproduce copied bytes sequentially.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "Repeat range paste failed.")) return 1;
        for (int i = 0; i < 4; i++) {
            if (expect(backend.read((uint16_t)(8 + i)) == (uint8_t)(0xA0 + i),
                       "Repeated CBM-V must duplicate the last copied content.")) return 1;
        }
        if (expect(mon.poll(0) == 1, "Range clipboard test exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_asm_range_copy_paste(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    const int keys[] = { 'J', 'A', 'R', KEY_DOWN, KEY_CTRL_C, 'J', KEY_CTRL_V, KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    const uint8_t expected[] = { 0xA9, 0x12, 0x8D, 0x00, 0x04 };

    backend.write(0xC000, 0xA9);
    backend.write(0xC001, 0x12);
    backend.write(0xC002, 0x8D);
    backend.write(0xC003, 0x00);
    backend.write(0xC004, 0x04);

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    ui.push_prompt("C100", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        if (expect(mon.poll(0) == 0, "ASM range test command failed.")) return 1;
    }

    for (int i = 0; i < (int)sizeof(expected); i++) {
        if (expect(backend.read((uint16_t)(0xC100 + i)) == expected[i],
                   "ASM range copy/paste must include every byte of the final selected instruction.")) return 1;
    }
    if (expect(backend.read(0xC105) == 0x00,
               "ASM range copy/paste must not overrun past the selected instructions.")) return 1;
    if (expect(mon.poll(0) == 1, "ASM range test exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_asm_paste_keeps_viewport_position(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[39];
    int keys[32];
    int n = 0;
    const uint8_t expected[] = { 0xA9, 0x12, 0x8D, 0x00, 0x04 };

    keys[n++] = 'J';
    keys[n++] = 'A';
    keys[n++] = 'R';
    keys[n++] = KEY_DOWN;
    keys[n++] = KEY_CTRL_C;
    keys[n++] = 'J';
    for (int i = 0; i < 8; i++) {
        keys[n++] = KEY_DOWN;
    }
    keys[n++] = KEY_CTRL_V;
    keys[n++] = KEY_BREAK;

    backend.write(0xC000, 0xA9);
    backend.write(0xC001, 0x12);
    backend.write(0xC002, 0x8D);
    backend.write(0xC003, 0x00);
    backend.write(0xC004, 0x04);
    for (uint16_t addr = 0xC100; addr < 0xC120; addr++) {
        backend.write(addr, 0xEA);
    }

    FakeKeyboard kb(keys, n);
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    ui.push_prompt("C100", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: goto source failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: range mode failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: range extension failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: copy failed.")) return 1;
    if (expect(mon.poll(0) == 0, "ASM paste viewport test: goto target failed.")) return 1;
    for (int i = 0; i < 8; i++) {
        if (expect(mon.poll(0) == 0, "ASM paste viewport test: target navigation failed.")) return 1;
    }

    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "C100 EA") == line,
               "ASM paste viewport test must start with the original top row still visible.")) return 1;

    if (expect(mon.poll(0) == 0, "ASM paste viewport test: paste failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "MONITOR ASM $C10D") == line,
               "ASM paste must leave the cursor on the byte immediately after the pasted region.")) return 1;
    screen.get_slice(1, 4, 38, line);
    if (expect(strstr(line, "C100 EA") == line,
               "ASM paste must keep the current viewport anchored instead of snapping the pasted region to the top.")) return 1;
    for (int i = 0; i < (int)sizeof(expected); i++) {
        if (expect(backend.read((uint16_t)(0xC108 + i)) == expected[i],
                   "ASM paste viewport test must write the copied bytes at the current cursor position.")) return 1;
    }

    if (expect(mon.poll(0) == 1, "ASM paste viewport test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_illegal_mode_header_label(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[39];
    const int keys[] = { 'A', '*', 'U', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Illegal mode label test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Illegal mode label test: '*' should now be ignored.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "UndocOp") == NULL,
               "Disabled undocumented-opcode mode must not show the UndocOp label.")) return 1;
    if (expect(strstr(line, "IllOp") == NULL,
               "Visible monitor UI must no longer show the old IllOp label.")) return 1;
    if (expect(mon.poll(0) == 0, "Illegal mode label test: U toggle failed.")) return 1;
    screen.get_slice(1, 3, 38, line);
    if (expect(strstr(line, "Undoc") != NULL,
               "Enabled undocumented-opcode mode must show the compact Undoc label in the header.")) return 1;
    if (expect(strstr(line, "Illegal:ON") == NULL,
               "Illegal opcode mode must no longer show the old long header text.")) return 1;
    if (expect(mon.poll(0) == 1, "Illegal mode label test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_hunt_and_compare_picker_navigation(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[38];
        const int keys[] = { 'H', 'D', 'E', KEY_RETURN, KEY_PAGEDOWN, KEY_RETURN, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        for (uint16_t addr = 0xC000; addr <= 0xC013; addr++) {
            backend.write(addr, 0xDE);
        }
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Hunt picker test: hunt command failed.")) return 1;
        screen.get_slice(1, 4, 37, row);
        if (expect(strstr(row, "C000 DE") == row, "Hunt picker must show the first match at the top initially.")) return 1;
        if (expect(mon.poll(0) == 0, "Hunt picker test: page-down failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Hunt picker test: RETURN jump failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR HEX $C012") == header,
                   "Hunt picker RETURN must jump to the paged selection.")) return 1;
        if (expect(mon.poll(0) == 1, "Hunt picker test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        char row[38];
        const int keys[] = { 'C', KEY_DOWN, KEY_RETURN, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        backend.write(0xC100, 0x10);
        backend.write(0xC101, 0x11);
        backend.write(0xC102, 0x12);
        backend.write(0xC103, 0x13);
        backend.write(0xC200, 0x10);
        backend.write(0xC201, 0x91);
        backend.write(0xC202, 0x92);
        backend.write(0xC203, 0x13);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C100-C103,C200", 1);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Compare picker test: compare command failed.")) return 1;
        screen.get_slice(1, 4, 37, row);
        if (expect(strstr(row, "C101 11") == row, "Compare picker must list the first differing address first.")) return 1;
        if (expect(mon.poll(0) == 0, "Compare picker test: DOWN failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Compare picker test: RETURN jump failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR HEX $C102") == header,
                   "Compare picker RETURN must jump to the selected difference.")) return 1;
        if (expect(mon.poll(0) == 1, "Compare picker test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_hunt_prompt_typed_input(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'H', 'd', 'e', KEY_RETURN, KEY_RETURN, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        backend.write(0xC004, 0xDE);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(mon.poll(0) == 0, "Typed Hunt test: lowercase hex prompt failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Typed Hunt test: lowercase hex RETURN failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR HEX $C004") == header,
                   "Typed Hunt must accept lowercase hex input in the Hunt popup.")) return 1;
        if (expect(mon.poll(0) == 1, "Typed Hunt test: lowercase hex exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        const int keys[] = { 'H', '"', 'H', 'e', 'l', 'l', 'o', '"', KEY_RETURN, KEY_RETURN, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        const char hello[] = "Hello";
        const char upper_hello[] = "HELLO";

        for (unsigned int i = 0; i < sizeof(hello) - 1; i++) {
            backend.write((uint16_t)(0xC000 + i), (uint8_t)hello[i]);
            backend.write((uint16_t)(0xC008 + i), (uint8_t)upper_hello[i]);
        }

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(mon.poll(0) == 0, "Typed Hunt test: quoted ASCII prompt failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Typed Hunt test: quoted ASCII RETURN failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "MONITOR HEX $C000") == header,
                   "Typed Hunt must preserve quoted mixed-case ASCII bytes.")) return 1;
        if (expect(mon.poll(0) == 1, "Typed Hunt test: quoted ASCII exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_number_popup_edit_and_commit(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[32];
    const int keys[] = { 'N', 'F', 'A', 'E', '1', KEY_DELETE, KEY_DELETE, KEY_RETURN, KEY_BREAK, KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);

    if (expect(mon.poll(0) == 0, "Number popup test: open failed.")) return 1;
    get_popup_line(screen, 0, line, sizeof(line));
    if (expect(strstr(line, "MONITOR NUM $0000 BYTE") == line,
               "Number popup must open in BYTE mode with the current address in the framed header.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: typing first hex digit failed.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $0F") == line,
               "Number popup must zero-pad one-digit hex input as a BYTE preview.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: typing second hex digit failed.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $FA") == line,
               "Number popup must keep two-digit hex input in BYTE mode.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: typing third hex digit failed.")) return 1;
    get_popup_line(screen, 0, line, sizeof(line));
    if (expect(strstr(line, "MONITOR NUM $0000 WORD") == line,
               "Number popup must switch to WORD mode on the third hex digit.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $0FAE") == line,
               "Number popup must preserve leading zeroes when hex input grows to WORD width.")) return 1;
    get_popup_line(screen, 3, line, sizeof(line));
    if (expect(strcmp(line, "Binary   0000111110101110") == 0,
               "Number popup WORD binary preview must show all 16 bits without truncation.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: typing fourth hex digit failed.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $FAE1") == line,
               "Number popup must show the full four-digit hex WORD preview.")) return 1;
    get_popup_line(screen, 3, line, sizeof(line));
    if (expect(strcmp(line, "Binary   1111101011100001") == 0,
               "Number popup must show the full 16-bit WORD binary preview for $FAE1.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: first delete failed.")) return 1;
    get_popup_line(screen, 0, line, sizeof(line));
    if (expect(strstr(line, "MONITOR NUM $0000 WORD") == line,
               "Deleting from four to three hex digits must keep the Number popup in WORD mode.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $0FAE") == line,
               "Deleting one hex digit must keep the remaining WORD preview intact.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: second delete failed.")) return 1;
    get_popup_line(screen, 0, line, sizeof(line));
    if (expect(strstr(line, "MONITOR NUM $0000 BYTE") == line,
               "Deleting from three to two hex digits must fall back to BYTE mode.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strstr(line, "Hex      $FA") == line,
               "Deleting back to two hex digits must preserve the BYTE preview value.")) return 1;
    get_popup_line(screen, 3, line, sizeof(line));
    if (expect(strstr(line, "Binary   11111010") == line && line[17] == ' ',
               "Number popup BYTE binary preview must remain 8 bits wide.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: Enter commit failed.")) return 1;
    if (expect(backend.read(0x0000) == 0xFA && backend.read(0x0001) == 0x00,
               "Number popup Enter must commit the current BYTE preview without touching the next byte.")) return 1;

    if (expect(mon.poll(0) == 0, "Number popup test: popup close failed.")) return 1;
    if (expect(mon.poll(0) == 1, "Number popup test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_number_popup_word_commit_and_sticky_row(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        const int keys[] = {
            'N', KEY_DOWN, KEY_DOWN, KEY_DOWN, 'A', 'B', KEY_RETURN,
            KEY_ESCAPE, KEY_RIGHT, 'N', 'C', KEY_RETURN, KEY_BREAK, KEY_BREAK
        };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(mon.poll(0) == 0, "ASCII Number popup test: open failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: first row navigation failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: second row navigation failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: third row navigation failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Number popup ASCII-row test must keep the popup framed.")) return 1;
        if (expect(screen.reverse_chars[top + 5][left + 1],
                   "Number popup must allow selecting the ASCII row.")) return 1;

        if (expect(mon.poll(0) == 0, "ASCII Number popup test: first character failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: second character failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $0000 WORD") == line,
                   "Two ASCII characters must switch the Number popup into WORD mode.")) return 1;
        get_popup_line(screen, 4, line, sizeof(line));
        if (expect(strstr(line, "ASCII    AB") == line,
                   "ASCII Number popup preview must show both typed characters without extra annotations.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: Enter commit failed.")) return 1;
        if (expect(backend.read(0x0000) == 0x42 && backend.read(0x0001) == 0x41,
                   "ASCII WORD commit must write low/high bytes in little-endian order.")) return 1;

        if (expect(mon.poll(0) == 0, "ASCII Number popup test: close after commit failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: move to next address failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: reopen failed.")) return 1;
        get_popup_line(screen, 0, line, sizeof(line));
        if (expect(strstr(line, "MONITOR NUM $0001 BYTE") == line,
                   "Reopening the Number popup must use the current address and a fresh BYTE preview.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Number popup reopen test must keep the popup framed.")) return 1;
        if (expect(screen.reverse_chars[top + 5][left + 1],
                   "The Number popup must remember the selected row across close and reopen in one monitor session.")) return 1;

        if (expect(mon.poll(0) == 0, "ASCII Number popup test: repeated-entry character failed.")) return 1;
        if (expect(mon.poll(0) == 0, "ASCII Number popup test: repeated-entry Enter failed.")) return 1;
        if (expect(backend.read(0x0001) == 0x43 && backend.read(0x0002) == 0x00,
                   "Reopened Number popup must start with a fresh ASCII edit buffer at the new address.")) return 1;

        if (expect(mon.poll(0) == 0, "ASCII Number popup test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "ASCII Number popup test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'N', KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, 'A', 'B', KEY_RETURN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(mon.poll(0) == 0, "Screen Number popup test: open failed.")) return 1;
        for (int i = 0; i < 4; i++) {
            if (expect(mon.poll(0) == 0, "Screen Number popup test: row navigation failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "Screen Number popup test: first character failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen Number popup test: second character failed.")) return 1;
        get_popup_line(screen, 5, line, sizeof(line));
        if (expect(strstr(line, "Screen   AB") == line,
                   "Screen Number popup preview must show screen characters without redundant hex suffixes.")) return 1;
        if (expect(strchr(line, '(') == NULL,
                   "Screen Number popup must not append per-byte hex annotations.")) return 1;
        if (expect(mon.poll(0) == 0, "Screen Number popup test: Enter commit failed.")) return 1;
        if (expect(backend.read(0x0000) == 0x02 && backend.read(0x0001) == 0x01,
                   "Screen WORD commit must use screen-code values written in little-endian order.")) return 1;

        if (expect(mon.poll(0) == 0, "Screen Number popup test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Screen Number popup test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char line[32];
        const int keys[] = { 'V', 'U', 'N', KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, 'A', KEY_RETURN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: Screen view switch failed.")) return 1;
        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: charset toggle failed.")) return 1;
        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: popup open failed.")) return 1;
        for (int i = 0; i < 4; i++) {
            if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: row navigation failed.")) return 1;
        }
        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: character entry failed.")) return 1;
        get_popup_line(screen, 5, line, sizeof(line));
        if (expect(strstr(line, "Screen   A") == line,
                   "Screen Number popup preview must use the active Screen charset display.")) return 1;
        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: commit failed.")) return 1;
        if (expect(backend.read(0x0000) == 0x41,
                   "Screen Number popup input must use the active Screen charset input mapping.")) return 1;
        if (expect(mon.poll(0) == 0, "L/U Screen Number popup test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "L/U Screen Number popup test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_number_popup_placement_and_overlay_redraw(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int cursor_x = 0;
        int cursor_y = 0;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        const int keys[] = { 'N', '1', KEY_DOWN, KEY_DELETE, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);

        if (expect(find_highlighted_cell(screen, &cursor_x, &cursor_y),
                   "Placement test: initial cursor highlight not found.")) return 1;
        if (expect(mon.poll(0) == 0, "Placement test: open failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
                   "Placement test: framed popup not found.")) return 1;
        if (expect(left == cursor_x + 1 && top == cursor_y + 1,
                   "Top-left Number popup must be diagonally down-right from the invoking cursor when space allows.")) return 1;
        if (expect(!(cursor_x >= left && cursor_x <= right && cursor_y >= top && cursor_y <= bottom),
                   "Number popup must not cover the invoking cursor cell.")) return 1;
        if (expect(screen.reverse_chars[cursor_y][cursor_x],
                   "The invoking cursor cell must remain visible after opening the Number popup.")) return 1;

        screen.reset_write_counts();
        if (expect(mon.poll(0) == 0, "Placement test: typing should keep popup open.")) return 1;
        if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
                   "Typing inside the Number popup must not redraw uncovered monitor cells.")) return 1;
        screen.reset_write_counts();
        if (expect(mon.poll(0) == 0, "Placement test: row navigation should keep popup open.")) return 1;
        if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
                   "Popup row navigation must not redraw uncovered monitor cells.")) return 1;
        screen.reset_write_counts();
        if (expect(mon.poll(0) == 0, "Placement test: delete should keep popup open.")) return 1;
        if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
                   "Deleting inside the Number popup must not redraw uncovered monitor cells.")) return 1;

        if (expect(mon.poll(0) == 0, "Placement test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Placement test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int cursor_x = 0;
        int cursor_y = 0;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int keys[32];
        int n = 0;
        keys[n++] = 'I';
        for (int i = 0; i < 22; i++) keys[n++] = KEY_RIGHT;
        keys[n++] = 'N';
        keys[n++] = KEY_BREAK;
        keys[n++] = KEY_BREAK;
        FakeKeyboard kb(keys, n);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Top-right placement test: ASCII view switch failed.")) return 1;
        for (int i = 0; i < 22; i++) {
            if (expect(mon.poll(0) == 0, "Top-right placement test: cursor movement failed.")) return 1;
        }
        if (expect(find_highlighted_cell(screen, &cursor_x, &cursor_y),
                   "Top-right placement test: cursor highlight not found.")) return 1;
        if (expect(mon.poll(0) == 0, "Top-right placement test: open failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
                   "Top-right placement test: framed popup not found.")) return 1;
        if (expect(right == cursor_x - 1 && top == cursor_y + 1,
                   "Top-right Number popup must be diagonally down-left from the invoking cursor when space allows.")) return 1;
        if (expect(mon.poll(0) == 0, "Top-right placement test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Top-right placement test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int cursor_x = 0;
        int cursor_y = 0;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int keys[32];
        int n = 0;
        for (int i = 0; i < 17; i++) keys[n++] = KEY_DOWN;
        keys[n++] = 'N';
        keys[n++] = KEY_BREAK;
        keys[n++] = KEY_BREAK;
        FakeKeyboard kb(keys, n);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int i = 0; i < 17; i++) {
            if (expect(mon.poll(0) == 0, "Bottom-left placement test: cursor movement failed.")) return 1;
        }
        if (expect(find_highlighted_cell(screen, &cursor_x, &cursor_y),
                   "Bottom-left placement test: cursor highlight not found.")) return 1;
        if (expect(mon.poll(0) == 0, "Bottom-left placement test: open failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
                   "Bottom-left placement test: framed popup not found.")) return 1;
        if (expect(left == cursor_x + 1 && bottom == cursor_y - 1,
                   "Bottom-left Number popup must be diagonally up-right from the invoking cursor when space allows.")) return 1;
        if (expect(mon.poll(0) == 0, "Bottom-left placement test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Bottom-left placement test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int cursor_x = 0;
        int cursor_y = 0;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int keys[64];
        int n = 0;
        keys[n++] = 'I';
        for (int i = 0; i < 17; i++) keys[n++] = KEY_DOWN;
        for (int i = 0; i < 22; i++) keys[n++] = KEY_RIGHT;
        keys[n++] = 'N';
        keys[n++] = KEY_BREAK;
        keys[n++] = KEY_BREAK;
        FakeKeyboard kb(keys, n);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Bottom-right placement test: ASCII view switch failed.")) return 1;
        for (int i = 0; i < 39; i++) {
            if (expect(mon.poll(0) == 0, "Bottom-right placement test: cursor movement failed.")) return 1;
        }
        if (expect(find_highlighted_cell(screen, &cursor_x, &cursor_y),
                   "Bottom-right placement test: cursor highlight not found.")) return 1;
        if (expect(mon.poll(0) == 0, "Bottom-right placement test: open failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
                   "Bottom-right placement test: framed popup not found.")) return 1;
        if (expect(right == cursor_x - 1 && bottom == cursor_y - 1,
                   "Bottom-right Number popup must be diagonally up-left from the invoking cursor when space allows.")) return 1;
        if (expect(mon.poll(0) == 0, "Bottom-right placement test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Bottom-right placement test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int cursor_x = 0;
        int cursor_y = 0;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int keys[40];
        int n = 0;
        keys[n++] = 'I';
        for (int i = 0; i < 8; i++) keys[n++] = KEY_DOWN;
        for (int i = 0; i < 22; i++) keys[n++] = KEY_RIGHT;
        keys[n++] = 'N';
        keys[n++] = KEY_BREAK;
        keys[n++] = KEY_BREAK;
        FakeKeyboard kb(keys, n);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Center placement test: ASCII view switch failed.")) return 1;
        for (int i = 0; i < 30; i++) {
            if (expect(mon.poll(0) == 0, "Center placement test: cursor movement failed.")) return 1;
        }
        if (expect(find_highlighted_cell(screen, &cursor_x, &cursor_y),
                   "Center placement test: cursor highlight not found.")) return 1;
        if (expect(mon.poll(0) == 0, "Center placement test: open failed.")) return 1;
        if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
                   "Center placement test: framed popup not found.")) return 1;
        if (expect(!(cursor_x >= left && cursor_x <= right && cursor_y >= top && cursor_y <= bottom),
                   "Mid-row Number popup placement must keep the invoking cursor visible.")) return 1;
        if (expect(left > 0 && top > 2 && right < 39 && bottom < 22,
                   "Mid-row Number popup placement must stay inside the monitor content bounds.")) return 1;
        if (expect(mon.poll(0) == 0, "Center placement test: popup close failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Center placement test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_disassembly_up_keeps_screen_row(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int row_before = -1;
    int row_after = -1;
    const int keys[] = { 'A', 'J', KEY_DOWN, KEY_DOWN, KEY_UP, KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    backend.write(0x0000, 0x4C);
    backend.write(0x0001, 0x00);
    backend.write(0x0002, 0xEA);
    backend.write(0x0003, 0xEA);
    backend.write(0x0004, 0xEA);

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("0001", 1);
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);

    if (expect(mon.poll(0) == 0, "Disassembly row test: ASM view switch failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Disassembly row test: goto failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Disassembly row test: first step down failed.")) return 1;
    if (expect(mon.poll(0) == 0, "Disassembly row test: second step down failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_before),
               "Disassembly row test: highlighted current line not found before stepping up.")) return 1;
    if (expect(row_before > 4,
               "Disassembly row test must place the cursor away from the top row before stepping up.")) return 1;
    if (expect(mon.poll(0) == 0, "Disassembly row test: step up failed.")) return 1;
    if (expect(find_highlighted_cell(screen, NULL, &row_after),
               "Disassembly row test: highlighted current line not found after stepping up.")) return 1;
    if (expect(row_after == row_before - 1,
               "Stepping upward in Assembly view must move the highlighted line up by one row without snapping it to the top.")) return 1;
    if (expect(mon.poll(0) == 1, "Disassembly row test: exit failed.")) return 1;
    mon.deinit();
    return 0;
}

static int test_fixed_prompt_widths(void)
{
    struct PromptCase {
        int key;
        const char *message;
        int maxlen;
    } cases[] = {
        { 'F', "Fill AAAA-BBBB,DD", 13 },
        { 'T', "Transfer AAAA-BBBB,CCCC", 15 },
        { 'C', "Compare AAAA-BBBB,CCCC", 15 },
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        int keys[] = { cases[i].key, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("", 0);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Fixed prompt width test: command key failed.")) return 1;
        if (expect(strcmp(ui.last_prompt_message, cases[i].message) == 0,
                   "Fixed prompt width test: monitor used the wrong prompt template.")) return 1;
        if (expect(ui.last_prompt_maxlen == cases[i].maxlen,
                   "Fixed prompt width test: prompt width did not match the expected bounded template length.")) return 1;
        if (expect(mon.poll(0) == 1, "Fixed prompt width test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_prompt_cancel_and_empty_clipboard(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'F', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("", 0);
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Prompt cancel test: fill command should stay in the monitor.")) return 1;
        if (expect(backend.read(0x0000) == 0x00 && ui.popup_count == 0,
                   "Canceled prompt must not mutate memory or raise an error popup.")) return 1;
        if (expect(mon.poll(0) == 1, "Prompt cancel test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { KEY_CTRL_V, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Empty clipboard test: paste should not exit.")) return 1;
        if (expect(ui.popup_count == 1 && strcmp(ui.last_popup, "?CLIP") == 0,
                   "Pasting with an empty clipboard must raise ?CLIP.")) return 1;
        if (expect(mon.poll(0) == 1, "Empty clipboard test: exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_load_save_and_goto_command_flow(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'L', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("PRG,0001,2", 1);
        monitor_reset_saved_state();
        monitor_io::reset_fake_monitor_io();
        monitor_io::g_monitor_io.pick_file_result = true;
        strcpy(monitor_io::g_monitor_io.pick_path, "/tmp");
        strcpy(monitor_io::g_monitor_io.pick_name, "demo.prg");
        monitor_io::g_monitor_io.load_data[0] = 0x01;
        monitor_io::g_monitor_io.load_data[1] = 0x08;
        monitor_io::g_monitor_io.load_data[2] = 0xAA;
        monitor_io::g_monitor_io.load_data[3] = 0xBB;
        monitor_io::g_monitor_io.load_data[4] = 0xCC;
        monitor_io::g_monitor_io.load_size = 5;

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "LOAD command flow test failed.")) return 1;
        if (expect(monitor_io::g_monitor_io.load_called && monitor_io::g_monitor_io.pick_file_calls == 1 &&
                   strcmp(monitor_io::g_monitor_io.load_path, "/tmp") == 0 &&
                   strcmp(monitor_io::g_monitor_io.load_name, "demo.prg") == 0,
                   "LOAD command must invoke the monitor I/O shim with the picked file.")) return 1;
        if (expect(backend.read(0x0801) == 0xBB && backend.read(0x0802) == 0xCC,
                   "LOAD command must honour PRG header, file offset, and length.")) return 1;
        if (expect(ui.popup_count == 1 && strcmp(ui.last_popup, "LOAD demo.prg\n$0801-$0802 (2 bytes)") == 0,
                   "LOAD command must show a two-line confirmation popup with the effective byte range.")) return 1;
        if (expect(mon.poll(0) == 1, "LOAD command flow test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'S', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        backend.write(0x0801, 0xA9);
        backend.write(0x0802, 0x42);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0801-0802", 1);
        monitor_reset_saved_state();
        monitor_io::reset_fake_monitor_io();
        monitor_io::g_monitor_io.pick_file_result = true;
        strcpy(monitor_io::g_monitor_io.pick_path, "/tmp");
        strcpy(monitor_io::g_monitor_io.pick_name, "save.prg");

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "SAVE command flow test failed.")) return 1;
        if (expect(monitor_io::g_monitor_io.save_called && monitor_io::g_monitor_io.last_pick_save_mode,
                   "SAVE command must invoke the monitor save shim in save-pick mode.")) return 1;
        if (expect(monitor_io::g_monitor_io.saved_size == 4 &&
                   monitor_io::g_monitor_io.saved_data[0] == 0x01 &&
                   monitor_io::g_monitor_io.saved_data[1] == 0x08 &&
                   monitor_io::g_monitor_io.saved_data[2] == 0xA9 &&
                   monitor_io::g_monitor_io.saved_data[3] == 0x42,
                   "SAVE command must emit a PRG header followed by the selected bytes.")) return 1;
        if (expect(ui.popup_count == 1 && strcmp(ui.last_popup, "SAVE save.prg\n$0801-$0802 (2 bytes)") == 0,
                   "SAVE command must show a two-line confirmation popup with the saved range.")) return 1;
        if (expect(mon.poll(0) == 1, "SAVE command flow test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'S', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        backend.write(0x0801, 0xA9);
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("0801-0801", 1);
        monitor_reset_saved_state();
        monitor_io::reset_fake_monitor_io();
        monitor_io::g_monitor_io.pick_file_result = true;
        strcpy(monitor_io::g_monitor_io.pick_path, "/tmp");
        strcpy(monitor_io::g_monitor_io.pick_name, "THIS-IS-A-VERY-LONG-FILENAME-FOR-CONFIRMATION-TESTING.PRG");

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Long confirmation filename test failed.")) return 1;
        if (expect(ui.popup_count == 1,
                   "Long confirmation filename test must produce one confirmation popup.")) return 1;
        if (expect(popup_longest_line(ui.last_popup) <= 38,
                   "Every line in the LOAD/SAVE confirmation popup must stay within 38 characters.")) return 1;
        if (expect(strstr(ui.last_popup, "SAVE ") == ui.last_popup,
                   "Long confirmation filename test must still label the operation on the first line.")) return 1;
        if (expect(strstr(ui.last_popup, "\n$0801-$0801 (1 bytes)") != NULL,
                   "Long confirmation filename test must keep the effective range on the second line.")) return 1;
        if (expect(mon.poll(0) == 1, "Long confirmation filename test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'G' };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C123", 1);
        monitor_reset_saved_state();
        monitor_io::reset_fake_monitor_io();

        BackendMachineMonitor monitor(&ui, &backend);
        ui.run_machine_monitor(&backend);
        if (expect(monitor_io::g_monitor_io.jump_called && monitor_io::g_monitor_io.jump_address == 0xC123,
                   "Go command must dispatch the requested jump address.")) return 1;
    }

    return 0;
}

static int test_hex_single_nibble_commits_on_navigation(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'E', 'E', KEY_RIGHT, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();
        backend.write(0x0000, 0xFF);

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Hex nibble cursor-right test: enter edit mode failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble cursor-right test: typing nibble failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble cursor-right test: moving right failed.")) return 1;
        if (expect(backend.read(0x0000) == 0x0E,
                   "Leaving a half-typed hex nibble with cursor-right must commit it as 0x0N.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble cursor-right test: edit-mode exit failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Hex nibble cursor-right test: monitor exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'E', 'E', KEY_PAGEDOWN, KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();
        backend.write(0x0000, 0xFF);

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Hex nibble page-move test: enter edit mode failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble page-move test: typing nibble failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble page-move test: page down failed.")) return 1;
        if (expect(backend.read(0x0000) == 0x0E,
                   "Leaving a half-typed hex nibble with page movement must commit it as 0x0N.")) return 1;
        if (expect(mon.poll(0) == 0, "Hex nibble page-move test: edit-mode exit failed.")) return 1;
        if (expect(mon.poll(0) == 1, "Hex nibble page-move test: monitor exit failed.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_header_invariants_and_parity(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeBankedMemoryBackend backend;
        char header[39];
        const int keys[] = { 'E', 'R', 'Z', KEY_BREAK, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Header test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: range mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: freeze toggle failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "Edit Mode") == NULL,
                   "Header must show Edit, not Edit Mode.")) return 1;
        if (expect(strncmp(header + 34, "EDIT", 4) == 0,
                   "Edit indicator must be fixed at top-right and uppercase.")) return 1;
        if (expect(screen.colors[3][35] == 1 && screen.colors[3][36] == 1 &&
                   screen.colors[3][37] == 1 && screen.colors[3][38] == 1,
                   "EDIT must use the shared UI accent colour used for the help/title text.")) return 1;
        if (expect(strncmp(header + 19, "Range", 5) == 0,
                   "Range indicator must use its fixed slot.")) return 1;
        if (expect(strncmp(header + 25, "Frz", 3) == 0,
                   "Freeze indicator must use its compact fixed slot.")) return 1;
        if (expect(strstr(header, "W=") == NULL,
                   "Header must not display binary width.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: RUN/STOP should leave edit mode first.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "EDIT") == NULL,
                   "Leaving edit mode must clear the far-right EDIT indicator area.")) return 1;
        if (expect(mon.poll(0) == 1, "Header test: exit failed.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui_a;
        TestUserInterface ui_b;
        CaptureScreen screen_a;
        CaptureScreen screen_b;
        FakeMemoryBackend backend_a;
        FakeMemoryBackend backend_b;
        const int keys_a[] = { 'B', KEY_RIGHT, KEY_RIGHT, 'E', '1' };
        const int keys_b[] = { 'B', KEY_RIGHT, KEY_RIGHT, 'E', '1' };
        FakeKeyboard kb_a(keys_a, sizeof(keys_a) / sizeof(keys_a[0]));
        FakeKeyboard kb_b(keys_b, sizeof(keys_b) / sizeof(keys_b[0]));
        for (int i = 0; i < 16; i++) {
            backend_a.write((uint16_t)i, (uint8_t)(0x10 + i));
            backend_b.write((uint16_t)i, (uint8_t)(0x10 + i));
        }

        ui_a.screen = &screen_a;
        ui_a.keyboard = &kb_a;
        ui_b.screen = &screen_b;
        ui_b.keyboard = &kb_b;
        monitor_reset_saved_state();
        BackendMachineMonitor mon_a(&ui_a, &backend_a);
        mon_a.init(&screen_a, &kb_a);
        for (int i = 0; i < (int)(sizeof(keys_a) / sizeof(keys_a[0])); i++) {
            if (expect(mon_a.poll(0) == 0, "Parity render A command failed.")) return 1;
        }

        monitor_reset_saved_state();
        BackendMachineMonitor mon_b(&ui_b, &backend_b);
        mon_b.init(&screen_b, &kb_b);
        for (int i = 0; i < (int)(sizeof(keys_b) / sizeof(keys_b[0])); i++) {
            if (expect(mon_b.poll(0) == 0, "Parity render B command failed.")) return 1;
        }
        if (expect_screens_equal(screen_a, screen_b,
                                 "Two monitor frontends using the shared renderer must produce byte-identical screens.")) return 1;
        mon_a.deinit();
        mon_b.deinit();
    }

    return 0;
}

static int test_poll_mode_refreshes_visible_ram(void)
{
    struct FakeNtscMemoryBackend : public FakeMemoryBackend {
        virtual uint8_t monitor_poll_hz(void) const { return 60; }
    };

    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char header[39];
    char line[9];
    const int keys[] = { 'P' };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();
    set_fake_ms_timer(0);
    backend.write(0x0000, 0x11);

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Poll mode test: P toggle failed.")) return 1;

    screen.get_slice(1, 3, 38, header);
    if (expect(strncmp(header + 29, "Poll", 4) == 0,
               "Poll indicator must appear in the top-right flag area.")) return 1;

    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "0000 11", 7) == 0,
               "Poll mode test setup must render the original byte.")) return 1;

    backend.write(0x0000, 0x22);
    set_fake_ms_timer(19);
    if (expect(mon.poll(0) == 0, "Poll mode test: early idle poll failed.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "0000 11", 7) == 0,
               "Poll mode must not refresh before the PAL frame interval elapses.")) return 1;

    advance_fake_ms_timer(1);
    if (expect(mon.poll(0) == 0, "Poll mode test: timed idle poll failed.")) return 1;
    screen.get_slice(1, 4, 8, line);
    if (expect(strncmp(line, "0000 22", 7) == 0,
               "Poll mode must refresh the displayed RAM at PAL frame rate.")) return 1;

    mon.deinit();

    {
        TestUserInterface ntsc_ui;
        CaptureScreen ntsc_screen;
        FakeNtscMemoryBackend ntsc_backend;
        const int ntsc_keys[] = { 'P' };
        FakeKeyboard ntsc_kb(ntsc_keys, sizeof(ntsc_keys) / sizeof(ntsc_keys[0]));

        ntsc_ui.screen = &ntsc_screen;
        ntsc_ui.keyboard = &ntsc_kb;
        monitor_reset_saved_state();
        set_fake_ms_timer(0);
        ntsc_backend.write(0x0000, 0x11);

        BackendMachineMonitor ntsc_mon(&ntsc_ui, &ntsc_backend);
        ntsc_mon.init(&ntsc_screen, &ntsc_kb);
        if (expect(ntsc_mon.poll(0) == 0, "NTSC poll mode test: P toggle failed.")) return 1;

        ntsc_backend.write(0x0000, 0x22);
        set_fake_ms_timer(15);
        if (expect(ntsc_mon.poll(0) == 0, "NTSC poll mode test: early first poll failed.")) return 1;
        ntsc_screen.get_slice(1, 4, 8, line);
        if (expect(strncmp(line, "0000 11", 7) == 0,
                   "NTSC poll mode must not refresh before the first 16 ms frame slice.")) return 1;

        advance_fake_ms_timer(1);
        if (expect(ntsc_mon.poll(0) == 0, "NTSC poll mode test: first frame poll failed.")) return 1;
        ntsc_screen.get_slice(1, 4, 8, line);
        if (expect(strncmp(line, "0000 22", 7) == 0,
                   "NTSC poll mode must refresh on the first 16 ms frame slice.")) return 1;

        ntsc_backend.write(0x0000, 0x33);
        set_fake_ms_timer(32);
        if (expect(ntsc_mon.poll(0) == 0, "NTSC poll mode test: early second poll failed.")) return 1;
        ntsc_screen.get_slice(1, 4, 8, line);
        if (expect(strncmp(line, "0000 22", 7) == 0,
                   "NTSC poll mode must carry the 60 Hz fractional frame interval.")) return 1;

        advance_fake_ms_timer(1);
        if (expect(ntsc_mon.poll(0) == 0, "NTSC poll mode test: second frame poll failed.")) return 1;
        ntsc_screen.get_slice(1, 4, 8, line);
        if (expect(strncmp(line, "0000 33", 7) == 0,
                   "NTSC poll mode must average to 60 Hz with 16/17 ms frame slices.")) return 1;
        ntsc_mon.deinit();
    }
    return 0;
}

static int test_edit_indicator_layout_across_views(void)
{
    static const int hex_keys[] = { 'E' };
    static const int asm_keys[] = { 'A', 'E' };
    static const int asc_keys[] = { 'I', 'E' };
    static const int scr_keys[] = { 'S', 'E' };
    static const int bin_keys[] = { 'B', 'E' };
    struct ViewCase {
        const int *keys;
        int key_count;
    } cases[] = {
        { hex_keys, 1 },
        { asm_keys, 2 },
        { asc_keys, 2 },
        { scr_keys, 2 },
        { bin_keys, 2 },
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        char header[39];
        FakeKeyboard kb(cases[i].keys, cases[i].key_count);

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int step = 0; step < cases[i].key_count; step++) {
            if (expect(mon.poll(0) == 0, "Edit indicator cross-view test: command sequence failed.")) return 1;
        }
        screen.get_slice(1, 3, 38, header);
        if (expect(strncmp(header + 34, "EDIT", 4) == 0,
                   "EDIT must remain far-right aligned in every edit-capable view.")) return 1;
        if (expect(screen.colors[3][35] == 1 && screen.colors[3][36] == 1 &&
                   screen.colors[3][37] == 1 && screen.colors[3][38] == 1,
                   "EDIT must keep the shared UI accent colour in every edit-capable view.")) return 1;
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeBankedMemoryBackend backend;
        char header[39];
        const int keys[] = { 'A', 'U', 'Z', 'P', 'E' };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

        ui.screen = &screen;
        ui.keyboard = &kb;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        for (int step = 0; step < (int)(sizeof(keys) / sizeof(keys[0])); step++) {
            if (expect(mon.poll(0) == 0, "Edit indicator layout test: ASM/freeze sequence failed.")) return 1;
        }
        screen.get_slice(1, 3, 38, header);
        if (expect(strncmp(header + 19, "Undoc", 5) == 0,
                   "Undoc must remain visible to the left of Poll and EDIT.")) return 1;
        if (expect(strncmp(header + 25, "Frz", 3) == 0,
                   "Frz must remain visible to the left of Poll and EDIT.")) return 1;
        if (expect(strncmp(header + 29, "Poll", 4) == 0,
                   "Poll must appear immediately to the left of EDIT.")) return 1;
        if (expect(strncmp(header + 34, "EDIT", 4) == 0,
                   "EDIT must remain fixed at the far right when Undoc, Freeze, and Poll are active.")) return 1;
        mon.deinit();
    }

    return 0;
}

static int test_poll_mode_disabled_on_full_refresh_screen(void)
{
    struct FullRefreshCaptureScreen : public CaptureScreen {
        virtual bool prefers_full_refresh(void) { return true; }
    };

    TestUserInterface ui;
    FullRefreshCaptureScreen screen;
    FakeMemoryBackend backend;
    char header[39];
    const int keys[] = { 'P', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));

    ui.screen = &screen;
    ui.keyboard = &kb;
    monitor_reset_saved_state();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    if (expect(mon.poll(0) == 0, "Telnet poll-mode guard: P command failed.")) return 1;
    if (expect(ui.popup_count == 1,
               "Poll mode on full-refresh screens must raise one warning popup.")) return 1;
    if (expect(strstr(ui.last_popup, "TELNET") != NULL,
               "Poll mode warning must mention the telnet restriction.")) return 1;
    screen.get_slice(1, 3, 38, header);
    if (expect(strncmp(header + 29, "Poll", 4) != 0,
               "Poll indicator must stay disabled on full-refresh screens.")) return 1;
    if (expect(mon.poll(0) == 1, "Telnet poll-mode guard: exit failed.")) return 1;
    mon.deinit();
    return 0;
}


static int test_load_save_param_parsers(void)
{
    bool use_prg = false;
    uint16_t start = 0, off16 = 0, end = 0;
    bool len_auto = false;
    uint32_t length = 0;
    uint32_t effective = 0;

    // LOAD: standard "RETURN, RETURN" path: PRG keyword + AUTO length.
    if (expect(monitor_parse_load_params("PRG,0000,AUTO", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser failed for PRG,0000,AUTO.")) return 1;
    if (expect(use_prg && off16 == 0 && len_auto, "LOAD PRG defaults parsed wrong.")) return 1;

    // LOAD: explicit start address & explicit length.
    if (expect(monitor_parse_load_params("0801,0002,1FFE", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser failed for explicit form.")) return 1;
    if (expect(!use_prg && start == 0x0801 && off16 == 0x0002 && !len_auto && length == 0x1FFE,
               "LOAD explicit parser values wrong.")) return 1;

    // LOAD: 5-digit length up to 0x10000 inclusive is permitted.
    if (expect(monitor_parse_load_params("C000,0000,10000", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept 64K length.")) return 1;
    if (expect(length == 0x10000, "LOAD 64K length parsed wrong.")) return 1;

    // LOAD: zero length must be rejected.
    if (expect(monitor_parse_load_params("0801,0000,0", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_VALUE,
               "LOAD parser must reject zero length.")) return 1;

    // LOAD: garbage in fields must be rejected.
    if (expect(monitor_parse_load_params("XYZ,00,AUTO", &use_prg, &start, &off16, &len_auto, &length) != MONITOR_OK,
               "LOAD parser must reject garbage.")) return 1;

    // LOAD: empty input -> all defaults (PRG, offset 0, AUTO length).
    use_prg = false; start = 0xDEAD; off16 = 0xDEAD; len_auto = false; length = 0xDEADBEEF;
    if (expect(monitor_parse_load_params("", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept empty input.")) return 1;
    if (expect(use_prg && off16 == 0 && len_auto, "LOAD empty defaults wrong.")) return 1;

    // LOAD: PRG only -> defaults for offset and length.
    if (expect(monitor_parse_load_params("PRG", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept 'PRG' alone.")) return 1;
    if (expect(use_prg && off16 == 0 && len_auto, "LOAD PRG-only defaults wrong.")) return 1;

    // LOAD: address only -> offset 0, length AUTO.
    if (expect(monitor_parse_load_params("0801", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept address alone.")) return 1;
    if (expect(!use_prg && start == 0x0801 && off16 == 0 && len_auto, "LOAD addr-only defaults wrong.")) return 1;

    // LOAD: address + offset, length defaults to AUTO.
    if (expect(monitor_parse_load_params("0801,1000", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept addr,off form.")) return 1;
    if (expect(!use_prg && start == 0x0801 && off16 == 0x1000 && len_auto, "LOAD addr,off defaults wrong.")) return 1;

    // LOAD: empty fields between commas -> defaults take over.
    if (expect(monitor_parse_load_params(",,0010", &use_prg, &start, &off16, &len_auto, &length) == MONITOR_OK,
               "LOAD parser must accept ,,len form.")) return 1;
    if (expect(use_prg && off16 == 0 && !len_auto && length == 0x10, "LOAD ,,len defaults wrong.")) return 1;

    // SAVE: canonical form.
    if (expect(monitor_parse_save_params("0800-9FFF", &start, &end) == MONITOR_OK,
               "SAVE parser failed for 0800-9FFF.")) return 1;
    if (expect(start == 0x0800 && end == 0x9FFF, "SAVE parser values wrong.")) return 1;

    // SAVE: inverted range must be rejected.
    if (expect(monitor_parse_save_params("9FFF-0800", &start, &end) == MONITOR_RANGE,
               "SAVE parser must reject inverted range.")) return 1;

    // Validate load size: AUTO, file fits.
    if (expect(monitor_validate_load_size(100, 0, true, 0, &effective) == MONITOR_OK && effective == 100,
               "Validate AUTO small file failed.")) return 1;

    // Validate load size: AUTO, >64K.
    if (expect(monitor_validate_load_size(0x20000, 0, true, 0, &effective) == MONITOR_RANGE,
               "Validate must reject AUTO over 64K.")) return 1;

    // Validate load size: AUTO, partial window keeps within 64K.
    if (expect(monitor_validate_load_size(0x20000, 0x10000, true, 0, &effective) == MONITOR_OK && effective == 0x10000,
               "Validate AUTO partial window failed.")) return 1;

    // Validate load size: explicit length over 64K rejected.
    if (expect(monitor_validate_load_size(0x30000, 0, false, 0x10001, &effective) == MONITOR_RANGE,
               "Validate must reject explicit length over 64K.")) return 1;

    // Validate load size: offset past end rejected.
    if (expect(monitor_validate_load_size(100, 100, true, 0, &effective) == MONITOR_RANGE,
               "Validate must reject offset==filesize.")) return 1;

    // Validate load size: requested length larger than what's left in the file.
    if (expect(monitor_validate_load_size(100, 50, false, 100, &effective) == MONITOR_RANGE,
               "Validate must reject length beyond EOF.")) return 1;

    return 0;
}

extern int g_set_screen_title_call_count;

// Regression: pressing 'W' in fixed-width views, 'Z' when freeze is unavailable,
// and 'U' outside ASM view must show a bounded warning popup, but the
// post-popup redraw must NOT call UserInterface::set_screen_title(). That
// helper repaints the screen chrome (including a horizontal-line glyph row
// just below the monitor) and would clobber the application info row,
// producing the reported "row of horizontal lines" symptom.
static int test_warning_popups_preserve_status_row(void)
{
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'I', 'W', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        g_set_screen_title_call_count = 0;
        if (expect(mon.poll(0) == 0, "ASCII view switch before W warning test failed.")) return 1;
        if (expect(mon.poll(0) == 0, "W in ASCII view should not exit the monitor.")) return 1;
        if (expect(ui.popup_count == 1, "W outside Memory/Binary view must raise exactly one warning popup.")) return 1;
        if (expect(strstr(ui.last_popup, "MEMORY") != NULL && strstr(ui.last_popup, "BINARY") != NULL,
               "W warning must mention the MEMORY/BINARY view requirement.")) return 1;
        if (expect(g_set_screen_title_call_count == 0,
                   "W warning popup must not trigger set_screen_title (would erase the row below the monitor with horizontal lines).")) return 1;
        mon.poll(0);
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeFreezeControlBackend backend(false);
        const int keys[] = { 'Z', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        g_set_screen_title_call_count = 0;
        if (expect(mon.poll(0) == 0, "Z outside overlay mode should not exit.")) return 1;
        if (expect(ui.popup_count == 1, "Z outside overlay mode must raise exactly one warning popup.")) return 1;
        if (expect(strstr(ui.last_popup, "FREEZE") != NULL && strstr(ui.last_popup, "OVERLAY") != NULL,
                   "Z warning must explain that freeze is only available in overlay mode.")) return 1;
        if (expect(backend.set_frozen_calls == 0 && !backend.frozen,
                   "Invalid-context Z must not change freeze state or call set_frozen.")) return 1;
        if (expect(g_set_screen_title_call_count == 0,
                   "Z warning popup must not trigger set_screen_title.")) return 1;
        mon.poll(0);
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeFreezeControlBackend backend(true);
        const int keys[] = { 'Z', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Z in overlay mode should not exit.")) return 1;
        if (expect(ui.popup_count == 0, "Z in overlay mode must not raise a warning popup.")) return 1;
        if (expect(backend.set_frozen_calls == 1 && backend.frozen,
                   "Z in overlay mode must still perform the freeze toggle.")) return 1;
        mon.poll(0);
        mon.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'U', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        g_set_screen_title_call_count = 0;
        if (expect(mon.poll(0) == 0, "U outside ASM view should not exit.")) return 1;
        if (expect(ui.popup_count == 1, "U outside ASM view must raise exactly one warning popup.")) return 1;
        if (expect(strcmp(ui.last_popup, "UNDOC IN ASM, CASE IN SCR") == 0,
               "U warning must explain the Assembly undocumented-op toggle and the Screen case toggle.")) return 1;
        if (expect(g_set_screen_title_call_count == 0,
                   "U warning popup must not trigger set_screen_title.")) return 1;
        mon.poll(0);
        mon.deinit();
    }

    // Sanity: U in ASM view still toggles silently (no warning popup).
    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'A', 'U', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.popup_count = 0;
        monitor_reset_saved_state();

        BackendMachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "ASM view switch failed in U-in-ASM sanity test.")) return 1;
        if (expect(mon.poll(0) == 0, "U in ASM view should toggle silently.")) return 1;
        if (expect(ui.popup_count == 0, "U inside ASM view must not raise a warning popup.")) return 1;
        mon.poll(0);
        mon.deinit();
    }

    return 0;
}

static int test_restricted_backend_guards_platform_features(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeRestrictedMemoryBackend backend;
    const int keys[] = { 'o', 'O', 'Z', 'G', KEY_BREAK };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    char status[39];
    uint16_t go_addr = 0;

    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C123", 1);
    monitor_reset_saved_state();
    monitor_io::reset_fake_monitor_io();

    BackendMachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);

    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU VIEW") == status && strstr(status, "CPU BANK N/A") != NULL && strstr(status, "VIC N/A") != NULL,
               "Restricted backend status must describe the current CPU-visible map.")) return 1;
    if (expect(backend.set_monitor_cpu_port_calls == 0,
               "Restricted backend init must not receive a fake CPU-bank selection.")) return 1;

    ui.popup_count = 0;
    if (expect(mon.poll(0) == 0, "Restricted CPU-bank shortcut should not exit.")) return 1;
    if (expect(ui.popup_count == 1 && strcmp(ui.last_popup, "CPU BANK UNAVAILABLE") == 0,
               "Restricted CPU-bank shortcut must warn clearly.")) return 1;
    if (expect(backend.set_monitor_cpu_port_calls == 0,
               "Restricted CPU-bank shortcut must leave backend bank state unchanged.")) return 1;

    if (expect(mon.poll(0) == 0, "Restricted VIC-bank shortcut should not exit.")) return 1;
    if (expect(ui.popup_count == 2 && strcmp(ui.last_popup, "VIC BANK UNAVAILABLE") == 0,
               "Restricted VIC-bank shortcut must warn clearly.")) return 1;
    if (expect(backend.set_live_vic_bank_calls == 0,
               "Restricted VIC-bank shortcut must not write DD00.")) return 1;

    if (expect(mon.poll(0) == 0, "Restricted freeze shortcut should not exit.")) return 1;
    if (expect(ui.popup_count == 3 && strcmp(ui.last_popup, "FREEZE UNAVAILABLE") == 0,
               "Restricted freeze shortcut must warn clearly.")) return 1;

    if (expect(mon.poll(0) == 1, "Restricted GO command should exit after queueing a jump.")) return 1;
    if (expect(ui.popup_count == 3,
               "Restricted GO command must not warn when a backend exposes CPU-view execution.")) return 1;
    if (expect(mon.consume_pending_go(&go_addr) && go_addr == 0xC123,
               "Restricted GO command must queue the requested jump address.")) return 1;
    mon.deinit();
    monitor_reset_saved_state();
    return 0;
}

int main()
{
    if (test_disassembler()) return 1;
    if (test_memory_helpers()) return 1;
    if (test_parsers_and_formatters()) return 1;
    if (test_hunt_prompt_typed_input()) return 1;
    if (test_load_save_param_parsers()) return 1;
    if (test_banked_backend()) return 1;
    if (test_frozen_banked_backend()) return 1;
    if (test_kernal_disassembly_mapping()) return 1;
    if (test_disassembly_instruction_stepping()) return 1;
    if (test_disassembly_boundary_cutover()) return 1;
    if (test_disassembly_reverse_cutover_keeps_ffff_at_bottom()) return 1;
    if (test_hex_reverse_wrap_keeps_tail_row_at_bottom()) return 1;
    if (test_last_address_row_highlight()) return 1;
    if (test_template_cursor()) return 1;
    if (test_task_action_lookup()) return 1;
    if (test_monitor_renders_window_border()) return 1;
    if (test_monitor_byte_to_address_invariant()) return 1;
    if (test_monitor_cursor_header_and_scroll()) return 1;
    if (test_monitor_interaction()) return 1;
    if (test_monitor_default_cpu_bank_and_vic_shortcuts()) return 1;
    if (test_monitor_freeze_mode_vic_shortcut_override()) return 1;
    if (test_monitor_reopen_restores_state()) return 1;
    if (test_monitor_kernal_bank_switch_and_ram_interaction()) return 1;
    if (test_assembler_encoding()) return 1;
    if (test_screen_code_reverse()) return 1;
    if (test_screen_charset_toggle_and_header()) return 1;
    if (test_screen_charset_display_and_memory_ascii_pane()) return 1;
    if (test_logical_delete_per_view()) return 1;
    if (test_scr_edit_writes_screen_code()) return 1;
    if (test_number_shortcut_routing()) return 1;
    if (test_range_shortcut_routing()) return 1;
    if (test_asm_number_popup_targets_operands()) return 1;
    if (test_asm_number_popup_illegal_and_invalid_rows()) return 1;
    if (test_asm_edit_assemble_at_cursor()) return 1;
    if (test_asm_edit_direct_typing()) return 1;
    if (test_asm_edit_direct_typing_immediate()) return 1;
    if (test_asm_edit_branch_two_parts()) return 1;
    if (test_asm_cpu_bank_cycle_preserves_screen_row()) return 1;
    if (test_asm_page_up_keeps_screen_row()) return 1;
    if (test_space_shortcuts_match_existing_paging()) return 1;
    if (test_opcode_picker_refilters_live()) return 1;
    if (test_opcode_picker_filters_orders_and_commits_on_enter()) return 1;
    if (test_opcode_picker_browsing_does_not_mutate_frozen_charset_backup()) return 1;
    if (test_opcode_picker_near_bottom_refresh_stays_inside_content()) return 1;
    if (test_opcode_picker_selection_near_bottom_preserves_live_charset_page()) return 1;
    if (test_opcode_picker_pauses_poll_mode()) return 1;
    if (test_cross_view_sync()) return 1;
    if (test_space_edit_behavior_preserved()) return 1;
    if (test_shift_space_does_not_page_in_edit_mode()) return 1;
    if (test_binary_row_formats()) return 1;
    if (test_binary_bit_navigation_and_width()) return 1;
    if (test_memory_row_width_cycle()) return 1;
    if (test_binary_delete_behavior()) return 1;
    if (test_clipboard_number_and_range()) return 1;
    if (test_number_popup_edit_and_commit()) return 1;
    if (test_number_popup_word_commit_and_sticky_row()) return 1;
    if (test_number_popup_placement_and_overlay_redraw()) return 1;
    if (test_fixed_prompt_widths()) return 1;
    if (test_asm_range_copy_paste()) return 1;
    if (test_asm_paste_keeps_viewport_position()) return 1;
    if (test_disassembly_up_keeps_screen_row()) return 1;
    if (test_illegal_mode_header_label()) return 1;
    if (test_hunt_and_compare_picker_navigation()) return 1;
    if (test_prompt_cancel_and_empty_clipboard()) return 1;
    if (test_load_save_and_goto_command_flow()) return 1;
    if (test_hex_single_nibble_commits_on_navigation()) return 1;
    if (test_header_invariants_and_parity()) return 1;
    if (test_poll_mode_refreshes_visible_ram()) return 1;
    if (test_poll_mode_disabled_on_full_refresh_screen()) return 1;
    if (test_edit_indicator_layout_across_views()) return 1;
    if (test_warning_popups_preserve_status_row()) return 1;
    if (test_restricted_backend_guards_platform_features()) return 1;

    puts("machine_monitor_test: OK");
    return 0;
}
