/*
 * Host side tests for the power on behavior of the control module.
 *
 *   make -C target/pc/linux/powerstate && ./target/pc/linux/powerstate/result/powerstate
 *
 * The real software/u64ctrl/main/power_state.c is compiled here against the
 * NVS stub in software/test/stubs/esp, so what is exercised is the code that
 * ships, not a copy of it.
 */
#include <stdio.h>
#include <string.h>
#include "power_state.h"
#include "nvs.h"

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

static void check_eq(int got, int expected, const char *what)
{
    checks++;
    if (got != expected) {
        failures++;
        printf("  FAIL  %s (expected %d, got %d)\n", what, expected, got);
    } else {
        printf("  ok    %s\n", what);
    }
}

// A machine that has been running: the mode is set and the buttons have
// recorded a state, as they do on every press.
static void given(uint8_t mode, uint8_t last_state)
{
    nvs_stub_erase_all();
    power_set_mode(mode);
    power_store_last_state(last_state);
    nvs_stub_reset_commits();
}

int main(void)
{
    printf("A factory fresh module\n");
    nvs_stub_erase_all();
    check_eq(power_get_mode(), POWERON_MODE_OFF, "defaults to OFF");
    check_eq(power_get_last_state(), 0, "reports the machine as off");
    check_eq(power_initial_state(0), 0, "stays off on a cold start");

    printf("Mode OFF\n");
    given(POWERON_MODE_OFF, 1);
    check_eq(power_initial_state(0), 0, "stays off although the machine was on");
    check_eq(power_get_last_state(), 0, "records that it came up off");

    printf("Mode ON\n");
    given(POWERON_MODE_ON, 0);
    check_eq(power_initial_state(0), 1, "comes up although the machine was off");
    check_eq(power_get_last_state(), 1, "records that it came up on");
    given(POWERON_MODE_ON, 1);
    check_eq(power_initial_state(0), 1, "comes up when the machine was on");

    printf("Mode LAST_STATE\n");
    given(POWERON_MODE_LAST_STATE, 1);
    check_eq(power_initial_state(0), 1, "comes up when the machine was on");
    check_eq(power_get_last_state(), 1, "leaves the recorded state on");
    given(POWERON_MODE_LAST_STATE, 0);
    check_eq(power_initial_state(0), 0, "stays off when the machine was off");
    check_eq(power_get_last_state(), 0, "leaves the recorded state off");

    printf("Switching to LAST_STATE after the machine came up by itself\n");
    given(POWERON_MODE_ON, 0);
    (void)power_initial_state(0);                 // comes up, machine is now on
    power_set_mode(POWERON_MODE_LAST_STATE);      // user changes the mode
    check_eq(power_initial_state(0), 1, "acts on the state this boot established");

    printf("An already loaded application FPGA\n");
    given(POWERON_MODE_OFF, 0);
    check_eq(power_initial_state(1), 1, "leaves the machine on, whatever the mode says");
    check_eq(power_get_last_state(), 1, "records that the machine is on");

    printf("A stored mode that is out of range\n");
    nvs_stub_erase_all();
    power_store_last_state(1);                    // creates the namespace
    {
        nvs_handle_t h;
        check_eq(nvs_open("power", NVS_READWRITE, &h), ESP_OK, "namespace can be opened");
        nvs_set_u8(h, "mode", 42);
        nvs_commit(h);
        nvs_close(h);
    }
    check_eq(power_get_mode(), POWERON_MODE_OFF, "is read as OFF");
    check_eq(power_initial_state(0), 0, "keeps the machine off");

    printf("Storing a mode that does not exist\n");
    given(POWERON_MODE_ON, 0);
    check_eq(power_set_mode(3), ESP_ERR_INVALID_ARG, "is refused");
    check_eq(power_get_mode(), POWERON_MODE_ON, "leaves the stored mode alone");

    printf("Writing a value that has not changed\n");
    given(POWERON_MODE_LAST_STATE, 1);
    power_store_last_state(1);
    check_eq(nvs_stub_commits(), 0, "does not touch the flash");
    power_store_last_state(0);
    check(nvs_stub_commits() > 0, "a real change does touch the flash");

    printf("Wake on Wi-Fi, on a factory fresh module\n");
    nvs_stub_erase_all();
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_DISABLED, "is disabled");

    printf("Turning Wake on Wi-Fi on and off\n");
    check_eq(power_set_wake_on_wifi(WAKE_ON_WIFI_ENABLED), ESP_OK, "enabling is accepted");
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_ENABLED, "reads back as enabled");
    check_eq(power_set_wake_on_wifi(WAKE_ON_WIFI_DISABLED), ESP_OK, "disabling is accepted");
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_DISABLED, "reads back as disabled");

    printf("Storing a Wake on Wi-Fi value that does not exist\n");
    (void)power_set_wake_on_wifi(WAKE_ON_WIFI_ENABLED);
    check_eq(power_set_wake_on_wifi(2), ESP_ERR_INVALID_ARG, "is refused");
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_ENABLED, "leaves the stored value alone");

    printf("A stored Wake on Wi-Fi value that is out of range\n");
    {
        nvs_handle_t h;
        check_eq(nvs_open("power", NVS_READWRITE, &h), ESP_OK, "namespace can be opened");
        nvs_set_u8(h, "wowifi", 42);
        nvs_commit(h);
        nvs_close(h);
    }
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_DISABLED, "is read as disabled");

    printf("The two settings share a namespace but not a value\n");
    given(POWERON_MODE_LAST_STATE, 1);
    (void)power_set_wake_on_wifi(WAKE_ON_WIFI_ENABLED);
    check_eq(power_get_mode(), POWERON_MODE_LAST_STATE, "the mode survives a wake write");
    check_eq(power_get_wake_on_wifi(), WAKE_ON_WIFI_ENABLED, "the wake setting survives a mode write");
    check_eq(power_initial_state(0), 1, "and the cold start still acts on the mode");

    printf("Writing a Wake on Wi-Fi value that has not changed\n");
    (void)power_set_wake_on_wifi(WAKE_ON_WIFI_ENABLED);
    nvs_stub_reset_commits();
    (void)power_set_wake_on_wifi(WAKE_ON_WIFI_ENABLED);
    check_eq(nvs_stub_commits(), 0, "does not touch the flash");
    (void)power_set_wake_on_wifi(WAKE_ON_WIFI_DISABLED);
    check(nvs_stub_commits() > 0, "a real change does touch the flash");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
