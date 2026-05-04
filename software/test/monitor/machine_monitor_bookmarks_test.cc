#include <stdio.h>
#include <string.h>

#include "keyboard_usb.h"
#include "machine_monitor_test_support.h"
#include "monitor_bookmarks.h"

enum {
    TEST_BOOKMARK_CFG_SCHEMA = 1,
    TEST_BOOKMARK_CFG_SLOT0,
};

Keyboard_USB::Keyboard_USB()
{
}

Keyboard_USB::~Keyboard_USB()
{
}

void Keyboard_USB::process_data(uint8_t *)
{
}

int Keyboard_USB::getch(void)
{
    return -1;
}

void Keyboard_USB::push_head(int)
{
}

void Keyboard_USB::push_head_repeat(int, int)
{
}

int Keyboard_USB::count_injected_key(int) const
{
    return 0;
}

bool Keyboard_USB::has_injected_key(int) const
{
    return false;
}

void Keyboard_USB::remove_injected_key(int)
{
}

void Keyboard_USB::wait_free(void)
{
}

void Keyboard_USB::clear_buffer(void)
{
}

void Keyboard_USB::setMatrix(volatile uint8_t *)
{
}

void Keyboard_USB::enableMatrix(bool)
{
}

Keyboard_USB system_usb_keyboard;

namespace monitor_io {

bool pick_file(UserInterface *, const char *, char *, int, char *, int, bool)
{
    return false;
}

const char *load_into_memory(const char *, const char *, MemoryBackend *,
                             uint16_t, bool, uint32_t, bool, uint32_t,
                             uint16_t *, uint32_t *)
{
    return "UNUSED";
}

const char *save_from_memory(UserInterface *, const char *, const char *,
                             MemoryBackend *, uint16_t, uint16_t)
{
    return "UNUSED";
}

void jump_to(uint16_t)
{
}

} // namespace monitor_io

static void reset_bookmark_test_state(void)
{
    monitor_reset_saved_state();
    reset_fake_config_manager_state();
    set_fake_ms_timer(0);
}

static void get_monitor_header(const CaptureScreen &screen, char *out)
{
    screen.get_slice(1, 3, 38, out);
}

static void get_monitor_status(const CaptureScreen &screen, char *out)
{
    screen.get_slice(1, 22, 38, out);
}

static int expect_monitor_border_restored(const CaptureScreen &screen)
{
    for (int y = 3; y <= 22; y++) {
        if (expect((unsigned char)screen.chars[y][0] == CHR_VERTICAL_LINE,
                   "Monitor left border must be restored after the bookmark popup closes.")) return 1;
        if (expect((unsigned char)screen.chars[y][39] == CHR_VERTICAL_LINE,
                   "Monitor right border must be restored after the bookmark popup closes.")) return 1;
    }
    return 0;
}

static void seed_bookmark_full(uint8_t slot, uint16_t address, uint8_t view,
                               uint8_t cpu_bank, uint8_t vic_bank, bool edit_mode,
                               uint8_t binary_width, const char *label)
{
    MonitorBookmarks bookmarks;
    MonitorBookmarkSlot bookmark;

    bookmarks.ensure_loaded(7, vic_bank);
    memset(&bookmark, 0, sizeof(bookmark));
    bookmark.address = address;
    bookmark.view = view;
    bookmark.cpu_bank = cpu_bank;
    bookmark.vic_bank = vic_bank;
    bookmark.binary_width = binary_width;
    bookmark.edit_mode = edit_mode;
    bookmark.is_default = false;
    bookmark.is_valid = true;
    if (label) {
        size_t i = 0;
        while (i < MONITOR_BOOKMARK_LABEL_MAX && label[i]) {
            bookmark.label[i] = label[i];
            i++;
        }
        bookmark.label[i] = 0;
    } else {
        bookmark.label[0] = 0;
    }
    bookmarks.set(slot, bookmark);
}

static void seed_bookmark(uint8_t slot, uint16_t address, uint8_t view,
                          uint8_t cpu_bank, uint8_t vic_bank, bool edit_mode)
{
    seed_bookmark_full(slot, address, view, cpu_bank, vic_bank, edit_mode, 1, NULL);
}

struct BookmarkTestBackend : public FakeFrozenVicBackend
{
    BookmarkTestBackend()
    {
        frozen = false;
        reported_vic_bank = 2;
        requested_vic_bank = 2;
    }
};

static int test_bookmark_addendum_default_table(void)
{
    static const struct {
        uint16_t address;
        uint8_t view;
        const char *label;
    } expected[MONITOR_BOOKMARK_SLOT_COUNT] = {
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

    reset_bookmark_test_state();

    MonitorBookmarks bookmarks;
    if (expect(bookmarks.ensure_loaded(7, 2), "Bookmark defaults should load on empty config.")) return 1;

    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        const MonitorBookmarkSlot *slot = bookmarks.get((uint8_t)i);
        if (expect(slot != NULL, "Bookmark default slot must exist.")) return 1;
        if (expect(slot->address == expected[i].address, "Bookmark default address mismatch.")) return 1;
        if (expect(slot->view == expected[i].view, "Bookmark default view mismatch.")) return 1;
        if (expect(strcmp(slot->label, expected[i].label) == 0,
                   "Bookmark default label mismatch.")) return 1;
        if (expect(slot->cpu_bank == 7 && slot->vic_bank == 0, "Default CPU/VIC bank mismatch.")) return 1;
        if (expect(!slot->edit_mode && slot->is_default, "Default mode/flags mismatch.")) return 1;
        if (expect(slot->binary_width == 1, "Default binary width must be W1.")) return 1;
    }

    // Spot-check rationale items called out by the addendum.
    if (expect(strcmp(bookmarks.get(6)->label, "SID") == 0, "$D400 must be labelled SID.")) return 1;
    if (expect(strcmp(bookmarks.get(4)->label, "HIRAM") == 0, "$C000 must be labelled HIRAM.")) return 1;
    if (expect(bookmarks.get(7)->view == MONITOR_BOOKMARK_VIEW_BINARY &&
               bookmarks.get(7)->binary_width == 1,
               "CIA1 must default to BIN W1.")) return 1;
    if (expect(bookmarks.get(8)->view == MONITOR_BOOKMARK_VIEW_BINARY &&
               bookmarks.get(8)->binary_width == 1,
               "CIA2 must default to BIN W1.")) return 1;
    return 0;
}

static int test_bookmark_label_normalize_helper(void)
{
    char buf[MONITOR_BOOKMARK_LABEL_STORAGE];

    monitor_bookmark_normalize_label(buf, sizeof(buf), "");
    if (expect(strcmp(buf, "USER") == 0, "Empty label must normalize to USER.")) return 1;

    monitor_bookmark_normalize_label(buf, sizeof(buf), NULL);
    if (expect(strcmp(buf, "USER") == 0, "NULL label must normalize to USER.")) return 1;

    monitor_bookmark_normalize_label(buf, sizeof(buf), "sprite");
    if (expect(strcmp(buf, "SPRITE") == 0, "Lowercase label must normalize to uppercase.")) return 1;

    monitor_bookmark_normalize_label(buf, sizeof(buf), "abc!@#");
    if (expect(strcmp(buf, "ABC___") == 0, "Unsupported chars must normalize deterministically.")) return 1;

    monitor_bookmark_normalize_label(buf, sizeof(buf), "TOOLONGLABEL");
    if (expect(strcmp(buf, "TOOLON") == 0, "Overlong label must truncate to 6 chars.")) return 1;

    monitor_bookmark_normalize_label(buf, sizeof(buf), "A_B-1");
    if (expect(strcmp(buf, "A_B-1") == 0, "Allowed chars (A-Z 0-9 _ -) must pass through.")) return 1;
    return 0;
}

static int test_bookmark_corrupt_config_falls_back_to_defaults(void)
{
    reset_bookmark_test_state();
    {
        MonitorBookmarks bookmarks;
        ConfigStore *cfg;

        bookmarks.ensure_loaded(7, 1);
        cfg = ConfigManager::getConfigManager()->find_store("Machine Monitor Bookmarks");
        if (expect(cfg != NULL, "Bookmark config store should exist after default creation.")) return 1;
        cfg->set_value(TEST_BOOKMARK_CFG_SCHEMA, 1);
        cfg->set_string(TEST_BOOKMARK_CFG_SLOT0, "bogus");
        cfg->write();
    }

    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 3);
    const MonitorBookmarkSlot *slot0 = reopened.get(0);

    if (expect(slot0 && slot0->address == 0x0000 && slot0->cpu_bank == 7 && slot0->vic_bank == 0,
               "Corrupt bookmark config should recreate defaults with VIC0.")) return 1;
    if (expect(strcmp(slot0->label, "ZP") == 0,
               "Corrupt config should restore the default label.")) return 1;
    return 0;
}

static int test_bookmark_old_six_field_falls_back(void)
{
    reset_bookmark_test_state();
    {
        MonitorBookmarks bookmarks;
        ConfigStore *cfg;

        bookmarks.ensure_loaded(7, 0);
        cfg = ConfigManager::getConfigManager()->find_store("Machine Monitor Bookmarks");
        if (expect(cfg != NULL, "Config store must exist for malformed-string test.")) return 1;
        cfg->set_value(TEST_BOOKMARK_CFG_SCHEMA, 1);
        // Old pre-addendum encoding: six fields (no binary_width, no label).
        cfg->set_string(TEST_BOOKMARK_CFG_SLOT0, "2049,1,7,0,0,0");
        cfg->write();
    }

    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 0);
    const MonitorBookmarkSlot *slot0 = reopened.get(0);
    if (expect(slot0 && slot0->is_default && slot0->address == 0x0000 &&
               strcmp(slot0->label, "ZP") == 0,
               "Old six-field strings must fall back to addendum defaults.")) return 1;
    return 0;
}

static int test_bookmark_persists_label_and_binary_width(void)
{
    reset_bookmark_test_state();

    MonitorBookmarks bookmarks;
    MonitorBookmarkSlot custom;
    bookmarks.ensure_loaded(7, 0);
    memset(&custom, 0, sizeof(custom));
    custom.address = 0x2000;
    custom.view = MONITOR_BOOKMARK_VIEW_BINARY;
    custom.cpu_bank = 3;
    custom.vic_bank = 1;
    custom.binary_width = 3;
    custom.edit_mode = true;
    custom.is_default = false;
    custom.is_valid = true;
    strcpy(custom.label, "SPRITE");
    if (expect(bookmarks.set(7, custom), "Custom bookmark set should save immediately.")) return 1;

    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 0);
    const MonitorBookmarkSlot *slot7 = reopened.get(7);

    if (expect(slot7 && slot7->address == 0x2000 && slot7->view == MONITOR_BOOKMARK_VIEW_BINARY &&
               slot7->cpu_bank == 3 && slot7->vic_bank == 1 && !slot7->edit_mode &&
               slot7->binary_width == 3 && strcmp(slot7->label, "SPRITE") == 0,
               "Reopen should preserve label and binary width without persisting edit mode.")) return 1;
    return 0;
}

static int test_bookmark_set_preserves_label(void)
{
    reset_bookmark_test_state();

    MonitorBookmarks bookmarks;
    MonitorBookmarkSlot custom;
    bookmarks.ensure_loaded(7, 0);
    memset(&custom, 0, sizeof(custom));
    custom.address = 0x2000;
    custom.view = MONITOR_BOOKMARK_VIEW_BINARY;
    custom.cpu_bank = 7;
    custom.vic_bank = 0;
    custom.binary_width = 1;
    custom.is_valid = true;
    strcpy(custom.label, "MYLBL");
    bookmarks.set(7, custom);

    MonitorBookmarkSlot updated;
    memset(&updated, 0, sizeof(updated));
    updated.address = 0x9000;
    updated.view = MONITOR_BOOKMARK_VIEW_HEX;
    updated.cpu_bank = 5;
    updated.vic_bank = 2;
    updated.binary_width = 2;
    updated.is_valid = true;
    // No label provided in the captured target.
    bookmarks.set_target_preserve_label(7, updated);

    const MonitorBookmarkSlot *slot7 = bookmarks.get(7);
    if (expect(slot7 && slot7->address == 0x9000 && slot7->view == MONITOR_BOOKMARK_VIEW_HEX &&
               slot7->cpu_bank == 5 && slot7->vic_bank == 2 &&
               strcmp(slot7->label, "MYLBL") == 0,
               "set_target_preserve_label must preserve the previous label.")) return 1;
    return 0;
}

static int test_bookmark_label_edit_changes_only_label(void)
{
    reset_bookmark_test_state();

    MonitorBookmarks bookmarks;
    MonitorBookmarkSlot custom;
    bookmarks.ensure_loaded(7, 0);
    memset(&custom, 0, sizeof(custom));
    custom.address = 0xC100;
    custom.view = MONITOR_BOOKMARK_VIEW_ASM;
    custom.cpu_bank = 4;
    custom.vic_bank = 1;
    custom.binary_width = 1;
    custom.is_valid = true;
    strcpy(custom.label, "OLD");
    bookmarks.set(2, custom);

    bookmarks.set_label(2, "new");

    const MonitorBookmarkSlot *slot2 = bookmarks.get(2);
    if (expect(slot2 && slot2->address == 0xC100 && slot2->view == MONITOR_BOOKMARK_VIEW_ASM &&
               slot2->cpu_bank == 4 && slot2->vic_bank == 1 &&
               strcmp(slot2->label, "NEW") == 0,
               "set_label must only update the label (uppercased) and keep target state.")) return 1;
    return 0;
}

static int test_bookmark_selected_reset_restores_default(void)
{
    reset_bookmark_test_state();

    MonitorBookmarks bookmarks;
    bookmarks.ensure_loaded(7, 0);
    bookmarks.set_label(7, "CUSTOM");
    {
        MonitorBookmarkSlot redirect;
        memset(&redirect, 0, sizeof(redirect));
        redirect.address = 0x4000;
        redirect.view = MONITOR_BOOKMARK_VIEW_HEX;
        redirect.cpu_bank = 0;
        redirect.vic_bank = 3;
        redirect.binary_width = 4;
        redirect.is_valid = true;
        strcpy(redirect.label, "CUSTOM");
        bookmarks.set(7, redirect);
    }

    if (expect(bookmarks.reset_slot_to_default(7, 7, 0), "reset_slot_to_default should succeed.")) return 1;
    const MonitorBookmarkSlot *slot7 = bookmarks.get(7);
    if (expect(slot7 && slot7->address == 0xDC00 && slot7->view == MONITOR_BOOKMARK_VIEW_BINARY &&
               slot7->cpu_bank == 7 && slot7->vic_bank == 0 && slot7->binary_width == 1 &&
               slot7->is_default && strcmp(slot7->label, "CIA1") == 0,
               "reset_slot_to_default should restore default target state and label.")) return 1;

    // Ensure other slots are NOT affected.
    const MonitorBookmarkSlot *slot4 = bookmarks.get(4);
    if (expect(slot4 && strcmp(slot4->label, "HIRAM") == 0,
               "reset_slot_to_default must not touch other slots.")) return 1;
    return 0;
}

static int test_bookmark_status_message_formatting(void)
{
    char line[40];
    MonitorBookmarkSlot bookmark;

    // Non-binary apply (omit Wn, omit MODE, includes label).
    memset(&bookmark, 0, sizeof(bookmark));
    bookmark.address = 0x0801;
    bookmark.view = MONITOR_BOOKMARK_VIEW_ASM;
    bookmark.cpu_bank = 7;
    bookmark.vic_bank = 0;
    bookmark.binary_width = 1;
    bookmark.is_valid = true;
    strcpy(bookmark.label, "BASIC");
    monitor_bookmark_format_status(line, sizeof(line), 2, &bookmark, MONITOR_BOOKMARK_STATUS_RESTORED);
    if (expect(strcmp(line, "BM2 BASIC $0801 ASM CPU7 VIC0") == 0,
               "Non-binary apply message must omit Wn and MODE and include the label.")) return 1;
    if (expect(strstr(line, "MODE") == NULL && strstr(line, "VIEW") == NULL,
               "Apply messages must not include MODE.")) return 1;
    if (expect((int)strlen(line) <= 38, "Apply message must fit in 38 columns.")) return 1;

    // Binary apply with Wn.
    bookmark.address = 0xDC00;
    bookmark.view = MONITOR_BOOKMARK_VIEW_BINARY;
    bookmark.binary_width = 1;
    strcpy(bookmark.label, "CIA1");
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_RESTORED);
    if (expect(strcmp(line, "BM7 CIA1 $DC00 BIN W1 CPU7 VIC0") == 0,
               "Binary apply message must include Wn and label.")) return 1;
    if (expect((int)strlen(line) <= 38, "Binary apply message must fit in 38 columns.")) return 1;

    // Binary set with 6-char label and W1.
    bookmark.address = 0x2000;
    strcpy(bookmark.label, "SPRITE");
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_SET);
    if (expect(strcmp(line, "BM7 SPRITE $2000 BIN W1 CPU7 VIC0 SET") == 0,
               "Binary set message must include label and Wn and SET.")) return 1;
    if (expect((int)strlen(line) <= 38, "Binary set with 6-char label must fit in 38 columns.")) return 1;

    // Saved/Reset/Cancel.
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_LABEL_SAVED);
    if (expect(strcmp(line, "BM7 SPRITE SAVED") == 0, "Saved message must be compact.")) return 1;
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_LABEL_RESET);
    if (expect(strcmp(line, "BM7 SPRITE RESET") == 0, "Reset message must be compact.")) return 1;
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_LABEL_CANCEL);
    if (expect(strcmp(line, "BM7 SPRITE CANCEL") == 0, "Cancel message must be compact.")) return 1;

    monitor_bookmark_format_status(line, sizeof(line), 1, NULL, MONITOR_BOOKMARK_STATUS_EMPTY);
    if (expect(strcmp(line, "BM1 EMPTY") == 0, "EMPTY message formatting is incorrect.")) return 1;

    monitor_bookmark_format_status(line, sizeof(line), 0, NULL, MONITOR_BOOKMARK_STATUS_RESET_DEFAULTS);
    if (expect(strcmp(line, "BOOKMARKS RESET TO DEFAULTS") == 0,
               "Reset-defaults message formatting is incorrect.")) return 1;
    return 0;
}

static int test_bookmark_popup_line_formatting(void)
{
    char line[64];
    MonitorBookmarkSlot bookmark;

    memset(&bookmark, 0, sizeof(bookmark));
    bookmark.is_valid = true;
    bookmark.address = 0xE000;
    bookmark.view = MONITOR_BOOKMARK_VIEW_ASM;
    bookmark.cpu_bank = 7;
    bookmark.vic_bank = 0;
    bookmark.binary_width = 1;
    strcpy(bookmark.label, "KERNAL");
    monitor_bookmark_format_popup_line(line, sizeof(line), 9, &bookmark);
    if (expect(strcmp(line, "9 KERNAL $E000 ASM    CPU7 VIC0") == 0,
               "Non-binary popup row must use LABEL6 + padded VIEW6 without a mode column.")) return 1;
    if (expect((int)strlen(line) <= 38, "Popup row must fit in 38 columns.")) return 1;

    bookmark.address = 0xDC00;
    bookmark.view = MONITOR_BOOKMARK_VIEW_BINARY;
    bookmark.binary_width = 1;
    strcpy(bookmark.label, "CIA1");
    monitor_bookmark_format_popup_line(line, sizeof(line), 7, &bookmark);
    if (expect(strcmp(line, "7 CIA1   $DC00 BIN W1 CPU7 VIC0") == 0,
               "Binary popup row must use BIN Wn in VIEW6 without a mode column.")) return 1;
    if (expect((int)strlen(line) <= 38, "Binary popup row must fit in 38 columns.")) return 1;

    bookmark.edit_mode = true;
    monitor_bookmark_format_popup_line(line, sizeof(line), 7, &bookmark);
    if (expect(strcmp(line, "7 CIA1   $DC00 BIN W1 CPU7 VIC0") == 0,
               "Popup row must ignore edit mode because bookmarks no longer store it.")) return 1;
    return 0;
}

static int test_bookmark_default_popup_alignment(void)
{
    reset_bookmark_test_state();
    MonitorBookmarks bookmarks;
    bookmarks.ensure_loaded(7, 0);

    int reference_cpu_col = -1;
    for (int i = 0; i < MONITOR_BOOKMARK_SLOT_COUNT; i++) {
        char line[64];
        monitor_bookmark_format_popup_line(line, sizeof(line), (uint8_t)i, bookmarks.get((uint8_t)i));
        if (expect((int)strlen(line) <= 38, "Default popup row must fit in 38 columns.")) return 1;
        const char *cpu = strstr(line, "CPU");
        if (expect(cpu != NULL, "Default popup row must contain CPU column.")) return 1;
        int col = (int)(cpu - line);
        if (reference_cpu_col < 0) {
            reference_cpu_col = col;
        }
        if (expect(col == reference_cpu_col,
                   "CPU column must align across all default popup rows.")) return 1;
    }
    return 0;
}

static int test_bookmark_popup_instruction_line(void)
{
    const char *expected = "0-9/RET Jmp  S Set  L Label  DEL Reset";
    if (expect((int)strlen(expected) == 38,
               "Instruction line must be 38 chars (within the 38-column popup).")) return 1;
    if (expect((int)strlen(expected) <= 38, "Instruction line must fit in 38 columns.")) return 1;
    if (expect(strstr(expected, "Jmp") != NULL && strstr(expected, "Set") != NULL &&
               strstr(expected, "Label") != NULL && strstr(expected, "Reset") != NULL,
               "Instruction line must document Jmp/Set/Label/Reset.")) return 1;
    return 0;
}

static int test_bookmark_six_char_custom_label_fits(void)
{
    char line[40];
    MonitorBookmarkSlot bookmark;

    memset(&bookmark, 0, sizeof(bookmark));
    bookmark.is_valid = true;
    bookmark.address = 0x2000;
    bookmark.view = MONITOR_BOOKMARK_VIEW_BINARY;
    bookmark.binary_width = 1;
    bookmark.cpu_bank = 7;
    bookmark.vic_bank = 0;
    strcpy(bookmark.label, "SPRITE");
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_SET);
    if (expect(strcmp(line, "BM7 SPRITE $2000 BIN W1 CPU7 VIC0 SET") == 0,
               "Binary set with 6-char label produces canonical message.")) return 1;
    if (expect((int)strlen(line) <= 38, "Binary set with 6-char label must fit in 38 columns.")) return 1;

    bookmark.view = MONITOR_BOOKMARK_VIEW_HEX;
    monitor_bookmark_format_status(line, sizeof(line), 7, &bookmark, MONITOR_BOOKMARK_STATUS_SET);
    if (expect((int)strlen(line) <= 38, "Non-binary 6-char label set must fit in 38 columns.")) return 1;
    if (expect(strstr(line, "Wn") == NULL && strstr(line, " W1") == NULL,
               "Non-binary set must omit Wn.")) return 1;
    return 0;
}

static int test_bookmark_overlong_persisted_label_truncates_for_display(void)
{
    reset_bookmark_test_state();
    {
        MonitorBookmarks bookmarks;
        ConfigStore *cfg;

        bookmarks.ensure_loaded(7, 0);
        cfg = ConfigManager::getConfigManager()->find_store("Machine Monitor Bookmarks");
        if (expect(cfg != NULL, "Config store must exist for overlong label test.")) return 1;
        cfg->set_value(TEST_BOOKMARK_CFG_SCHEMA, 1);
        // Inject a malformed slot string with an overlong label. The parser
        // normalizes this into the fixed-size storage slot.
        cfg->set_string(TEST_BOOKMARK_CFG_SLOT0,
                        "0,0,7,0,0,0,1,VERYLONGLABEL");
        cfg->write();
    }

    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 0);
    const MonitorBookmarkSlot *slot0 = reopened.get(0);
    if (expect(slot0 && (int)strlen(slot0->label) <= MONITOR_BOOKMARK_LABEL_MAX,
               "Overlong persisted label must truncate to <=6 chars.")) return 1;
    if (expect(strcmp(slot0->label, "VERYLO") == 0,
               "Overlong persisted label must truncate deterministically.")) return 1;
    return 0;
}

static int test_monitor_bookmark_capture_restore_includes_binary_width(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];
    char header[39];

    reset_bookmark_test_state();
    seed_bookmark_full(7, 0xDC00, MONITOR_BOOKMARK_VIEW_BINARY, 7, 2, false, 1, "CIA1");

    const int keys[] = { KEY_CTRL_7, KEY_BREAK };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark restore for binary-width test failed.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "BM7 CIA1 $DC00 BIN W1 CPU7 VIC2") == status,
               "Restore status must report BIN W1.")) return 1;
    get_monitor_header(screen, header);
    if (expect(strstr(header, "MONITOR BIN $DC00") == header,
               "Restore must apply binary view and address.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_restore_applies_width_w3(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];

    reset_bookmark_test_state();
    seed_bookmark_full(5, 0x1000, MONITOR_BOOKMARK_VIEW_BINARY, 7, 2, false, 3, "WIDE");

    const int keys[] = { KEY_CTRL_5, KEY_BREAK };
    FakeKeyboard keyboard(keys, 2);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark W3 restore failed.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "BIN W3") != NULL,
               "Restore must apply stored binary_width to the running view.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_capture_no_memory_write(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];
    const int keys[] = { 'J', 'D', 'e', KEY_CTRL_B, KEY_DOWN, 'S', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 8);

    reset_bookmark_test_state();
    backend.ram[0xC000] = 0xA9;
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("C000", 1);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto C000 failed for capture test.")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM view switch failed for capture test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Edit-entry failed for capture test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed for capture test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup selection failed for capture test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup set failed for capture test.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "BM1 ") == status, "Set status must start with BMn label.")) return 1;
    if (expect(strstr(status, " SET") != NULL, "Set status must include SET.")) return 1;
    if (expect(backend.ram[0xC000] == 0xA9, "Capture must not write target memory.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_set_preserves_label(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];

    reset_bookmark_test_state();
    seed_bookmark_full(3, 0xC000, MONITOR_BOOKMARK_VIEW_HEX, 7, 2, false, 1, "OLDLBL");

    const int keys[] = { 'J', 'D', KEY_CTRL_B, KEY_DOWN, KEY_DOWN, KEY_DOWN, 'S', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 9);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("9000", 1);
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM view switch failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup down 1 failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup down 2 failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup down 3 failed for set-preserves-label test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup set failed for set-preserves-label test.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "OLDLBL") != NULL,
               "Set must preserve the existing label OLDLBL.")) return 1;
    monitor.deinit();

    // Reopen to ensure the label survived persistence as well.
    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 2);
    const MonitorBookmarkSlot *slot3 = reopened.get(3);
    if (expect(slot3 && strcmp(slot3->label, "OLDLBL") == 0 && slot3->address == 0x9000,
               "Persisted set must keep the original label.")) return 1;
    return 0;
}

static int test_monitor_bookmark_popup_render(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char line[64];
    int left = 0, top = 0, right = 0, bottom = 0;
    const int keys[] = { KEY_CTRL_B, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);

    reset_bookmark_test_state();
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed.")) return 1;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup should draw a bordered overlay.")) return 1;
    int inner_w = right - left - 1;
    if (expect(inner_w == 38, "Bookmark popup must have 38 inner columns.")) return 1;

    get_popup_line(screen, 0, line, sizeof(line));
    if (expect(strncmp(line, "BOOKMARKS", strlen("BOOKMARKS")) == 0,
               "Popup header line must be BOOKMARKS.")) return 1;
    get_popup_line(screen, 1, line, sizeof(line));
    if (expect(strspn(line, " ") == strlen(line),
               "Popup must separate the header from the bookmarks with a blank line.")) return 1;

    // Verify a few specific row formats.
    get_popup_line(screen, 2, line, sizeof(line));
    if (expect(strncmp(line, "0 ZP     $0000 HEX    CPU",
                       strlen("0 ZP     $0000 HEX    CPU")) == 0,
               "Default popup slot 0 must be ZP HEX (padded VIEW6).")) return 1;
    get_popup_line(screen, 9, line, sizeof(line));
    if (expect(strncmp(line, "7 CIA1   $DC00 BIN W1 CPU",
                       strlen("7 CIA1   $DC00 BIN W1 CPU")) == 0,
               "Default popup slot 7 must be CIA1 BIN W1.")) return 1;
    get_popup_line(screen, 11, line, sizeof(line));
    if (expect(strncmp(line, "9 KERNAL $E000 ASM    CPU",
                       strlen("9 KERNAL $E000 ASM    CPU")) == 0,
               "Default popup slot 9 must be KERNAL ASM (padded VIEW6).")) return 1;
    get_popup_line(screen, 12, line, sizeof(line));
    if (expect(strspn(line, " ") == strlen(line),
               "Popup must separate bookmarks from the help line with a blank line.")) return 1;
    get_popup_line(screen, 13, line, sizeof(line));
    if (expect(strncmp(line, "0-9/RET Jmp  S Set  L Label  DEL Reset",
                       strlen("0-9/RET Jmp  S Set  L Label  DEL Reset")) == 0,
               "Popup instruction line must match addendum text.")) return 1;

    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_popup_opens_in_edit_mode(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int left = 0, top = 0, right = 0, bottom = 0;

    reset_bookmark_test_state();
    const int keys[] = { 'e', KEY_CTRL_B, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Edit-mode entry before popup open failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup open from edit mode failed.")) return 1;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup must open even when monitor is in EDIT mode.")) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_popup_close_restores_full_width_border(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int left = 0, top = 0, right = 0, bottom = 0;
    const int keys[] = { KEY_CTRL_B, KEY_CTRL_B, KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);

    reset_bookmark_test_state();
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed (border cleanup test).")) return 1;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup should draw a full-width bordered overlay.")) return 1;
    if (expect(left == 0 && right == 39,
               "Bookmark popup border cleanup test requires a full-width overlay.")) return 1;

    if (expect(monitor.poll(0) == 0, "Bookmark popup close failed (border cleanup test).")) return 1;
    if (expect(find_popup_rect(screen, NULL, NULL, NULL, NULL) == false,
               "Bookmark popup must be gone after closing.")) return 1;
    if (expect_monitor_border_restored(screen)) return 1;

    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_popup_navigation_keys(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];
    int left = 0, top = 0, right = 0, bottom = 0;
    const int keys[] = { KEY_CTRL_B, KEY_DOWN, KEY_DOWN, KEY_RETURN, KEY_BREAK };
    FakeKeyboard keyboard(keys, 5);

    reset_bookmark_test_state();
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed (navigation test).")) return 1;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup rect must be found before navigation.")) return 1;
    screen.reset_write_counts();
    if (expect(monitor.poll(0) == 0, "DOWN navigation 1 failed.")) return 1;
    if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
               "Bookmark popup navigation must not redraw the underlying monitor view.")) return 1;
    if (expect(monitor.poll(0) == 0, "DOWN navigation 2 failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "RETURN apply on selected row failed.")) return 1;
    get_monitor_status(screen, status);
    // Two DOWNs from default selected=0 -> slot 2 (BASIC ASM $0801).
    if (expect(strstr(status, "BM2 BASIC $0801 ASM") == status,
               "RETURN on selected row must apply that bookmark.")) return 1;
    if (expect_monitor_border_restored(screen)) return 1;
    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_popup_pauses_poll_mode(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    int left = 0, top = 0, right = 0, bottom = 0;
    const int keys[] = { 'P', KEY_CTRL_B };
    FakeKeyboard keyboard(keys, 2);

    reset_bookmark_test_state();
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    backend.write(0x0000, 0x11);

    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Poll mode enable failed before popup test.")) return 1;
    if (expect(monitor.poll(0) == 0, "Bookmark popup open failed with poll mode enabled.")) return 1;
    if (expect(find_popup_rect(screen, &left, &top, &right, &bottom),
               "Bookmark popup must be visible before polling pause is checked.")) return 1;

    screen.reset_write_counts();
    backend.write(0x0000, 0x22);
    set_fake_ms_timer(20);
    if (expect(monitor.poll(0) == 0, "Idle poll while bookmark popup is active failed.")) return 1;
    if (expect(find_popup_rect(screen, NULL, NULL, NULL, NULL),
               "Bookmark popup must remain open while idle polling is paused.")) return 1;
    if (expect(screen.count_writes_outside_rect(left, top, right, bottom) == 0,
               "Poll mode must not redraw the monitor outside the bookmark popup while the popup is active.")) return 1;

    monitor.deinit();
    return 0;
}

static int test_monitor_bookmark_popup_set_preserves_label(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;
    char status[39];

    reset_bookmark_test_state();
    seed_bookmark_full(2, 0x0801, MONITOR_BOOKMARK_VIEW_ASM, 7, 2, false, 1, "BASIC");

    const int keys[] = { 'J', 'D', KEY_CTRL_B, KEY_DOWN, KEY_DOWN, 'S', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 8);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("9000", 1);
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Goto for popup set test failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "ASM view switch for popup set test failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Popup open for popup set test failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Down 1 for popup set test failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "Down 2 for popup set test failed.")) return 1;
    if (expect(monitor.poll(0) == 0, "S in popup failed.")) return 1;

    monitor.deinit();
    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 2);
    const MonitorBookmarkSlot *slot2 = reopened.get(2);
    if (expect(slot2 && slot2->address == 0x9000 && strcmp(slot2->label, "BASIC") == 0,
               "Popup S must update target but preserve label.")) return 1;
    (void)status;
    return 0;
}

static int test_monitor_bookmark_popup_del_resets_selected(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;

    reset_bookmark_test_state();
    seed_bookmark_full(2, 0x9000, MONITOR_BOOKMARK_VIEW_HEX, 0, 1, false, 4, "MYLBL");

    const int keys[] = { KEY_CTRL_B, KEY_DOWN, KEY_DOWN, KEY_DELETE, KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 6);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Popup open failed (DEL test).")) return 1;
    if (expect(monitor.poll(0) == 0, "Down 1 failed (DEL test).")) return 1;
    if (expect(monitor.poll(0) == 0, "Down 2 failed (DEL test).")) return 1;
    if (expect(monitor.poll(0) == 0, "DEL on selected slot failed.")) return 1;

    monitor.deinit();
    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 2);
    const MonitorBookmarkSlot *slot2 = reopened.get(2);
    if (expect(slot2 && slot2->address == 0x0801 && slot2->view == MONITOR_BOOKMARK_VIEW_ASM &&
               strcmp(slot2->label, "BASIC") == 0,
               "DEL on selected slot must restore default target and label.")) return 1;
    return 0;
}

static int test_monitor_bookmark_label_edit(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    BookmarkTestBackend backend;

    reset_bookmark_test_state();
    seed_bookmark_full(0, 0x0000, MONITOR_BOOKMARK_VIEW_HEX, 7, 2, false, 1, "ZP");

    const int keys[] = { KEY_CTRL_B, 'L', KEY_BREAK, KEY_BREAK };
    FakeKeyboard keyboard(keys, 4);
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    ui.push_prompt("hello", 1);
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Popup open failed (label edit test).")) return 1;
    if (expect(monitor.poll(0) == 0, "L key for label edit failed.")) return 1;

    monitor.deinit();
    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 2);
    const MonitorBookmarkSlot *slot0 = reopened.get(0);
    if (expect(slot0 && strcmp(slot0->label, "HELLO") == 0 &&
               slot0->address == 0x0000 && slot0->view == MONITOR_BOOKMARK_VIEW_HEX,
               "Label edit must update label and preserve target state.")) return 1;
    return 0;
}

static int test_monitor_bookmark_shortcut_routing(void)
{
    char header[39];

    reset_bookmark_test_state();
    seed_bookmark_full(1, 0xC000, MONITOR_BOOKMARK_VIEW_ASM, 7, 2, false, 1, "T1");

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'e', '1', KEY_BREAK, KEY_BREAK };
        FakeKeyboard keyboard(keys, 4);

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        monitor_reset_saved_state();
        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        if (expect(monitor.poll(0) == 0, "Hex edit entry failed.")) return 1;
        if (expect(monitor.poll(0) == 0, "Digit routing in edit mode failed.")) return 1;
        get_monitor_header(screen, header);
        if (expect(strstr(header, "MONITOR HEX $0000") == header,
                   "Digit restore should not trigger while editing.")) return 1;
        monitor.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'e', KEY_CTRL_1, KEY_BREAK, KEY_BREAK };
        FakeKeyboard keyboard(keys, 4);

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        monitor_reset_saved_state();
        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        if (expect(monitor.poll(0) == 0, "Hex edit entry before bookmark jump failed.")) return 1;
        if (expect(monitor.poll(0) == 0, "CBM+1 bookmark jump should work in edit mode.")) return 1;
        get_monitor_header(screen, header);
        if (expect(strstr(header, "MONITOR ASM $C000") == header && strstr(header, "EDIT") != NULL,
                   "Bookmark jumps must stay enabled in edit mode and preserve edit state.")) return 1;
        monitor.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { 'N', '1', KEY_ESCAPE, KEY_BREAK };
        FakeKeyboard keyboard(keys, 4);

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        monitor_reset_saved_state();
        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        if (expect(monitor.poll(0) == 0, "Number popup open failed.")) return 1;
        if (expect(monitor.poll(0) == 0, "Digit while number popup is open should stay in the popup.")) return 1;
        if (expect(find_popup_rect(screen, NULL, NULL, NULL, NULL),
                   "Number popup should remain open when digits are typed into it.")) return 1;
        monitor.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { '1', KEY_BREAK };
        FakeKeyboard keyboard(keys, 2);

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        monitor_reset_saved_state();
        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        if (expect(monitor.poll(0) == 0, "Bare digit in main view should remain a normal key.")) return 1;
        get_monitor_header(screen, header);
        if (expect(strstr(header, "$C000") == NULL,
                   "Bare digits in main monitor views must not jump to bookmarks.")) return 1;
        monitor.deinit();
    }

    {
        TestUserInterface ui;
        CaptureScreen screen;
        FakeMemoryBackend backend;
        const int keys[] = { KEY_CTRL_1, KEY_BREAK };
        FakeKeyboard keyboard(keys, 2);

        ui.screen = &screen;
        ui.keyboard = &keyboard;
        monitor_reset_saved_state();
        BackendMachineMonitor monitor(&ui, &backend);
        monitor.init(&screen, &keyboard);

        if (expect(monitor.poll(0) == 0, "CBM+1 bookmark jump failed.")) return 1;
        get_monitor_header(screen, header);
        if (expect(strstr(header, "MONITOR ASM $C000") == header,
                   "CBM+digits in main monitor views must jump to bookmarks.")) return 1;
        monitor.deinit();
    }

    MonitorBookmarks reopened;
    reopened.ensure_loaded(7, 2);
    const MonitorBookmarkSlot *slot1 = reopened.get(1);
    if (expect(slot1 && slot1->address == 0xC000 && strcmp(slot1->label, "T1") == 0,
               "CBM+digits in main monitor views must not overwrite bookmark targets.")) return 1;

    return 0;
}

static int test_monitor_bookmark_status_dismissal(void)
{
    TestUserInterface ui;
    CaptureScreen screen;
    FakeMemoryBackend backend;
    char status[39];
    const int keys[] = { KEY_CTRL_1, KEY_RIGHT, KEY_BREAK };
    FakeKeyboard keyboard(keys, 3);

    reset_bookmark_test_state();
    seed_bookmark_full(1, 0x0801, MONITOR_BOOKMARK_VIEW_ASM, 7, 2, false, 1, "BASIC");
    ui.screen = &screen;
    ui.keyboard = &keyboard;
    BackendMachineMonitor monitor(&ui, &backend);
    monitor.init(&screen, &keyboard);

    if (expect(monitor.poll(0) == 0, "Bookmark restore for status dismissal test failed.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "BM1 BASIC $0801 ASM CPU7 VIC2") == status,
               "Bookmark restore status message should appear immediately.")) return 1;
    if (expect(monitor.poll(0) == 0, "Next user action should dismiss bookmark status.")) return 1;
    get_monitor_status(screen, status);
    if (expect(strstr(status, "CPU7") == status,
               "Next user action should restore the normal bottom status row.")) return 1;
    monitor.deinit();
    return 0;
}

int main()
{
    if (test_bookmark_addendum_default_table()) return 1;
    if (test_bookmark_label_normalize_helper()) return 1;
    if (test_bookmark_corrupt_config_falls_back_to_defaults()) return 1;
    if (test_bookmark_old_six_field_falls_back()) return 1;
    if (test_bookmark_persists_label_and_binary_width()) return 1;
    if (test_bookmark_set_preserves_label()) return 1;
    if (test_bookmark_label_edit_changes_only_label()) return 1;
    if (test_bookmark_selected_reset_restores_default()) return 1;
    if (test_bookmark_status_message_formatting()) return 1;
    if (test_bookmark_popup_line_formatting()) return 1;
    if (test_bookmark_default_popup_alignment()) return 1;
    if (test_bookmark_popup_instruction_line()) return 1;
    if (test_bookmark_six_char_custom_label_fits()) return 1;
    if (test_bookmark_overlong_persisted_label_truncates_for_display()) return 1;
    if (test_monitor_bookmark_capture_restore_includes_binary_width()) return 1;
    if (test_monitor_bookmark_restore_applies_width_w3()) return 1;
    if (test_monitor_bookmark_capture_no_memory_write()) return 1;
    if (test_monitor_bookmark_set_preserves_label()) return 1;
    if (test_monitor_bookmark_popup_render()) return 1;
    if (test_monitor_bookmark_popup_opens_in_edit_mode()) return 1;
    if (test_monitor_bookmark_popup_close_restores_full_width_border()) return 1;
    if (test_monitor_bookmark_popup_navigation_keys()) return 1;
    if (test_monitor_bookmark_popup_pauses_poll_mode()) return 1;
    if (test_monitor_bookmark_popup_set_preserves_label()) return 1;
    if (test_monitor_bookmark_popup_del_resets_selected()) return 1;
    if (test_monitor_bookmark_label_edit()) return 1;
    if (test_monitor_bookmark_shortcut_routing()) return 1;
    if (test_monitor_bookmark_status_dismissal()) return 1;

    puts("machine_monitor_bookmarks_test: OK");
    return 0;
}
