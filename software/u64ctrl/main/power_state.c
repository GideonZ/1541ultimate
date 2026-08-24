/*
 * power_state.c
 *
 * See power_state.h.
 */

#include "power_state.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "power_state";

#define NVS_NAMESPACE "power"
#define NVS_KEY_MODE  "mode"
#define NVS_KEY_LAST  "last"
#define NVS_KEY_WAKE  "wowifi"

static uint8_t read_u8(const char *key, uint8_t dflt)
{
    nvs_handle_t handle;
    uint8_t value;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(handle, key, &value);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "'%s' not read (%s); using default %d", key, esp_err_to_name(err), dflt);
        return dflt;
    }
    return value;
}

static esp_err_t write_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    uint8_t current;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    // The last state is written on every power transition, so don't wear out
    // the flash when there is nothing to change.
    if ((nvs_get_u8(handle, key, &current) == ESP_OK) && (current == value)) {
        nvs_close(handle);
        return ESP_OK;
    }
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot store '%s' = %d: %s", key, value, esp_err_to_name(err));
    }
    return err;
}

const char *power_mode_name(uint8_t mode)
{
    switch(mode) {
        case POWERON_MODE_OFF:        return "OFF";
        case POWERON_MODE_ON:         return "ON";
        case POWERON_MODE_LAST_STATE: return "LAST_STATE";
    }
    return "?";
}

uint8_t power_get_mode(void)
{
    uint8_t mode = read_u8(NVS_KEY_MODE, POWERON_MODE_OFF);
    if (mode > POWERON_MODE_MAX) {
        ESP_LOGW(TAG, "Stored mode %d is out of range; falling back to OFF", mode);
        mode = POWERON_MODE_OFF;
    }
    return mode;
}

esp_err_t power_set_mode(uint8_t mode)
{
    if (mode > POWERON_MODE_MAX) {
        ESP_LOGE(TAG, "Refusing to store unknown mode %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Power on after power loss: %s", power_mode_name(mode));
    return write_u8(NVS_KEY_MODE, mode);
}

uint8_t power_get_wake_on_wifi(void)
{
    uint8_t enabled = read_u8(NVS_KEY_WAKE, WAKE_ON_WIFI_DISABLED);
    if (enabled > WAKE_ON_WIFI_MAX) {
        ESP_LOGW(TAG, "Stored wake on Wi-Fi %d is out of range; falling back to disabled", enabled);
        enabled = WAKE_ON_WIFI_DISABLED;
    }
    return enabled;
}

esp_err_t power_set_wake_on_wifi(uint8_t enabled)
{
    if (enabled > WAKE_ON_WIFI_MAX) {
        ESP_LOGE(TAG, "Refusing to store unknown wake on Wi-Fi value %d", enabled);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Wake on Wi-Fi: %s", enabled ? "ENABLED" : "DISABLED");
    return write_u8(NVS_KEY_WAKE, enabled);
}

uint8_t power_get_last_state(void)
{
    return read_u8(NVS_KEY_LAST, 0) ? 1 : 0;
}

void power_store_last_state(uint8_t machine_on)
{
    // Kept up to date in all modes, such that switching to LAST_STATE later on
    // does not act on a stale value.
    write_u8(NVS_KEY_LAST, machine_on ? 1 : 0);
}

int power_initial_state(int fpga_running)
{
    if (fpga_running) {
        ESP_LOGI(TAG, "Application FPGA is already loaded; leaving the machine on.");
        power_store_last_state(1);
        return 1;
    }

    uint8_t mode = power_get_mode();
    int machine_on;

    switch(mode) {
        case POWERON_MODE_ON:
            machine_on = 1;
            break;
        case POWERON_MODE_LAST_STATE:
            machine_on = (int)power_get_last_state();
            break;
        default:
            machine_on = 0;
            break;
    }
    ESP_LOGI(TAG, "Cold start with mode %s; machine comes up %s.", power_mode_name(mode),
             machine_on ? "ON" : "OFF");
    // The state the machine comes up in is a power transition like any other:
    // record it, or a later switch to LAST_STATE acts on the state before this
    // boot rather than on the state this boot established.
    power_store_last_state((uint8_t)machine_on);
    return machine_on;
}
