#include <stdio.h>
#include <string.h>

#include "monitor/machine_monitor.h"
#include "monitor/disassembler_6502.h"
#include "menu.h"
#include "screen.h"
#include "task_actions.h"
#include "tasks_collection.h"
#include "ui_elements.h"
#include "userinterface.h"

#define TEST_SORT_ORDER_DEVELOPER 999
#define TEST_SUBSYSID_U64 6

void outbyte(int b)
{
    char c = (char)b;
    fwrite(&c, 1, 1, stdout);
}

ConfigManager :: ConfigManager() : stores(4, NULL), pages(4, NULL), flash(NULL), num_pages(0), safeMode(false)
{
}

ConfigManager :: ~ConfigManager()
{
}

ConfigStore *ConfigManager :: register_store(uint32_t, const char *, t_cfg_definition *, ConfigurableObject *)
{
    return NULL;
}

void ConfigManager :: remove_store(ConfigStore *)
{
}

int ConfigStore :: unregister(ConfigurableObject *)
{
    return 0;
}

UserInterface :: UserInterface(const char *ui_title, bool use_logo) : title(ui_title)
{
    initialized = false;
    doBreak = false;
    available = true;
    status_box = NULL;
    for (int i = 0; i < MAX_UI_OBJECTS; i++) {
        ui_objects[i] = NULL;
    }
    color_border = 0;
    color_bg = 0;
    color_fg = 15;
    color_sel = 0;
    color_sel_bg = 0;
    reverse_sel = 0;
    color_status = 0;
    color_inactive = 0;
    config_save = 0;
    filename_overflow_squeeze = 0;
    navmode = 0;
    logo = use_logo;
    host = NULL;
    keyboard = NULL;
    screen = NULL;
    focus = -1;
    menu_response_to_action = MENU_NOP;
}

UserInterface :: ~UserInterface()
{
}

void UserInterface :: effectuate_settings(void)
{
}

void UserInterface :: release_host()
{
}

bool UserInterface :: is_available(void)
{
    return available;
}

void UserInterface :: run()
{
}

void UserInterface :: run_once()
{
}

void UserInterface :: run_remote()
{
}

int UserInterface :: pollInactive()
{
    return 0;
}

int UserInterface :: keymapper(int c, keymap_options_t map)
{
    if ((navmode == 1) && (map != e_keymap_monitor)) {
        if (c >= 'A' && c <= 'Z') {
            c |= 0x20;
        } else {
            switch (c) {
            case 'w': c = KEY_UP; break;
            case 'a': c = KEY_LEFT; break;
            case 's': c = KEY_DOWN; break;
            case 'd': c = KEY_RIGHT; break;
            }
        }
    }
    switch (c) {
    case KEY_F1: c = KEY_PAGEUP; break;
    case KEY_F3: c = KEY_HELP; break;
    case KEY_F5: c = KEY_TASKS; break;
    case KEY_F7: c = KEY_PAGEDOWN; break;
    case KEY_F2: c = KEY_CONFIG; break;
    case KEY_F4: c = KEY_SYSINFO; break;
    case KEY_F6: c = KEY_SEARCH; break;
    }
    return c;
}

int UserInterface :: popup(const char *, uint8_t)
{
    return 0;
}

int UserInterface :: popup(const char *, int, const char **, const char *)
{
    return 0;
}

int UserInterface :: choice(const char *, const char **, int)
{
    return 0;
}

int UserInterface :: string_box(const char *, char *, int)
{
    return 0;
}

int UserInterface :: string_box(const char *, char *, int, bool)
{
    return 0;
}

int UserInterface :: string_box(const char *, char *, int, bool, bool)
{
    return 0;
}

int UserInterface :: string_edit(char *, int, Window *, int, int)
{
    return 0;
}

void UserInterface :: show_progress(const char *, int)
{
}

void UserInterface :: update_progress(const char *, int)
{
}

void UserInterface :: hide_progress(void)
{
}

void UserInterface :: init(GenericHost *h)
{
    host = h;
}

void UserInterface :: appear(void)
{
}

void UserInterface :: set_screen(Screen *s)
{
    screen = s;
}

void UserInterface :: set_screen_title(void)
{
}

int UserInterface :: activate_uiobject(UIObject *)
{
    return 0;
}

bool UserInterface :: has_focus(UIObject *)
{
    return true;
}

int UserInterface :: getPreferredType(void)
{
    return 0;
}

void UserInterface :: help()
{
}

void UserInterface :: run_editor(const char *, int) { }

void UserInterface :: run_hex_editor(const char *, int)
{
}

void UserInterface :: run_machine_monitor(MemoryBackend *)
{
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

void UserInterface :: swapDisk(void)
{
}

void UserInterface :: send_keystroke(int)
{
}

bool UserInterface :: anyMenuActive(void)
{
    return true;
}

void UserInterface :: postMessage(const char *)
{
}

mstring *UserInterface :: getMessage()
{
    return NULL;
}

struct FakeMemoryBackend : public MemoryBackend
{
    uint8_t memory[65536];
    int session_begin_count;
    int session_end_count;

    FakeMemoryBackend()
    {
        memset(memory, 0, sizeof(memory));
        session_begin_count = 0;
        session_end_count = 0;
    }

    virtual uint8_t read(uint16_t address)
    {
        return memory[address];
    }

    virtual void write(uint16_t address, uint8_t value)
    {
        memory[address] = value;
    }

    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len)
    {
        uint16_t i;
        for (i = 0; i < len; i++) {
            dst[i] = memory[(uint16_t)(address + i)];
        }
    }

    virtual void begin_session(void)
    {
        session_begin_count++;
    }

    virtual void end_session(void)
    {
        session_end_count++;
    }
};

struct FakeBankedMemoryBackend : public MemoryBackend
{
    uint8_t ram[65536];
    uint8_t basic[8192];
    uint8_t kernal[8192];
    uint8_t charrom[4096];
    uint8_t io[4096];
    uint8_t screen_backup[1024]; // Models the freezer's $0400-$07FF backup region.
    uint8_t ram_backup[2048];    // Models the freezer's $0800-$0FFF backup region.
    bool frozen;                 // When true, $0400-$0FFF reads/writes go to the backup buffers.
    uint8_t live_cpu_port;
    uint8_t live_cpu_ddr;
    uint8_t live_dd00;
    int session_begin_count;
    int session_end_count;

    FakeBankedMemoryBackend() : frozen(false), live_cpu_port(0x07), live_cpu_ddr(0x07), live_dd00(0x01)
    {
        memset(ram, 0, sizeof(ram));
        memset(basic, 0, sizeof(basic));
        memset(kernal, 0, sizeof(kernal));
        memset(charrom, 0, sizeof(charrom));
        memset(io, 0, sizeof(io));
        memset(screen_backup, 0, sizeof(screen_backup));
        memset(ram_backup, 0, sizeof(ram_backup));
        session_begin_count = 0;
        session_end_count = 0;
        set_monitor_cpu_port(live_cpu_port);
    }

    virtual uint8_t read(uint16_t address)
    {
        uint8_t cpu_port = get_monitor_cpu_port();

        if (address >= 0xA000 && address <= 0xBFFF) {
            if ((cpu_port & 0x03) == 0x03) {
                return basic[address - 0xA000];
            }
            return ram[address];
        }
        if (address >= 0xD000 && address <= 0xDFFF) {
            if ((cpu_port & 0x03) == 0x00) {
                return ram[address];
            }
            if (cpu_port & 0x04) {
                if (address == 0xDD00) {
                    return live_dd00;
                }
                return io[address - 0xD000];
            }
            return charrom[address - 0xD000];
        }
        if (address >= 0xE000) {
            if (cpu_port & 0x02) {
                return kernal[address - 0xE000];
            }
            return ram[address];
        }
        if (frozen && address >= 0x0400 && address < 0x0800) {
            return screen_backup[address - 0x0400];
        }
        if (frozen && address >= 0x0800 && address < 0x1000) {
            return ram_backup[address - 0x0800];
        }
        if (address == 0x0001) {
            return (ram[address] & 0xF8) | live_cpu_port;
        }
        if (address == 0x0000) {
            return (ram[address] & 0xF8) | live_cpu_ddr;
        }
        return ram[address];
    }

    virtual void write(uint16_t address, uint8_t value)
    {
        uint8_t cpu_port = get_monitor_cpu_port();

        if (address >= 0xD000 && address <= 0xDFFF && (cpu_port & 0x03) != 0x00 && (cpu_port & 0x04)) {
            io[address - 0xD000] = value;
            if (address == 0xDD00) {
                live_dd00 = value;
            }
            return;
        }
        ram[address] = value;
        if (frozen && address >= 0x0400 && address < 0x0800) {
            screen_backup[address - 0x0400] = value;
        } else if (frozen && address >= 0x0800 && address < 0x1000) {
            ram_backup[address - 0x0800] = value;
        }
        if (address == 0x0000) {
            live_cpu_ddr = value & 0x07;
        }
        if (address == 0x0001) {
            live_cpu_port = value & 0x07;
        }
    }

    virtual uint8_t get_live_cpu_port(void)
    {
        return (uint8_t)(((live_cpu_port & live_cpu_ddr) | ((uint8_t)(~live_cpu_ddr) & 0x07)) & 0x07);
    }

    virtual uint8_t get_live_vic_bank(void)
    {
        return (uint8_t)(3 - (live_dd00 & 0x03));
    }

    virtual void begin_session(void)
    {
        session_begin_count++;
    }

    virtual void end_session(void)
    {
        session_end_count++;
    }

    virtual bool supports_freeze(void) const
    {
        return true;
    }

    virtual bool is_frozen(void) const
    {
        return frozen;
    }

    virtual void set_frozen(bool on)
    {
        frozen = on;
    }
};

class FakeKeyboard : public Keyboard
{
    const int *keys;
    int count;
    int index;
public:
    FakeKeyboard(const int *k, int c) : keys(k), count(c), index(0) { }

    int getch(void)
    {
        if (index >= count) {
            return -1;
        }
        return keys[index++];
    }
};

class CaptureScreen : public Screen
{
    int width;
    int height;
    int cursor_x;
    int cursor_y;
    int color;
public:
    char chars[25][40];
    uint8_t colors[25][40];
    bool reverse_chars[25][40];
    bool reverse_mode_on;
    int clear_calls;

    CaptureScreen() : width(40), height(25), cursor_x(0), cursor_y(0), color(0), reverse_mode_on(false), clear_calls(0)
    {
        clear();
    }

    void set_background(int) { }
    void set_color(int c) { color = c; }
    int get_color() { return color; }
    void reverse_mode(int enabled) { reverse_mode_on = enabled != 0; }
    int get_size_x(void) { return width; }
    int get_size_y(void) { return height; }
    void backup(void) { }
    void restore(void) { }
    void clear()
    {
        clear_calls++;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                chars[y][x] = ' ';
                colors[y][x] = 0;
                reverse_chars[y][x] = false;
            }
        }
        cursor_x = 0;
        cursor_y = 0;
        reverse_mode_on = false;
    }

    void move_cursor(int x, int y)
    {
        cursor_x = x;
        cursor_y = y;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= width) cursor_x = width - 1;
        if (cursor_y >= height) cursor_y = height - 1;
    }

    int output(char c)
    {
        if (c == '\n') {
            cursor_x = 0;
            if (cursor_y < height - 1) {
                cursor_y++;
            }
            return 1;
        }
        if (cursor_x < width && cursor_y < height) {
            chars[cursor_y][cursor_x] = c;
            colors[cursor_y][cursor_x] = (uint8_t)color;
            reverse_chars[cursor_y][cursor_x] = reverse_mode_on;
        }
        if (cursor_x < width - 1) {
            cursor_x++;
        }
        return 1;
    }

    int output(const char *string)
    {
        int written = 0;
        while (*string) {
            written += output(*string++);
        }
        return written;
    }

    void repeat(char c, int rep)
    {
        for (int i = 0; i < rep; i++) {
            output(c);
        }
    }

    void output_fixed_length(const char *string, int, int width_to_write)
    {
        for (int i = 0; i < width_to_write; i++) {
            if (string[i]) {
                output(string[i]);
            } else {
                output(' ');
            }
        }
    }

    void get_slice(int x, int y, int len, char *out)
    {
        for (int i = 0; i < len; i++) {
            out[i] = chars[y][x + i];
        }
        out[len] = 0;
    }
};

class CaptureWindow : public Window
{
    int width;
public:
    int last_x;
    int last_y;

    CaptureWindow(Screen *screen, int width) : Window(screen, 0, 0, width, 1), width(width), last_x(-1), last_y(-1) { }

    void move_cursor(int x, int y)
    {
        last_x = x;
        last_y = y;
    }

    void output_length(const char *, int) { }
    void repeat(char, int) { }
    int get_size_x(void)
    {
        return width;
    }
};

class TestUserInterface : public UserInterface
{
public:
    int popup_count;
    char last_popup[128];
    char prompt_texts[8][64];
    int prompt_results[8];
    int prompt_count;
    int prompt_index;

    TestUserInterface() : UserInterface("test", false), popup_count(0), prompt_count(0), prompt_index(0)
    {
        last_popup[0] = 0;
    }

    void set_prompt(const char *text, int result)
    {
        prompt_count = 0;
        prompt_index = 0;
        push_prompt(text, result);
    }

    void push_prompt(const char *text, int result)
    {
        if (prompt_count >= (int)(sizeof(prompt_texts) / sizeof(prompt_texts[0]))) {
            return;
        }
        strncpy(prompt_texts[prompt_count], text ? text : "", sizeof(prompt_texts[prompt_count]) - 1);
        prompt_texts[prompt_count][sizeof(prompt_texts[prompt_count]) - 1] = 0;
        prompt_results[prompt_count] = result;
        prompt_count++;
    }

    int popup(const char *msg, uint8_t)
    {
        popup_count++;
        strncpy(last_popup, msg ? msg : "", sizeof(last_popup) - 1);
        last_popup[sizeof(last_popup) - 1] = 0;
        return BUTTON_OK;
    }

    int string_box(const char *, char *buffer, int maxlen)
    {
        if (prompt_index >= prompt_count) {
            return 0;
        }
        int result = prompt_results[prompt_index];
        if (result > 0) {
            strncpy(buffer, prompt_texts[prompt_index], maxlen);
            buffer[maxlen - 1] = 0;
        }
        prompt_index++;
        return result;
    }

    int string_box(const char *msg, char *buffer, int maxlen, bool)
    {
        return string_box(msg, buffer, maxlen);
    }

    int string_box(const char *msg, char *buffer, int maxlen, bool, bool)
    {
        return string_box(msg, buffer, maxlen);
    }
};

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
        TaskCategory *dev = TasksCollection::getCategory("Developer", TEST_SORT_ORDER_DEVELOPER);
        monitor_action = new Action("Machine Code Monitor", TEST_SUBSYSID_U64, 12);
        dev->append(monitor_action);
    }

    void update_task_items(bool writablePath)
    {
        update_called = true;
        last_writable = writablePath;
    }
};

static int fail(const char *message)
{
    puts(message);
    return 1;
}

static int expect(int condition, const char *message)
{
    if (!condition) {
        return fail(message);
    }
    return 0;
}

static int expect_screens_equal(const CaptureScreen &a, const CaptureScreen &b, const char *message)
{
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 40; x++) {
            if (a.chars[y][x] != b.chars[y][x] ||
                a.reverse_chars[y][x] != b.reverse_chars[y][x]) {
                return fail(message);
            }
        }
    }
    return 0;
}

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

    const uint8_t illegal_zpx[] = { 0x54, 0x44, 0x00 };
    disassemble_6502(0x3000, illegal_zpx, true, &decoded);
    if (expect(strcmp(decoded.text, "NOP $44,X") == 0, "Illegal zeropage,X disassembly failed.")) return 1;

    return 0;
}

static int test_memory_helpers(void)
{
    FakeMemoryBackend backend;
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
    monitor_apply_goto(&state, 0x1235);
    if (expect(state.current_addr == 0x1235 && state.base_addr == 0x1230 && state.disasm_offset == 0, "Goto state alignment failed.")) return 1;

    if (expect(monitor_parse_address("XYZ", &value) == MONITOR_ADDR, "Address validator failure.")) return 1;
    if (expect(monitor_parse_fill("C100-C000,00", &start, &end, &byte) == MONITOR_RANGE, "Range validator failure.")) return 1;
    if (expect(monitor_parse_hunt("C100-C200, ", &start, &end, needle, &needle_len) == MONITOR_SYNTAX, "Hunt validator failure.")) return 1;
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
        if (expect(text_row[28] == ' ' && text_row[29] == ' ',
                   "Blank screen-code range $60-$7F should render as spaces.")) return 1;
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

    MachineMonitor monitor(&ui, &backend);
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

    MachineMonitor monitor(&ui, &backend);
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

    ensure_task_actions_created(true);
    if (expect(provider.update_called && provider.last_writable, "Task action update hook was not called.")) return 1;
    if (expect(provider.monitor_action != NULL, "Task action provider did not create the monitor action.")) return 1;
    if (expect(provider.monitor_action->isPersistent(), "Task action should be made persistent.")) return 1;
    if (expect(find_task_action("Developer", "Machine Code Monitor") == provider.monitor_action, "Task action lookup failed.")) return 1;
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

    MachineMonitor monitor(&ui, &backend);
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

    MachineMonitor monitor(&ui, &backend);
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

    MachineMonitor monitor(&ui, &backend);
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

    MachineMonitor help_monitor(&ui, &banked_backend);
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
    if (expect(strstr(status, "Help (F3/? closes)") == status, "Help header should replace the normal view header.")) return 1;
    {
        static const char *expected_help_lines[] = {
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

        MachineMonitor esc_help_monitor(&ui, &backend);
        esc_help_monitor.init(&screen, &esc_help_keyboard);
        if (expect(esc_help_monitor.poll(0) == 0, "F3 should open help before ESC handling is tested.")) return 1;
        screen.get_slice(1, 4, 38, line);
        if (expect(strncmp(line, "M Memory    I ASCII     V Screen", strlen("M Memory    I ASCII     V Screen")) == 0,
                   "Help must be visible before ESC closes it.")) return 1;
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

    MachineMonitor goto_disasm_monitor(&ui, &banked_backend);
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

        MachineMonitor vic_monitor(&ui, &banked_backend);
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

        MachineMonitor live_bank_monitor(&ui, &banked_backend);
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

    MachineMonitor blink_monitor(&ui, &backend);
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

    MachineMonitor view_monitor(&ui, &backend);
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

    MachineMonitor hex_monitor(&ui, &backend);
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

        MachineMonitor rom_write_monitor(&ui, &banked_backend);
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

        MachineMonitor exit_monitor(&ui, &backend);
        exit_monitor.init(&screen, &exit_keyboard);
        if (expect(exit_monitor.poll(0) == 0, "F3 edit-help setup via 'e' failed.")) return 1;
        if (expect(exit_monitor.poll(0) == 0, "F3 should toggle help while keeping edit mode.")) return 1;
        screen.get_slice(1, 4, 38, line);
        if (expect(strncmp(line, "M Memory    I ASCII     V Screen", strlen("M Memory    I ASCII     V Screen")) == 0,
                   "F3 did not open help while editing.")) return 1;
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

        MachineMonitor nav_monitor(&ui, &backend);
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

        MachineMonitor toggle_monitor(&ui, &backend);
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

    MachineMonitor ignored_monitor(&ui, &backend);
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
        'o', '1',
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

    MachineMonitor monitor(&ui, &backend);
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

    if (expect(monitor.poll(0) == 0, "Digit key should be ignored after plain O.")) return 1;
    screen.get_slice(1, 22, 38, status);
    if (expect(strstr(status, "CPU0 $A:RAM $D:RAM $E:RAM VIC2 $8000") == status,
               "Number keys must not be captured for CPU bank selection.")) return 1;
    if (expect(ui.popup_count == 0, "Digit keys must not trigger CPU bank popups.")) return 1;

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

        MachineMonitor first_monitor(&ui, &backend);
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

        MachineMonitor second_monitor(&ui, &backend);
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

    MachineMonitor monitor(&ui, &backend);
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

#include "monitor/assembler_6502.h"

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
        MachineMonitor mon(&ui, &backend);
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
        MachineMonitor mon(&ui, &backend);
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
        MachineMonitor mon(&ui, &backend);
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
        MachineMonitor mon(&ui, &backend);
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
        MachineMonitor mon(&ui, &backend);
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
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    backend.write(0x0400, 0x00);
    const int keys[] = { 'J', 'V', 'E', 'A', KEY_BREAK };
    FakeKeyboard kb(keys, 5);
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("0400", 1);
    MachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < 5; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0x0400) == 0x01, "SCR edit: typing 'A' must write screen code 0x01")) return 1;
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
    //   DOWN DOWN                    -- LDA opcodes are A1, A5, A9, ... so
    //                                   the third entry is A9 (immediate)
    //   ENTER                        -- write A9 to $C000
    //   4 2                          -- type the operand byte at $C001
    const int keys[] = {
        'J', 'A', 'E', 'L', 'D', 'A', KEY_DOWN, KEY_DOWN, KEY_RETURN, '4', '2', KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    MachineMonitor mon(&ui, &backend);
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
    //   L D A 1 0 0 0  ENTER         -- type the whole instruction
    const int keys[] = {
        'J', 'A', 'E', 'L', 'D', 'A', '1', '0', '0', '0', KEY_RETURN, KEY_BREAK
    };
    FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
    ui.screen = &screen;
    ui.keyboard = &kb;
    ui.set_prompt("C000", 1);
    MachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xAD && backend.read(0xC001) == 0x00 && backend.read(0xC002) == 0x10,
               "ASM direct typing 'LDA 1000' must encode AD 00 10")) return 1;
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
    MachineMonitor mon(&ui, &backend);
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
    MachineMonitor mon(&ui, &backend);
    mon.init(&screen, &kb);
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])) - 1; i++) {
        (void)mon.poll(0);
    }
    if (expect(backend.read(0xC000) == 0xF0 && backend.read(0xC001) == 0x1E,
               "ASM branch edit must rewrite rel offset to land on typed target")) return 1;
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
    MachineMonitor mon(&ui, &backend);
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

        MachineMonitor mon(&ui, &backend);
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
        char row[38];
        const int keys[] = { 'B', 'W', KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("4", 1);
        backend.write(0x0000, 0x80);
        backend.write(0x0001, 0x01);
        backend.write(0x0002, 0xFF);
        backend.write(0x0003, 0x00);
        monitor_reset_saved_state();

        MachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Binary width test: view switch failed.")) return 1;
        screen.get_slice(1, 4, 13, row);
        if (expect(strcmp(row, "0000 *.......") == 0,
                   "Binary default width must be one byte per row.")) return 1;
        if (expect(mon.poll(0) == 0, "Binary width test: width command failed.")) return 1;
        screen.get_slice(1, 4, 37, row);
        if (expect(strcmp(row, "0000 *..............*********........") == 0,
                   "Binary width=4 must render exactly 32 bits per row.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "W=") == NULL && strstr(header, "bits") == NULL,
                   "Binary width must not be displayed in the header.")) return 1;
        if (expect(mon.poll(0) == 1, "Binary width test: exit failed.")) return 1;
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

        MachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Number inspector did not open.")) return 1;
        if (expect(mon.poll(0) == 0, "Number inspector decimal navigation failed.")) return 1;
        if (expect(screen.reverse_chars[5][1],
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

        MachineMonitor mon(&ui, &backend);
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

    MachineMonitor mon(&ui, &backend);
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

    MachineMonitor mon(&ui, &backend);
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

    MachineMonitor mon(&ui, &backend);
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
    if (expect(strstr(line, "UndocOp") != NULL,
               "Enabled undocumented-opcode mode must show the UndocOp label in the header.")) return 1;
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
        const int keys[] = { 'H', KEY_PAGEDOWN, KEY_RETURN, KEY_BREAK };
        FakeKeyboard kb(keys, sizeof(keys) / sizeof(keys[0]));
        for (uint16_t addr = 0xC000; addr <= 0xC013; addr++) {
            backend.write(addr, 0xDE);
        }
        ui.screen = &screen;
        ui.keyboard = &kb;
        ui.set_prompt("C000-C013,DE", 1);
        monitor_reset_saved_state();

        MachineMonitor mon(&ui, &backend);
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

        MachineMonitor mon(&ui, &backend);
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

        MachineMonitor mon(&ui, &backend);
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

        MachineMonitor mon(&ui, &backend);
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

        MachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "LOAD command flow test failed.")) return 1;
        if (expect(monitor_io::g_monitor_io.load_called && monitor_io::g_monitor_io.pick_file_calls == 1 &&
                   strcmp(monitor_io::g_monitor_io.load_path, "/tmp") == 0 &&
                   strcmp(monitor_io::g_monitor_io.load_name, "demo.prg") == 0,
                   "LOAD command must invoke the monitor I/O shim with the picked file.")) return 1;
        if (expect(backend.read(0x0801) == 0xBB && backend.read(0x0802) == 0xCC,
                   "LOAD command must honour PRG header, file offset, and length.")) return 1;
        if (expect(ui.popup_count == 1 && strstr(ui.last_popup, "LOAD demo.prg: $0801-$0802 (2 bytes)") != NULL,
                   "LOAD command must show a confirmation popup with the effective byte range.")) return 1;
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

        MachineMonitor mon(&ui, &backend);
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
        if (expect(ui.popup_count == 1 && strstr(ui.last_popup, "SAVE save.prg: $0801-$0802 (2 bytes)") != NULL,
                   "SAVE command must show a confirmation popup with the saved range.")) return 1;
        if (expect(mon.poll(0) == 1, "SAVE command flow test: exit failed.")) return 1;
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

        MachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 1, "Go command must exit the monitor.")) return 1;
        if (expect(monitor_io::g_monitor_io.jump_called && monitor_io::g_monitor_io.jump_address == 0xC123,
                   "Go command must dispatch the requested jump address.")) return 1;
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

        MachineMonitor mon(&ui, &backend);
        mon.init(&screen, &kb);
        if (expect(mon.poll(0) == 0, "Header test: edit mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: range mode entry failed.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: freeze toggle failed.")) return 1;
        screen.get_slice(1, 3, 38, header);
        if (expect(strstr(header, "Edit Mode") == NULL,
                   "Header must show Edit, not Edit Mode.")) return 1;
        if (expect(strncmp(header + 34, "Edit", 4) == 0,
                   "Edit indicator must be fixed at top-right.")) return 1;
        if (expect(strncmp(header + 20, "Range", 5) == 0,
                   "Range indicator must use its fixed slot.")) return 1;
        if (expect(strncmp(header + 27, "Freeze", 6) == 0,
                   "Freeze indicator must use its fixed slot.")) return 1;
        if (expect(strstr(header, "W=") == NULL,
                   "Header must not display binary width.")) return 1;
        if (expect(mon.poll(0) == 0, "Header test: RUN/STOP should leave edit mode first.")) return 1;
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
        MachineMonitor mon_a(&ui_a, &backend_a);
        mon_a.init(&screen_a, &kb_a);
        for (int i = 0; i < (int)(sizeof(keys_a) / sizeof(keys_a[0])); i++) {
            if (expect(mon_a.poll(0) == 0, "Parity render A command failed.")) return 1;
        }

        monitor_reset_saved_state();
        MachineMonitor mon_b(&ui_b, &backend_b);
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

int main()
{
    if (test_disassembler()) return 1;
    if (test_memory_helpers()) return 1;
    if (test_parsers_and_formatters()) return 1;
    if (test_load_save_param_parsers()) return 1;
    if (test_banked_backend()) return 1;
    if (test_frozen_banked_backend()) return 1;
    if (test_kernal_disassembly_mapping()) return 1;
    if (test_disassembly_instruction_stepping()) return 1;
    if (test_template_cursor()) return 1;
    if (test_task_action_lookup()) return 1;
    if (test_monitor_renders_window_border()) return 1;
    if (test_monitor_byte_to_address_invariant()) return 1;
    if (test_monitor_cursor_header_and_scroll()) return 1;
    if (test_monitor_interaction()) return 1;
    if (test_monitor_default_cpu_bank_and_vic_shortcuts()) return 1;
    if (test_monitor_reopen_restores_state()) return 1;
    if (test_monitor_kernal_bank_switch_and_ram_interaction()) return 1;
    if (test_assembler_encoding()) return 1;
    if (test_screen_code_reverse()) return 1;
    if (test_logical_delete_per_view()) return 1;
    if (test_scr_edit_writes_screen_code()) return 1;
    if (test_asm_edit_assemble_at_cursor()) return 1;
    if (test_asm_edit_direct_typing()) return 1;
    if (test_asm_edit_direct_typing_immediate()) return 1;
    if (test_asm_edit_branch_two_parts()) return 1;
    if (test_cross_view_sync()) return 1;
    if (test_binary_bit_navigation_and_width()) return 1;
    if (test_clipboard_number_and_range()) return 1;
    if (test_asm_range_copy_paste()) return 1;
    if (test_asm_paste_keeps_viewport_position()) return 1;
    if (test_illegal_mode_header_label()) return 1;
    if (test_hunt_and_compare_picker_navigation()) return 1;
    if (test_prompt_cancel_and_empty_clipboard()) return 1;
    if (test_load_save_and_goto_command_flow()) return 1;
    if (test_header_invariants_and_parity()) return 1;

    puts("machine_monitor_test: OK");
    return 0;
}
