#ifndef MONITOR_BOOKMARKS_H
#define MONITOR_BOOKMARKS_H

#include <stddef.h>
#include <stdint.h>

class ConfigStore;

enum {
    MONITOR_BOOKMARK_SLOT_COUNT = 10,
    MONITOR_BOOKMARK_VIEW_HEX = 0,
    MONITOR_BOOKMARK_VIEW_ASM,
    MONITOR_BOOKMARK_VIEW_ASCII,
    MONITOR_BOOKMARK_VIEW_SCREEN,
    MONITOR_BOOKMARK_VIEW_BINARY,
    MONITOR_BOOKMARK_VIEW_MAX = MONITOR_BOOKMARK_VIEW_BINARY,
    MONITOR_BOOKMARK_LABEL_MAX = 6,
    MONITOR_BOOKMARK_LABEL_STORAGE = MONITOR_BOOKMARK_LABEL_MAX + 1,
    MONITOR_BOOKMARK_BINARY_WIDTH_MIN = 1,
    MONITOR_BOOKMARK_BINARY_WIDTH_MAX = 4,
};

struct MonitorBookmarkSlot
{
    uint16_t address;
    uint8_t view;
    uint8_t cpu_bank;
    uint8_t vic_bank;
    uint8_t binary_width;
    bool edit_mode;
    bool is_default;
    bool is_valid;
    char label[MONITOR_BOOKMARK_LABEL_STORAGE];
};

enum MonitorBookmarkStatusKind {
    MONITOR_BOOKMARK_STATUS_RESTORED = 0,
    MONITOR_BOOKMARK_STATUS_SET,
    MONITOR_BOOKMARK_STATUS_EMPTY,
    MONITOR_BOOKMARK_STATUS_SAVE_FAILED,
    MONITOR_BOOKMARK_STATUS_RESTORE_FAILED,
    MONITOR_BOOKMARK_STATUS_RESET_DEFAULTS,
    MONITOR_BOOKMARK_STATUS_RESET_FAILED,
    MONITOR_BOOKMARK_STATUS_LABEL_SAVED,
    MONITOR_BOOKMARK_STATUS_LABEL_RESET,
    MONITOR_BOOKMARK_STATUS_LABEL_CANCEL,
};

const char *monitor_bookmark_default_label(uint8_t slot);
const char *monitor_bookmark_view_name(uint8_t view);
bool monitor_bookmark_slot_is_valid(const MonitorBookmarkSlot &slot);
bool monitor_bookmark_status_uses_emphasis(MonitorBookmarkStatusKind kind, const MonitorBookmarkSlot *slot);
void monitor_bookmark_normalize_label(char *out, size_t out_size, const char *input);
void monitor_bookmark_format_label6(char *out, size_t out_size, const char *label);
void monitor_bookmark_format_view6(char *out, size_t out_size, uint8_t view, uint8_t binary_width);
void monitor_bookmark_format_status(char *out, size_t out_len, uint8_t slot,
                                    const MonitorBookmarkSlot *bookmark,
                                    MonitorBookmarkStatusKind kind);
void monitor_bookmark_format_popup_line(char *out, size_t out_len, uint8_t slot,
                                        const MonitorBookmarkSlot *bookmark);

class MonitorBookmarks
{
    MonitorBookmarkSlot slots[MONITOR_BOOKMARK_SLOT_COUNT];
    ConfigStore *cfg;
    bool loaded;
    bool persistence_available;

    void set_defaults(uint8_t default_cpu_bank, uint8_t default_vic_bank);
    bool load_from_config(void);
    bool save_to_config(void);

public:
    MonitorBookmarks();
    ~MonitorBookmarks();

    bool ensure_loaded(uint8_t default_cpu_bank, uint8_t default_vic_bank);
    const MonitorBookmarkSlot *get(uint8_t slot) const;
    bool set(uint8_t slot, const MonitorBookmarkSlot &bookmark);
    bool set_target_preserve_label(uint8_t slot, const MonitorBookmarkSlot &target);
    bool set_label(uint8_t slot, const char *label);
    bool reset_slot_to_default(uint8_t slot, uint8_t default_cpu_bank, uint8_t default_vic_bank);
    bool reset_to_defaults(uint8_t default_cpu_bank, uint8_t default_vic_bank);
};

#endif
