/*
 * radio_boot.h
 *
 * Whether the radio of the U64 and U2+L WiFi modules (software/wifi/raw_u64, raw_c3) goes
 * on at boot. After a power-up, or a reset by the Ultimate, the module starts with its
 * radio off and waits for the Ultimate (wifi_modem.c, switch_seen). After a restart the
 * Ultimate did not cause (a crash, a watchdog, a brownout), nobody would tell it again, so
 * it goes back to the radio's last state, kept in RTC memory that such a reset leaves
 * alone. A record that does not check out (lost, half written, never written) counts as
 * none: the radio goes on, as these modules always did at boot, and the module tells the
 * Ultimate it restarted (EVENT_RESTARTED), so that one with "WiFi Enabled" Disabled asks
 * for the radio off again. Until it does, the radio is on.
 *
 * Pure logic, so that the host test (target/pc/linux/wifiradio) runs it.
 */

#ifndef SOFTWARE_WIFI_COMMON_RADIO_BOOT_H_
#define SOFTWARE_WIFI_COMMON_RADIO_BOOT_H_

#include <stdbool.h>
#include <stdint.h>

#define RADIO_RECORD_MAGIC 0x52414449u // "RADI"
#define RADIO_RECORD_ON    0x4F4E4F4Eu
#define RADIO_RECORD_OFF   0x4F464646u

typedef struct {
    uint32_t magic;
    uint32_t state; // RADIO_RECORD_ON or RADIO_RECORD_OFF
    uint32_t check; // ~(magic ^ state)
} radio_record_t;

typedef enum {
    RADIO_BOOT_WAIT, // radio off, until the Ultimate asks
    RADIO_BOOT_ON,   // a restart of its own: radio on, as before it
    RADIO_BOOT_OFF,  // a restart of its own: radio off, as before it
} radio_boot_t;

// Called wherever the radio goes on or off for good
void radio_record_set(radio_record_t *rec, bool on);

// self_restart: the reset was not a power-up nor one the Ultimate caused
radio_boot_t radio_boot_decision(bool self_restart, const radio_record_t *rec);

#endif /* SOFTWARE_WIFI_COMMON_RADIO_BOOT_H_ */
