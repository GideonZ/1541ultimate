/* Minimal esp_err.h for building ESP32 sources on the host, see
   software/test/power_state. Only what the code under test uses. */
#ifndef TEST_STUB_ESP_ERR_H
#define TEST_STUB_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK                   0
#define ESP_FAIL                -1
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_NVS_NOT_FOUND    0x1102

const char *esp_err_to_name(esp_err_t code);

#endif
