/* Minimal nvs.h for host builds. The implementation in nvs_stub.c keeps the
   key/value pairs in memory and counts commits, so a test can tell an actual
   flash write from a write that was skipped because nothing changed. */
#ifndef TEST_STUB_NVS_H
#define TEST_STUB_NVS_H

#include <stdint.h>
#include "esp_err.h"

typedef unsigned int nvs_handle_t;

typedef enum {
    NVS_READONLY = 0,
    NVS_READWRITE = 1,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void      nvs_close(nvs_handle_t handle);

/* Test-only controls, not part of the ESP-IDF API. */
void nvs_stub_erase_all(void);   // a factory fresh module
int  nvs_stub_commits(void);     // how often the flash was actually written
void nvs_stub_reset_commits(void);

#endif
