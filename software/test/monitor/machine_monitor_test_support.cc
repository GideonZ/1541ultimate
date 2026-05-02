#include "machine_monitor_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"

#ifdef getch
#undef getch
#endif

namespace {

enum {
    FAKE_CONFIG_MAX_STORES = 8,
    FAKE_CONFIG_MAX_ITEMS = 32,
    FAKE_CONFIG_MAX_STRING = 64,
};

struct FakePersistedStore {
    bool used;
    uint32_t page_id;
    char name[64];
    const t_cfg_definition *defs;
    int item_count;
    int values[FAKE_CONFIG_MAX_ITEMS];
    char strings[FAKE_CONFIG_MAX_ITEMS][FAKE_CONFIG_MAX_STRING];
};

struct FakeRuntimeStore {
    ConfigStore *store;
    FakePersistedStore *persisted;
    const t_cfg_definition *defs;
    int item_count;
    int values[FAKE_CONFIG_MAX_ITEMS];
    char strings[FAKE_CONFIG_MAX_ITEMS][FAKE_CONFIG_MAX_STRING];
};

static FakePersistedStore g_fake_persisted_stores[FAKE_CONFIG_MAX_STORES];
static FakeRuntimeStore g_fake_runtime_stores[FAKE_CONFIG_MAX_STORES];
static uint16_t g_fake_ms_timer = 0;

static int fake_cfg_count_items(const t_cfg_definition *defs)
{
    int count = 0;

    while (defs && defs[count].type != CFG_TYPE_END && count < FAKE_CONFIG_MAX_ITEMS) {
        count++;
    }
    return count;
}

static int fake_cfg_find_index(const t_cfg_definition *defs, int item_count, uint8_t id)
{
    for (int i = 0; i < item_count; i++) {
        if (defs[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void fake_cfg_apply_defaults(FakeRuntimeStore *runtime)
{
    if (!runtime) {
        return;
    }
    for (int i = 0; i < runtime->item_count; i++) {
        runtime->values[i] = (int)runtime->defs[i].def;
        runtime->strings[i][0] = 0;
        if (runtime->defs[i].type == CFG_TYPE_STRING ||
            runtime->defs[i].type == CFG_TYPE_STRFUNC ||
            runtime->defs[i].type == CFG_TYPE_STRPASS ||
            runtime->defs[i].type == CFG_TYPE_INFO) {
            const char *def = (const char *)runtime->defs[i].def;
            if (def) {
                strncpy(runtime->strings[i], def, FAKE_CONFIG_MAX_STRING - 1);
                runtime->strings[i][FAKE_CONFIG_MAX_STRING - 1] = 0;
            }
        }
    }
}

static FakePersistedStore *fake_cfg_find_persisted(uint32_t page_id)
{
    for (int i = 0; i < FAKE_CONFIG_MAX_STORES; i++) {
        if (g_fake_persisted_stores[i].used && g_fake_persisted_stores[i].page_id == page_id) {
            return &g_fake_persisted_stores[i];
        }
    }
    return NULL;
}

static FakePersistedStore *fake_cfg_ensure_persisted(uint32_t page_id, const char *name, const t_cfg_definition *defs)
{
    FakePersistedStore *persisted = fake_cfg_find_persisted(page_id);

    if (persisted) {
        return persisted;
    }
    for (int i = 0; i < FAKE_CONFIG_MAX_STORES; i++) {
        if (!g_fake_persisted_stores[i].used) {
            persisted = &g_fake_persisted_stores[i];
            memset(persisted, 0, sizeof(*persisted));
            persisted->used = true;
            persisted->page_id = page_id;
            persisted->defs = defs;
            persisted->item_count = fake_cfg_count_items(defs);
            strncpy(persisted->name, name ? name : "", sizeof(persisted->name) - 1);
            FakeRuntimeStore runtime;
            memset(&runtime, 0, sizeof(runtime));
            runtime.defs = defs;
            runtime.item_count = persisted->item_count;
            fake_cfg_apply_defaults(&runtime);
            for (int item = 0; item < persisted->item_count; item++) {
                persisted->values[item] = runtime.values[item];
                strncpy(persisted->strings[item], runtime.strings[item], FAKE_CONFIG_MAX_STRING - 1);
                persisted->strings[item][FAKE_CONFIG_MAX_STRING - 1] = 0;
            }
            return persisted;
        }
    }
    return NULL;
}

static FakeRuntimeStore *fake_cfg_find_runtime(ConfigStore *store)
{
    for (int i = 0; i < FAKE_CONFIG_MAX_STORES; i++) {
        if (g_fake_runtime_stores[i].store == store) {
            return &g_fake_runtime_stores[i];
        }
    }
    return NULL;
}

static FakeRuntimeStore *fake_cfg_attach_runtime(ConfigStore *store, FakePersistedStore *persisted, const t_cfg_definition *defs)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(store);

    if (runtime) {
        return runtime;
    }
    for (int i = 0; i < FAKE_CONFIG_MAX_STORES; i++) {
        if (!g_fake_runtime_stores[i].store) {
            runtime = &g_fake_runtime_stores[i];
            memset(runtime, 0, sizeof(*runtime));
            runtime->store = store;
            runtime->persisted = persisted;
            runtime->defs = defs;
            runtime->item_count = fake_cfg_count_items(defs);
            if (persisted) {
                for (int item = 0; item < runtime->item_count; item++) {
                    runtime->values[item] = persisted->values[item];
                    strncpy(runtime->strings[item], persisted->strings[item], FAKE_CONFIG_MAX_STRING - 1);
                    runtime->strings[item][FAKE_CONFIG_MAX_STRING - 1] = 0;
                }
            } else {
                fake_cfg_apply_defaults(runtime);
            }
            return runtime;
        }
    }
    return NULL;
}

static void fake_cfg_detach_runtime(ConfigStore *store)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(store);

    if (!runtime) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
}

static void fake_cfg_commit(FakeRuntimeStore *runtime)
{
    if (!runtime || !runtime->persisted) {
        return;
    }
    for (int item = 0; item < runtime->item_count; item++) {
        runtime->persisted->values[item] = runtime->values[item];
        strncpy(runtime->persisted->strings[item], runtime->strings[item], FAKE_CONFIG_MAX_STRING - 1);
        runtime->persisted->strings[item][FAKE_CONFIG_MAX_STRING - 1] = 0;
    }
}

} // namespace

void outbyte(int b)
{
    char c = (char)b;
    fwrite(&c, 1, 1, stdout);
}

extern "C" uint16_t getMsTimer()
{
    return g_fake_ms_timer;
}

extern "C" void wait_ms(int)
{
}

void reset_fake_config_manager_state(void)
{
    IndexedList<ConfigStore *> *stores = ConfigManager::getConfigManager()->getStores();

    while (stores->get_elements() > 0) {
        ConfigStore *store = (*stores)[0];
        stores->remove_idx(0);
        delete store;
    }
    memset(g_fake_persisted_stores, 0, sizeof(g_fake_persisted_stores));
    memset(g_fake_runtime_stores, 0, sizeof(g_fake_runtime_stores));
}

void set_fake_ms_timer(uint16_t value)
{
    g_fake_ms_timer = value;
}

void advance_fake_ms_timer(uint16_t delta)
{
    g_fake_ms_timer = (uint16_t)(g_fake_ms_timer + delta);
}

ConfigManager :: ConfigManager() : stores(4, NULL), pages(4, NULL), flash(NULL), num_pages(0), safeMode(false)
{
}

ConfigManager :: ~ConfigManager()
{
}

ConfigStore *ConfigManager :: register_store(uint32_t page_id, const char *name, t_cfg_definition *defs, ConfigurableObject *ob)
{
    ConfigStore *existing;
    FakePersistedStore *persisted;

    for (int i = 0; i < stores.get_elements(); i++) {
        existing = stores[i];
        if (existing && strcmp(existing->get_store_name(), name) == 0) {
            if (ob) {
                existing->addObject(ob);
            }
            return existing;
        }
    }

    existing = new ConfigStore(NULL, name, defs, ob);
    persisted = fake_cfg_ensure_persisted(page_id, name, defs);
    fake_cfg_attach_runtime(existing, persisted, defs);
    stores.append(existing);
    return existing;
}

ConfigStore *ConfigManager :: find_store(const char *storename)
{
    for (int i = 0; i < stores.get_elements(); i++) {
        ConfigStore *store = stores[i];
        if (store && strcmp(store->get_store_name(), storename) == 0) {
            return store;
        }
    }
    return NULL;
}

ConfigStore *ConfigManager :: find_store(uint32_t)
{
    return NULL;
}

void ConfigManager :: remove_store(ConfigStore *store)
{
    stores.remove(store);
}

ConfigStore :: ConfigStore(ConfigPage *cfg_page, const char *name,
                           t_cfg_definition *, ConfigurableObject *obj)
    : sort_order(0), objects(4, NULL), hook_obj(NULL), store_name(name ? name : ""),
      alt_name(""), page(cfg_page), staleEffect(false), staleFlash(false), hidden(false), items(4, NULL)
{
    if (obj) {
        objects.append(obj);
    }
}

ConfigStore :: ~ConfigStore()
{
    fake_cfg_detach_runtime(this);
}

void ConfigStore :: addObject(ConfigurableObject *obj)
{
    if (obj) {
        objects.append(obj);
    }
}

int ConfigStore :: unregister(ConfigurableObject *obj)
{
    if (obj) {
        objects.remove(obj);
    }
    return objects.get_elements();
}

void ConfigStore :: reset(void)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);

    if (!runtime) {
        return;
    }
    fake_cfg_apply_defaults(runtime);
    fake_cfg_commit(runtime);
}

void ConfigStore :: read(bool)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);

    if (!runtime) {
        return;
    }
    if (runtime->persisted) {
        for (int item = 0; item < runtime->item_count; item++) {
            runtime->values[item] = runtime->persisted->values[item];
            strncpy(runtime->strings[item], runtime->persisted->strings[item], FAKE_CONFIG_MAX_STRING - 1);
            runtime->strings[item][FAKE_CONFIG_MAX_STRING - 1] = 0;
        }
    }
}

void ConfigStore :: write(void)
{
    fake_cfg_commit(fake_cfg_find_runtime(this));
}

void ConfigStore :: at_open_config(void)
{
}

void ConfigStore :: effectuate(void)
{
}

ConfigItem *ConfigStore :: find_item(uint8_t)
{
    return NULL;
}

ConfigItem *ConfigStore :: find_item(const char *)
{
    return NULL;
}

int ConfigStore :: get_value(uint8_t id)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);
    int index;

    if (!runtime) {
        return 0;
    }
    index = fake_cfg_find_index(runtime->defs, runtime->item_count, id);
    return (index >= 0) ? runtime->values[index] : 0;
}

const char *ConfigStore :: get_string(uint8_t id)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);
    int index;

    if (!runtime) {
        return "";
    }
    index = fake_cfg_find_index(runtime->defs, runtime->item_count, id);
    return (index >= 0) ? runtime->strings[index] : "";
}

void ConfigStore :: set_value(uint8_t id, int value)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);
    int index;

    if (!runtime) {
        return;
    }
    index = fake_cfg_find_index(runtime->defs, runtime->item_count, id);
    if (index >= 0) {
        runtime->values[index] = value;
    }
}

void ConfigStore :: set_string(uint8_t id, const char *text)
{
    FakeRuntimeStore *runtime = fake_cfg_find_runtime(this);
    int index;

    if (!runtime) {
        return;
    }
    index = fake_cfg_find_index(runtime->defs, runtime->item_count, id);
    if (index >= 0) {
        strncpy(runtime->strings[index], text ? text : "", FAKE_CONFIG_MAX_STRING - 1);
        runtime->strings[index][FAKE_CONFIG_MAX_STRING - 1] = 0;
    }
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

int g_set_screen_title_call_count = 0;

void UserInterface :: set_screen_title(void)
{
    g_set_screen_title_call_count++;
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

void UserInterface :: run_editor(const char *, int)
{
}

void UserInterface :: run_hex_editor(const char *, int)
{
}

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

FakeMemoryBackend :: FakeMemoryBackend()
{
    memset(memory, 0, sizeof(memory));
    session_begin_count = 0;
    session_end_count = 0;
}

uint8_t FakeMemoryBackend :: read(uint16_t address)
{
    return memory[address];
}

void FakeMemoryBackend :: write(uint16_t address, uint8_t value)
{
    memory[address] = value;
}

void FakeMemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = memory[(uint16_t)(address + i)];
    }
}

void FakeMemoryBackend :: begin_session(void)
{
    session_begin_count++;
}

void FakeMemoryBackend :: end_session(void)
{
    session_end_count++;
}

FakeBankedMemoryBackend :: FakeBankedMemoryBackend() : frozen(false), live_cpu_port(0x07), live_cpu_ddr(0x07), live_dd00(0x01)
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

uint8_t FakeBankedMemoryBackend :: read(uint16_t address)
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

void FakeBankedMemoryBackend :: write(uint16_t address, uint8_t value)
{
    uint8_t cpu_port = get_monitor_cpu_port();

    if (address >= 0xD000 && address <= 0xDFFF && (cpu_port & 0x03) != 0x00 && (cpu_port & 0x04)) {
        io[address - 0xD000] = value;
        if (address == 0xDD00) {
            live_dd00 = value;
        }
        return;
    }
    if (frozen && address >= 0x0400 && address < 0x0800) {
        screen_backup[address - 0x0400] = value;
        return;
    }
    if (frozen && address >= 0x0800 && address < 0x1000) {
        ram_backup[address - 0x0800] = value;
        return;
    }
    ram[address] = value;
    if (address == 0x0000) {
        live_cpu_ddr = value & 0x07;
    }
    if (address == 0x0001) {
        live_cpu_port = value & 0x07;
    }
}

uint8_t FakeBankedMemoryBackend :: get_live_cpu_port(void)
{
    return (uint8_t)(((live_cpu_port & live_cpu_ddr) | ((uint8_t)(~live_cpu_ddr) & 0x07)) & 0x07);
}

uint8_t FakeBankedMemoryBackend :: get_live_vic_bank(void)
{
    return (uint8_t)(3 - (live_dd00 & 0x03));
}

void FakeBankedMemoryBackend :: begin_session(void)
{
    session_begin_count++;
}

void FakeBankedMemoryBackend :: end_session(void)
{
    session_end_count++;
}

bool FakeBankedMemoryBackend :: supports_freeze(void) const
{
    return true;
}

bool FakeBankedMemoryBackend :: is_frozen(void) const
{
    return frozen;
}

void FakeBankedMemoryBackend :: set_frozen(bool on)
{
    frozen = on;
}

FakeFreezeControlBackend :: FakeFreezeControlBackend(bool available_now, bool frozen_now)
    : available(available_now), frozen(frozen_now), set_frozen_calls(0)
{
}

bool FakeFreezeControlBackend :: supports_freeze(void) const
{
    return true;
}

bool FakeFreezeControlBackend :: freeze_available(void) const
{
    return available;
}

bool FakeFreezeControlBackend :: is_frozen(void) const
{
    return frozen;
}

void FakeFreezeControlBackend :: set_frozen(bool on)
{
    set_frozen_calls++;
    frozen = on;
}

FakeFrozenVicBackend :: FakeFrozenVicBackend() : reported_vic_bank(0), requested_vic_bank(0), set_live_vic_bank_calls(0)
{
    frozen = true;
}

uint8_t FakeFrozenVicBackend :: get_live_vic_bank(void)
{
    return reported_vic_bank & 0x03;
}

void FakeFrozenVicBackend :: set_live_vic_bank(uint8_t vic_bank)
{
    requested_vic_bank = (uint8_t)(vic_bank & 0x03);
    set_live_vic_bank_calls++;
}

FakeRestrictedMemoryBackend :: FakeRestrictedMemoryBackend() : set_monitor_cpu_port_calls(0), set_live_vic_bank_calls(0)
{
}

void FakeRestrictedMemoryBackend :: set_monitor_cpu_port(uint8_t)
{
    set_monitor_cpu_port_calls++;
}

bool FakeRestrictedMemoryBackend :: supports_cpu_banking(void) const
{
    return false;
}

bool FakeRestrictedMemoryBackend :: supports_vic_bank(void) const
{
    return false;
}

void FakeRestrictedMemoryBackend :: set_live_vic_bank(uint8_t)
{
    set_live_vic_bank_calls++;
}

const char *FakeRestrictedMemoryBackend :: source_name(uint16_t) const
{
    return "CPU";
}

FakeKeyboard :: FakeKeyboard(const int *k, int c) : keys(k), count(c), index(0)
{
}

int FakeKeyboard :: getch(void)
{
    if (index >= count) {
        return -1;
    }
    return keys[index++];
}

CaptureScreen :: CaptureScreen() : width(40), height(25), cursor_x(0), cursor_y(0), color(0), reverse_mode_on(false), clear_calls(0)
{
    clear();
}

void CaptureScreen :: set_background(int)
{
}

void CaptureScreen :: set_color(int c)
{
    color = c;
}

int CaptureScreen :: get_color()
{
    return color;
}

void CaptureScreen :: reverse_mode(int enabled)
{
    reverse_mode_on = enabled != 0;
}

int CaptureScreen :: get_size_x(void)
{
    return width;
}

int CaptureScreen :: get_size_y(void)
{
    return height;
}

void CaptureScreen :: backup(void)
{
}

void CaptureScreen :: restore(void)
{
}

void CaptureScreen :: clear()
{
    clear_calls++;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            chars[y][x] = ' ';
            colors[y][x] = 0;
            reverse_chars[y][x] = false;
            write_counts[y][x] = 0;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    reverse_mode_on = false;
}

void CaptureScreen :: move_cursor(int x, int y)
{
    cursor_x = x;
    cursor_y = y;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= width) cursor_x = width - 1;
    if (cursor_y >= height) cursor_y = height - 1;
}

int CaptureScreen :: output(char c)
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
        write_counts[cursor_y][cursor_x]++;
    }
    if (cursor_x < width - 1) {
        cursor_x++;
    }
    return 1;
}

int CaptureScreen :: output(const char *string)
{
    int written = 0;
    while (*string) {
        written += output(*string++);
    }
    return written;
}

void CaptureScreen :: repeat(char c, int rep)
{
    for (int i = 0; i < rep; i++) {
        output(c);
    }
}

void CaptureScreen :: output_fixed_length(const char *string, int, int width_to_write)
{
    for (int i = 0; i < width_to_write; i++) {
        if (string[i]) {
            output(string[i]);
        } else {
            output(' ');
        }
    }
}

void CaptureScreen :: get_slice(int x, int y, int len, char *out) const
{
    for (int i = 0; i < len; i++) {
        out[i] = chars[y][x + i];
    }
    out[len] = 0;
}

void CaptureScreen :: reset_write_counts()
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            write_counts[y][x] = 0;
        }
    }
}

int CaptureScreen :: count_writes_outside_rect(int left, int top, int right, int bottom) const
{
    int writes = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= left && x <= right && y >= top && y <= bottom) {
                continue;
            }
            writes += write_counts[y][x];
        }
    }
    return writes;
}

CaptureWindow :: CaptureWindow(Screen *screen, int window_width)
    : Window(screen, 0, 0, window_width, 1), width(window_width), last_x(-1), last_y(-1)
{
}

void CaptureWindow :: move_cursor(int x, int y)
{
    last_x = x;
    last_y = y;
}

void CaptureWindow :: output_length(const char *, int)
{
}

void CaptureWindow :: repeat(char, int)
{
}

int CaptureWindow :: get_size_x(void)
{
    return width;
}

TestUserInterface :: TestUserInterface()
    : UserInterface("test", false), popup_count(0), last_prompt_maxlen(0), prompt_count(0), prompt_index(0)
{
    last_popup[0] = 0;
    last_prompt_message[0] = 0;
}

void TestUserInterface :: set_prompt(const char *text, int result)
{
    prompt_count = 0;
    prompt_index = 0;
    push_prompt(text, result);
}

void TestUserInterface :: push_prompt(const char *text, int result)
{
    if (prompt_count >= (int)(sizeof(prompt_texts) / sizeof(prompt_texts[0]))) {
        return;
    }
    strncpy(prompt_texts[prompt_count], text ? text : "", sizeof(prompt_texts[prompt_count]) - 1);
    prompt_texts[prompt_count][sizeof(prompt_texts[prompt_count]) - 1] = 0;
    prompt_results[prompt_count] = result;
    prompt_count++;
}

int TestUserInterface :: popup(const char *msg, uint8_t)
{
    popup_count++;
    strncpy(last_popup, msg ? msg : "", sizeof(last_popup) - 1);
    last_popup[sizeof(last_popup) - 1] = 0;
    return BUTTON_OK;
}

int TestUserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
    strncpy(last_prompt_message, msg ? msg : "", sizeof(last_prompt_message) - 1);
    last_prompt_message[sizeof(last_prompt_message) - 1] = 0;
    last_prompt_maxlen = maxlen;
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

int TestUserInterface :: string_box(const char *msg, char *buffer, int maxlen, bool)
{
    return string_box(msg, buffer, maxlen);
}

int TestUserInterface :: string_box(const char *msg, char *buffer, int maxlen, bool, bool)
{
    return string_box(msg, buffer, maxlen);
}

int fail(const char *message)
{
    puts(message);
    return 1;
}

int expect(int condition, const char *message)
{
    if (!condition) {
        return fail(message);
    }
    return 0;
}

int popup_longest_line(const char *msg)
{
    int longest = 0;
    int current = 0;

    while (msg && *msg) {
        if (*msg == '\n') {
            if (current > longest) longest = current;
            current = 0;
        } else if (*msg != '\r') {
            current++;
        }
        msg++;
    }
    if (current > longest) longest = current;
    return longest;
}

int expect_screens_equal(const CaptureScreen &a, const CaptureScreen &b, const char *message)
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

bool find_popup_rect(const CaptureScreen &screen, int *left, int *top, int *right, int *bottom)
{
    int best_left = -1;
    int best_top = -1;
    int best_right = -1;
    int best_bottom = -1;
    int best_area = 0;

    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 40; x++) {
            if ((unsigned char)screen.chars[y][x] != BORD_LOWER_RIGHT_CORNER) {
                continue;
            }
            for (int xr = x + 2; xr < 40; xr++) {
                if ((unsigned char)screen.chars[y][xr] != BORD_LOWER_LEFT_CORNER) {
                    continue;
                }
                for (int yb = y + 2; yb < 25; yb++) {
                    int width = xr - x + 1;
                    int height = yb - y + 1;
                    if (width > 40 || height >= 23) {
                        continue;
                    }
                    // Skip the surrounding monitor frame: it spans nearly the
                    // full screen. The bookmark popup is shorter.
                    if (width >= 38 && height >= 22) {
                        continue;
                    }
                    if ((unsigned char)screen.chars[yb][x] == BORD_UPPER_RIGHT_CORNER &&
                        (unsigned char)screen.chars[yb][xr] == BORD_UPPER_LEFT_CORNER) {
                        int area = width * height;
                        if (best_area == 0 || area < best_area) {
                            best_area = area;
                            best_left = x;
                            best_top = y;
                            best_right = xr;
                            best_bottom = yb;
                        }
                    }
                }
            }
        }
    }
    if (best_area == 0) {
        return false;
    }
    if (left) *left = best_left;
    if (top) *top = best_top;
    if (right) *right = best_right;
    if (bottom) *bottom = best_bottom;
    return true;
}

int find_highlighted_cell(const CaptureScreen &screen, int *x, int *y)
{
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < 40; col++) {
            if (screen.reverse_chars[row][col]) {
                if (x) *x = col;
                if (y) *y = row;
                return 1;
            }
        }
    }
    return 0;
}

void get_popup_line(const CaptureScreen &screen, int row, char *out, int out_len)
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int width;

    if (!find_popup_rect(screen, &left, &top, &right, &bottom)) {
        if (out_len > 0) {
            out[0] = 0;
        }
        return;
    }

    width = right - left - 1;
    if (width < 0) {
        width = 0;
    }
    if (width > out_len - 1) {
        width = out_len - 1;
    }
    screen.get_slice(left + 1, top + 1 + row, width, out);
}

int popup_corner_diagonal_distance(int popup_left, int popup_top, int popup_right, int popup_bottom,
                                   int cursor_x, int cursor_y)
{
    int distances[4];
    int best;

    distances[0] = abs(popup_left - cursor_x) + abs(popup_top - cursor_y);
    distances[1] = abs(popup_right - cursor_x) + abs(popup_top - cursor_y);
    distances[2] = abs(popup_left - cursor_x) + abs(popup_bottom - cursor_y);
    distances[3] = abs(popup_right - cursor_x) + abs(popup_bottom - cursor_y);
    best = distances[0];
    for (int i = 1; i < 4; i++) {
        if (distances[i] < best) {
            best = distances[i];
        }
    }
    return best;
}