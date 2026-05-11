#include "monitor_bookmarks.h"
#include "config.h"

extern "C" int sprintf(char *out, const char *fmt, ...);

namespace {

enum {
    MONITOR_BOOKMARKS_STORE_ID = 0x4D424D4B,
    MONITOR_BOOKMARKS_SCHEMA_VERSION = 1,
    MONITOR_BOOKMARK_CFG_SCHEMA = 1,
    MONITOR_BOOKMARK_CFG_SLOT0,
    MONITOR_BOOKMARK_CFG_SLOT1,
    MONITOR_BOOKMARK_CFG_SLOT2,
    MONITOR_BOOKMARK_CFG_SLOT3,
    MONITOR_BOOKMARK_CFG_SLOT4,
    MONITOR_BOOKMARK_CFG_SLOT5,
    MONITOR_BOOKMARK_CFG_SLOT6,
    MONITOR_BOOKMARK_CFG_SLOT7,
    MONITOR_BOOKMARK_CFG_SLOT8,
    MONITOR_BOOKMARK_CFG_SLOT9,
};

struct MonitorBookmarkDefault {
    uint16_t address;
    uint8_t view;
    const char *label;
};

static const MonitorBookmarkDefault kMonitorBookmarkDefaults[MONITOR_BOOKMARK_SLOT_COUNT] = {
    { 0x0000, MONITOR_BOOKMARK_VIEW_HEX,    "ZP" },
    { 0x0400, MONITOR_BOOKMARK_VIEW_SCREEN, "SCREEN" },
    { 0x0801, MONITOR_BOOKMARK_VIEW_ASM,    "BASIC" },
    { 0xA000, MONITOR_BOOKMARK_VIEW_ASM,    "BASROM" },
    { 0xC000, MONITOR_BOOKMARK_VIEW_ASM,    "HIRAM" },
    { 0xD000, MONITOR_BOOKMARK_VIEW_HEX,    "VIC" },
    { 0xD400, MONITOR_BOOKMARK_VIEW_HEX,    "SID" },
    { 0xDC00, MONITOR_BOOKMARK_VIEW_BINARY, "CIA1" },
    { 0xDD00, MONITOR_BOOKMARK_VIEW_BINARY, "CIA2" },
    { 0xE000, MONITOR_BOOKMARK_VIEW_ASM,    "KERNAL" },
};

static const t_cfg_definition monitor_bookmark_cfg[] = {
    { MONITOR_BOOKMARK_CFG_SCHEMA, CFG_TYPE_VALUE, "Schema", "%d", NULL, 0, MONITOR_BOOKMARKS_SCHEMA_VERSION, 0 },
    { MONITOR_BOOKMARK_CFG_SLOT0, CFG_TYPE_STRING, "BM0", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT1, CFG_TYPE_STRING, "BM1", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT2, CFG_TYPE_STRING, "BM2", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT3, CFG_TYPE_STRING, "BM3", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT4, CFG_TYPE_STRING, "BM4", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT5, CFG_TYPE_STRING, "BM5", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT6, CFG_TYPE_STRING, "BM6", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT7, CFG_TYPE_STRING, "BM7", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT8, CFG_TYPE_STRING, "BM8", "%s", NULL, 0, 47, (long)"" },
    { MONITOR_BOOKMARK_CFG_SLOT9, CFG_TYPE_STRING, "BM9", "%s", NULL, 0, 47, (long)"" },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 },
};

static uint8_t monitor_bookmark_cfg_id_for_slot(uint8_t slot)
{
    return (uint8_t)(MONITOR_BOOKMARK_CFG_SLOT0 + slot);
}

static void monitor_bookmark_copy(char *out, size_t out_len, const char *text)
{
    size_t index = 0;

    if (!out || !out_len) {
        return;
    }
    if (!text) {
        out[0] = 0;
        return;
    }
    while (text[index] && index + 1 < out_len) {
        out[index] = text[index];
        index++;
    }
    out[index] = 0;
}

static char monitor_bookmark_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if ((c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' || c == '-') {
        return c;
    }
    return '_';
}

static uint8_t monitor_bookmark_normalize_binary_width(uint8_t width)
{
    // Preserve sprite mode marker
    if (width == MONITOR_BOOKMARK_BINARY_WIDTH_SPRITE) return MONITOR_BOOKMARK_BINARY_WIDTH_SPRITE;
    if (width < MONITOR_BOOKMARK_BINARY_WIDTH_MIN) return MONITOR_BOOKMARK_BINARY_WIDTH_MIN;
    if (width > MONITOR_BOOKMARK_BINARY_WIDTH_MAX) return MONITOR_BOOKMARK_BINARY_WIDTH_MAX;
    return width;
}

static bool monitor_bookmark_view_has_width(uint8_t view)
{
    return view != MONITOR_BOOKMARK_VIEW_ASM;
}

static void monitor_bookmark_format_width2(char *out, size_t out_size, uint8_t view, uint8_t width)
{
    uint8_t normalized;

    if (!out || out_size == 0) {
        return;
    }
    if (out_size < 3) {
        out[0] = 0;
        return;
    }
    if (!monitor_bookmark_view_has_width(view)) {
        out[0] = ' ';
        out[1] = ' ';
        out[2] = 0;
        return;
    }

    normalized = monitor_bookmark_normalize_width(view, width);
    if (view == MONITOR_BOOKMARK_VIEW_BINARY && normalized == MONITOR_BOOKMARK_BINARY_WIDTH_SPRITE) {
        out[0] = '3';
        out[1] = 'S';
        out[2] = 0;
        return;
    }
    if (normalized >= 10) {
        sprintf(out, "%u", (unsigned)normalized);
    } else {
        out[0] = ' ';
        out[1] = (char)('0' + normalized);
        out[2] = 0;
    }
}

static void monitor_bookmark_format_status_width(char *out, size_t out_size, uint8_t view, uint8_t width)
{
    uint8_t normalized;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = 0;
    if (!monitor_bookmark_view_has_width(view)) {
        return;
    }

    normalized = monitor_bookmark_normalize_width(view, width);
    if (view == MONITOR_BOOKMARK_VIEW_BINARY && normalized == MONITOR_BOOKMARK_BINARY_WIDTH_SPRITE) {
        monitor_bookmark_copy(out, out_size, "W3S");
        return;
    }
    sprintf(out, "W%u", (unsigned)normalized);
}

static void monitor_bookmark_clear(MonitorBookmarkSlot *slot)
{
    if (!slot) {
        return;
    }
    slot->address = 0;
    slot->view = MONITOR_BOOKMARK_VIEW_HEX;
    slot->cpu_bank = 0;
    slot->vic_bank = 0;
    slot->view_width_mode = MONITOR_BOOKMARK_WIDTH_NONE;
    slot->edit_mode = false;
    slot->is_default = false;
    slot->is_valid = false;
    slot->label[0] = 0;
}

static int monitor_bookmark_parse_uint(const char *text, int *value)
{
    int parsed = 0;

    if (!text || !*text || !value) {
        return 0;
    }
    while (*text) {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        parsed = parsed * 10 + (*text - '0');
        text++;
    }
    *value = parsed;
    return 1;
}

static int monitor_bookmark_parse_slot(const char *text, MonitorBookmarkSlot *slot)
{
    char buffer[64];
    char *field;
    char *cursor;
    char *next;
    int field_index = 0;
    int values[7] = { 0, 0, 0, 0, 0, 0, 0 };
    const char *label_field = "";

    if (!text || !*text || !slot) {
        return 0;
    }

    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    cursor = buffer;
    while (cursor && *cursor) {
        next = strchr(cursor, ',');
        field = cursor;
        if (next) {
            *next = 0;
            cursor = next + 1;
        } else {
            cursor = NULL;
        }
        if (field_index < 7) {
            if (!monitor_bookmark_parse_uint(field, &values[field_index])) {
                return 0;
            }
        } else if (field_index == 7) {
            label_field = field;
        } else {
            return 0;
        }
        field_index++;
    }
    if (field_index != 8) {
        return 0;
    }

    slot->address = (uint16_t)values[0];
    slot->view = (uint8_t)values[1];
    slot->cpu_bank = (uint8_t)values[2];
    slot->vic_bank = (uint8_t)values[3];
    slot->edit_mode = false;
    slot->is_default = values[5] ? true : false;
    slot->view_width_mode = monitor_bookmark_normalize_width(slot->view, (uint8_t)values[6]);
    monitor_bookmark_normalize_label(slot->label, sizeof(slot->label), label_field);
    slot->is_valid = true;
    return monitor_bookmark_slot_is_valid(*slot) ? 1 : 0;
}

static void monitor_bookmark_encode_slot(char *out, size_t out_len, const MonitorBookmarkSlot &slot)
{
    char buffer[48];
    char label_buffer[MONITOR_BOOKMARK_LABEL_STORAGE];

    if (!out || !out_len) {
        return;
    }
    if (!slot.is_valid) {
        out[0] = 0;
        return;
    }
    monitor_bookmark_normalize_label(label_buffer, sizeof(label_buffer), slot.label);
    sprintf(buffer, "%u,%u,%u,%u,%u,%u,%u,%s",
            (unsigned)slot.address,
            (unsigned)slot.view,
            (unsigned)slot.cpu_bank,
            (unsigned)slot.vic_bank,
            0U,
            slot.is_default ? 1U : 0U,
            (unsigned)monitor_bookmark_normalize_width(slot.view, slot.view_width_mode),
            label_buffer);
    monitor_bookmark_copy(out, out_len, buffer);
}

static void monitor_bookmark_assign_default(MonitorBookmarkSlot *slot, uint8_t index,
                                            uint8_t default_cpu_bank, uint8_t default_vic_bank)
{
    (void)default_vic_bank;
    slot->address = kMonitorBookmarkDefaults[index].address;
    slot->view = kMonitorBookmarkDefaults[index].view;
    slot->cpu_bank = (uint8_t)(default_cpu_bank & 0x07);
    slot->vic_bank = 0;
    slot->view_width_mode = monitor_bookmark_normalize_width(slot->view, MONITOR_BOOKMARK_WIDTH_NONE);
    slot->edit_mode = false;
    slot->is_default = true;
    slot->is_valid = true;
    monitor_bookmark_normalize_label(slot->label, sizeof(slot->label),
                                     kMonitorBookmarkDefaults[index].label);
}

} // namespace

uint8_t monitor_bookmark_normalize_width(uint8_t view, uint8_t width)
{
    switch (view) {
        case MONITOR_BOOKMARK_VIEW_HEX:
            return (width >= MONITOR_BOOKMARK_WIDTH_MEMORY_16) ?
                MONITOR_BOOKMARK_WIDTH_MEMORY_16 : MONITOR_BOOKMARK_WIDTH_MEMORY_8;
        case MONITOR_BOOKMARK_VIEW_ASCII:
        case MONITOR_BOOKMARK_VIEW_SCREEN:
            return MONITOR_BOOKMARK_WIDTH_TEXT;
        case MONITOR_BOOKMARK_VIEW_BINARY:
            return monitor_bookmark_normalize_binary_width(width);
        case MONITOR_BOOKMARK_VIEW_ASM:
        default:
            return MONITOR_BOOKMARK_WIDTH_NONE;
    }
}

const char *monitor_bookmark_default_label(uint8_t slot)
{
    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT) {
        return "";
    }
    return kMonitorBookmarkDefaults[slot].label;
}

const char *monitor_bookmark_view_name(uint8_t view)
{
    switch (view) {
        case MONITOR_BOOKMARK_VIEW_ASM: return "ASM";
        case MONITOR_BOOKMARK_VIEW_ASCII: return "ASC";
        case MONITOR_BOOKMARK_VIEW_SCREEN: return "SCR";
        case MONITOR_BOOKMARK_VIEW_BINARY: return "BIN";
        default: return "HEX";
    }
}

bool monitor_bookmark_slot_is_valid(const MonitorBookmarkSlot &slot)
{
    if (!slot.is_valid) {
        return false;
    }
    if (slot.view > MONITOR_BOOKMARK_VIEW_MAX) {
        return false;
    }
    if (slot.cpu_bank > 7) {
        return false;
    }
    if (slot.vic_bank > 3) {
        return false;
    }
    return true;
}

bool monitor_bookmark_status_uses_emphasis(MonitorBookmarkStatusKind kind, const MonitorBookmarkSlot *slot)
{
    if (kind == MONITOR_BOOKMARK_STATUS_RESTORED) {
        return !slot || slot->edit_mode;
    }
    return true;
}

void monitor_bookmark_normalize_label(char *out, size_t out_size, const char *input)
{
    size_t written = 0;

    if (!out || out_size == 0) {
        return;
    }
    if (out_size < 2) {
        out[0] = 0;
        return;
    }

    if (input) {
        for (size_t i = 0; input[i] && written < MONITOR_BOOKMARK_LABEL_MAX &&
                          (written + 1) < out_size; i++) {
            char c = input[i];
            if (c == ' ' || c == '\t') {
                continue;
            }
            out[written++] = monitor_bookmark_normalize_char(c);
        }
    }
    out[written] = 0;

    if (written == 0) {
        const char *fallback = "USER";
        for (size_t i = 0; fallback[i] && (i + 1) < out_size; i++) {
            out[i] = fallback[i];
        }
        out[4] = 0;
    }
}

void monitor_bookmark_format_label6(char *out, size_t out_size, const char *label)
{
    size_t i = 0;
    bool ended = (label == NULL);

    if (!out || out_size == 0) {
        return;
    }
    if (out_size < MONITOR_BOOKMARK_LABEL_MAX + 1) {
        out[0] = 0;
        return;
    }
    while (i < MONITOR_BOOKMARK_LABEL_MAX) {
        if (!ended && label[i] == 0) {
            ended = true;
        }
        out[i] = ended ? ' ' : label[i];
        i++;
    }
    out[MONITOR_BOOKMARK_LABEL_MAX] = 0;
}

void monitor_bookmark_format_view6(char *out, size_t out_size, uint8_t view, uint8_t view_width_mode)
{
    char buffer[MONITOR_BOOKMARK_VIEW_DISPLAY_MAX + 2];
    char width2[3];

    if (!out || out_size == 0) {
        return;
    }
    if (out_size < 7) {
        out[0] = 0;
        return;
    }
    monitor_bookmark_format_width2(width2, sizeof(width2), view, view_width_mode);
    if (!monitor_bookmark_view_has_width(view)) {
        const char *name = monitor_bookmark_view_name(view);
        size_t i = 0;
        while (name[i] && i < 6) {
            out[i] = name[i];
            i++;
        }
        while (i < 6) {
            out[i++] = ' ';
        }
        out[6] = 0;
        return;
    }
    sprintf(buffer, "%s %s", monitor_bookmark_view_name(view), width2);
    monitor_bookmark_copy(out, out_size, buffer);
}

void monitor_bookmark_format_status(char *out, size_t out_len, uint8_t slot,
                                    const MonitorBookmarkSlot *bookmark,
                                    MonitorBookmarkStatusKind kind)
{
    char buffer[64];
    char label_buffer[MONITOR_BOOKMARK_LABEL_STORAGE];
    char width_token[4];

    if (!out || !out_len) {
        return;
    }
    out[0] = 0;
    label_buffer[0] = 0;
    width_token[0] = 0;
    if (bookmark) {
        monitor_bookmark_normalize_label(label_buffer, sizeof(label_buffer), bookmark->label);
        monitor_bookmark_format_status_width(width_token, sizeof(width_token),
                                            bookmark->view, bookmark->view_width_mode);
    }

    switch (kind) {
        case MONITOR_BOOKMARK_STATUS_EMPTY:
            sprintf(buffer, "BM%u EMPTY", (unsigned)slot);
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        case MONITOR_BOOKMARK_STATUS_SAVE_FAILED:
            if (bookmark && label_buffer[0]) {
                sprintf(buffer, "BM%u %s SAVE FAILED", (unsigned)slot, label_buffer);
            } else {
                sprintf(buffer, "BM%u SAVE FAILED", (unsigned)slot);
            }
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        case MONITOR_BOOKMARK_STATUS_RESTORE_FAILED:
            if (bookmark && label_buffer[0]) {
                sprintf(buffer, "BM%u %s RESTORE FAILED", (unsigned)slot, label_buffer);
            } else {
                sprintf(buffer, "BM%u RESTORE FAILED", (unsigned)slot);
            }
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        case MONITOR_BOOKMARK_STATUS_RESET_DEFAULTS:
            monitor_bookmark_copy(out, out_len, "BOOKMARKS RESET TO DEFAULTS");
            return;
        case MONITOR_BOOKMARK_STATUS_RESET_FAILED:
            monitor_bookmark_copy(out, out_len, "BOOKMARKS RESET FAILED");
            return;
        case MONITOR_BOOKMARK_STATUS_LABEL_SAVED:
            sprintf(buffer, "BM%u %s SAVED", (unsigned)slot, label_buffer);
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        case MONITOR_BOOKMARK_STATUS_LABEL_RESET:
            sprintf(buffer, "BM%u %s RESET", (unsigned)slot, label_buffer);
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        case MONITOR_BOOKMARK_STATUS_LABEL_CANCEL:
            sprintf(buffer, "BM%u %s CANCEL", (unsigned)slot, label_buffer);
            monitor_bookmark_copy(out, out_len, buffer);
            return;
        default:
            break;
    }

    if (!bookmark || !monitor_bookmark_slot_is_valid(*bookmark)) {
        sprintf(buffer, "BM%u RESTORE FAILED", (unsigned)slot);
        monitor_bookmark_copy(out, out_len, buffer);
        return;
    }

    {
        const char *suffix = (kind == MONITOR_BOOKMARK_STATUS_SET) ? " SET" : "";
        if (width_token[0]) {
            sprintf(buffer, "BM%u %s $%04X %s %s CPU%u VIC%u%s",
                    (unsigned)slot,
                    label_buffer,
                    (unsigned)bookmark->address,
                    monitor_bookmark_view_name(bookmark->view),
                    width_token,
                    (unsigned)bookmark->cpu_bank,
                    (unsigned)bookmark->vic_bank,
                    suffix);
        } else {
            sprintf(buffer, "BM%u %s $%04X %s CPU%u VIC%u%s",
                    (unsigned)slot,
                    label_buffer,
                    (unsigned)bookmark->address,
                    monitor_bookmark_view_name(bookmark->view),
                    (unsigned)bookmark->cpu_bank,
                    (unsigned)bookmark->vic_bank,
                    suffix);
        }
    }
    monitor_bookmark_copy(out, out_len, buffer);
}

void monitor_bookmark_format_popup_line(char *out, size_t out_len, uint8_t slot,
                                        const MonitorBookmarkSlot *bookmark)
{
    char buffer[64];
    char label_buffer[MONITOR_BOOKMARK_LABEL_STORAGE];
    char label6[MONITOR_BOOKMARK_LABEL_MAX + 1];
    char view6[MONITOR_BOOKMARK_VIEW_DISPLAY_MAX + 1];

    if (!out || !out_len) {
        return;
    }
    if (!bookmark || !monitor_bookmark_slot_is_valid(*bookmark)) {
        const char *fallback = monitor_bookmark_default_label(slot);
        monitor_bookmark_normalize_label(label_buffer, sizeof(label_buffer), fallback);
        monitor_bookmark_format_label6(label6, sizeof(label6), label_buffer);
        sprintf(buffer, "%u %s ----- ------ CPU- VIC-",
                (unsigned)slot, label6);
        monitor_bookmark_copy(out, out_len, buffer);
        return;
    }

    monitor_bookmark_normalize_label(label_buffer, sizeof(label_buffer), bookmark->label);
    monitor_bookmark_format_label6(label6, sizeof(label6), label_buffer);
    monitor_bookmark_format_view6(view6, sizeof(view6), bookmark->view, bookmark->view_width_mode);

    sprintf(buffer, "%u %s $%04X %s CPU%u VIC%u",
            (unsigned)slot,
            label6,
            (unsigned)bookmark->address,
            view6,
            (unsigned)bookmark->cpu_bank,
            (unsigned)bookmark->vic_bank);
    monitor_bookmark_copy(out, out_len, buffer);
}

MonitorBookmarks :: MonitorBookmarks()
{
    ConfigManager *manager = ConfigManager::getConfigManager();

    cfg = NULL;
    loaded = false;
    if (manager) {
        cfg = manager->find_store("Machine Monitor Bookmarks");
        if (!cfg) {
            cfg = manager->register_store(MONITOR_BOOKMARKS_STORE_ID,
                                          "Machine Monitor Bookmarks",
                                          (t_cfg_definition *)monitor_bookmark_cfg,
                                          (ConfigurableObject *)0);
        }
        if (cfg) {
            cfg->hide();
        }
    }
    persistence_available = (cfg != NULL);
    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        monitor_bookmark_clear(&slots[i]);
    }
}

MonitorBookmarks :: ~MonitorBookmarks()
{
    cfg = NULL;
}

void MonitorBookmarks :: set_defaults(uint8_t default_cpu_bank, uint8_t default_vic_bank)
{
    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        monitor_bookmark_assign_default(&slots[i], (uint8_t)i, default_cpu_bank, default_vic_bank);
    }
}

bool MonitorBookmarks :: load_from_config(void)
{
    if (!cfg) {
        return false;
    }
    if (cfg->get_value(MONITOR_BOOKMARK_CFG_SCHEMA) != MONITOR_BOOKMARKS_SCHEMA_VERSION) {
        return false;
    }
    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        MonitorBookmarkSlot parsed;
        const char *encoded = cfg->get_string(monitor_bookmark_cfg_id_for_slot((uint8_t)i));
        monitor_bookmark_clear(&parsed);
        if (!encoded || !monitor_bookmark_parse_slot(encoded, &parsed)) {
            return false;
        }
        slots[i] = parsed;
    }
    return true;
}

bool MonitorBookmarks :: save_to_config(void)
{
    char encoded[48];

    if (!cfg) {
        return false;
    }
    cfg->set_value(MONITOR_BOOKMARK_CFG_SCHEMA, MONITOR_BOOKMARKS_SCHEMA_VERSION);
    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        monitor_bookmark_encode_slot(encoded, sizeof(encoded), slots[i]);
        cfg->set_string(monitor_bookmark_cfg_id_for_slot((uint8_t)i), encoded);
    }
    cfg->write();
    return true;
}

bool MonitorBookmarks :: ensure_loaded(uint8_t default_cpu_bank, uint8_t default_vic_bank)
{
    if (loaded) {
        return true;
    }
    if (!load_from_config()) {
        set_defaults(default_cpu_bank, default_vic_bank);
        save_to_config();
    }
    loaded = true;
    return true;
}

const MonitorBookmarkSlot *MonitorBookmarks :: get(uint8_t slot) const
{
    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT) {
        return NULL;
    }
    return &slots[slot];
}

bool MonitorBookmarks :: set(uint8_t slot, const MonitorBookmarkSlot &bookmark)
{
    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT || !monitor_bookmark_slot_is_valid(bookmark)) {
        return false;
    }
    slots[slot] = bookmark;
    slots[slot].is_valid = true;
    slots[slot].edit_mode = false;
    slots[slot].view_width_mode = monitor_bookmark_normalize_width(bookmark.view, bookmark.view_width_mode);
    monitor_bookmark_normalize_label(slots[slot].label, sizeof(slots[slot].label), bookmark.label);
    loaded = true;
    return save_to_config();
}

bool MonitorBookmarks :: set_target_preserve_label(uint8_t slot, const MonitorBookmarkSlot &target)
{
    char preserved[MONITOR_BOOKMARK_LABEL_STORAGE];

    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT || !monitor_bookmark_slot_is_valid(target)) {
        return false;
    }
    if (slots[slot].is_valid && slots[slot].label[0]) {
        monitor_bookmark_normalize_label(preserved, sizeof(preserved), slots[slot].label);
    } else {
        const char *fallback = monitor_bookmark_default_label(slot);
        monitor_bookmark_normalize_label(preserved, sizeof(preserved), fallback);
    }
    slots[slot] = target;
    slots[slot].is_valid = true;
    slots[slot].is_default = false;
    slots[slot].edit_mode = false;
    slots[slot].view_width_mode = monitor_bookmark_normalize_width(target.view, target.view_width_mode);
    monitor_bookmark_copy(slots[slot].label, sizeof(slots[slot].label), preserved);
    loaded = true;
    return save_to_config();
}

bool MonitorBookmarks :: set_label(uint8_t slot, const char *label)
{
    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT) {
        return false;
    }
    if (!slots[slot].is_valid) {
        return false;
    }
    monitor_bookmark_normalize_label(slots[slot].label, sizeof(slots[slot].label), label);
    loaded = true;
    return save_to_config();
}

bool MonitorBookmarks :: reset_slot_to_default(uint8_t slot, uint8_t default_cpu_bank, uint8_t default_vic_bank)
{
    if (slot >= MONITOR_BOOKMARK_SLOT_COUNT) {
        return false;
    }
    monitor_bookmark_assign_default(&slots[slot], slot, default_cpu_bank, default_vic_bank);
    loaded = true;
    return save_to_config();
}

bool MonitorBookmarks :: reset_to_defaults(uint8_t default_cpu_bank, uint8_t default_vic_bank)
{
    set_defaults(default_cpu_bank, default_vic_bank);
    loaded = true;
    return save_to_config();
}
