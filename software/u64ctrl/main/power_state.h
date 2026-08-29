/*
 * power_state.h
 *
 * Determines what the machine does when the input power returns after it has
 * been lost. The ESP32 is the only part of the machine that is powered before
 * the user presses the power button, so the desired behavior is stored here,
 * in the NVS of the ESP32.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_POWER_STATE_H_
#define SOFTWARE_U64CTRL_MAIN_POWER_STATE_H_

#include <stdint.h>
#include "esp_err.h"

/* These values are also used as the enum values of the "Power On After Power
   Loss" setting in the menu of the machine, so don't renumber them. */
#define POWERON_MODE_OFF        0  // stay off, until the power button is pressed (default)
#define POWERON_MODE_ON         1  // always power up
#define POWERON_MODE_LAST_STATE 2  // power up if the machine was on when the power was lost
#define POWERON_MODE_MAX        POWERON_MODE_LAST_STATE

/* Human readable name of a mode, for logging. Never returns NULL. */
const char *power_mode_name(uint8_t mode);

/* The configured behavior; POWERON_MODE_OFF when nothing was stored yet. */
uint8_t power_get_mode(void);

/* Stores the behavior; ESP_ERR_INVALID_ARG for an unknown mode. */
esp_err_t power_set_mode(uint8_t mode);

/* Whether the machine was on the last time the power state was stored. */
uint8_t power_get_last_state(void);

/* Called on every power transition, to keep POWERON_MODE_LAST_STATE informed. */
void power_store_last_state(uint8_t machine_on);

/* The state the machine should come up in. When the application FPGA is
   already loaded, this is not a cold start (only the ESP32 restarted), and the
   machine is left on, regardless of the configured behavior. */
int power_initial_state(int fpga_running);

#endif /* SOFTWARE_U64CTRL_MAIN_POWER_STATE_H_ */
