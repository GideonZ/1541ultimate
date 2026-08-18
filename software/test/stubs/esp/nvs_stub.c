/*
 * An in-memory stand-in for the NVS of the control module, so that the power
 * on behavior can be exercised on the host. Values survive nvs_close(), like
 * flash does, and only nvs_commit() counts as a write.
 */
#include <string.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#define MAX_ENTRIES 16
#define MAX_KEY     16

static struct {
    char    ns[MAX_KEY];
    char    key[MAX_KEY];
    uint8_t value;
    int     used;
} entries[MAX_ENTRIES];

#define MAX_HANDLES 8

static char handle_ns[MAX_HANDLES][MAX_KEY];
static int  handle_writable[MAX_HANDLES];
static int  handle_used[MAX_HANDLES];
static int  commits;

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
        case ESP_OK:                return "ESP_OK";
        case ESP_FAIL:              return "ESP_FAIL";
        case ESP_ERR_INVALID_ARG:   return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
    }
    return "?";
}

esp_err_t nvs_flash_init(void) { return ESP_OK; }

void nvs_stub_erase_all(void)
{
    memset(entries, 0, sizeof(entries));
    memset(handle_used, 0, sizeof(handle_used));
    commits = 0;
}

int  nvs_stub_commits(void)       { return commits; }
void nvs_stub_reset_commits(void) { commits = 0; }

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    int handle = 0;
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (!handle_used[i]) {
            handle = i;
            break;
        }
    }
    if (!handle) {
        return ESP_FAIL; // every handle still open: the code under test leaks
    }
    // Opening a namespace that does not exist yet read-only fails on the real
    // NVS, and power_state.c depends on that to fall back to its default.
    if (open_mode == NVS_READONLY) {
        int found = 0;
        for (int i = 0; i < MAX_ENTRIES; i++) {
            if (entries[i].used && strcmp(entries[i].ns, namespace_name) == 0) {
                found = 1;
            }
        }
        if (!found) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
    }
    strncpy(handle_ns[handle], namespace_name, MAX_KEY - 1);
    handle_writable[handle] = (open_mode == NVS_READWRITE);
    handle_used[handle] = 1;
    *out_handle = (nvs_handle_t)handle;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (entries[i].used && strcmp(entries[i].ns, handle_ns[handle]) == 0
                            && strcmp(entries[i].key, key) == 0) {
            *out_value = entries[i].value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    if (!handle_writable[handle]) {
        return ESP_FAIL;
    }
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (entries[i].used && strcmp(entries[i].ns, handle_ns[handle]) == 0
                            && strcmp(entries[i].key, key) == 0) {
            entries[i].value = value;
            return ESP_OK;
        }
    }
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (!entries[i].used) {
            entries[i].used = 1;
            strncpy(entries[i].ns, handle_ns[handle], MAX_KEY - 1);
            strncpy(entries[i].key, key, MAX_KEY - 1);
            entries[i].value = value;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    commits++;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    if (handle < MAX_HANDLES) {
        handle_used[handle] = 0;
    }
}
