/*
 * radio_test.cpp
 *
 * wifi_radio.c (the one place that starts and stops the radio of the U64 and U2+L modules)
 * against a chip stub that counts the Wi-Fi starts and stops and can be told to fail them,
 * and radio_boot.c (the radio's state across a restart of the module).
 */

#include <stdio.h>
#include "host_test/host_test.h"
extern "C" {
#include "wifi_radio.h"
#include "wifi_radio_port.h"
#include "radio_boot.h"
}

#define CHECK(cond, what) EXPECT_TRUE((cond) && (what))

#define ESP_FAIL_CODE 0x101 // any esp_err_t other than 0

static int starts, stops;
static int start_result, stop_result;
static int chip_on; // what the chip does, as opposed to what wifi_radio.c believes
static int on_during_stop; // radio_is_on() while the chip was stopping

extern "C" int wr_port_wifi_start(void)
{
    starts++;
    if (start_result == 0) {
        chip_on = 1;
    }
    return start_result;
}

extern "C" int wr_port_wifi_stop(void)
{
    stops++;
    on_during_stop = radio_is_on();
    if (stop_result == 0) {
        chip_on = 0;
    }
    return stop_result;
}

// Each case starts with the radio off and the stub counting from zero
static void reset(void)
{
    start_result = stop_result = 0;
    radio_stop();
    starts = stops = 0;
    chip_on = 0;
}

TEST(Radio, starts_once)
{
    reset();
    CHECK(!radio_is_on(), "off to begin with");
    CHECK(radio_start() == 0, "start");
    CHECK(radio_is_on() && chip_on, "on");
    CHECK(radio_start() == 0, "a second start is not an error");
    CHECK(starts == 1, "the chip is started once");
}

TEST(Radio, stops_once)
{
    reset();
    radio_start();
    CHECK(radio_stop() == 0, "stop");
    CHECK(!radio_is_on() && !chip_on, "off");
    CHECK(radio_stop() == 0, "a second stop is not an error");
    CHECK(stops == 1, "the chip is stopped once");
}

TEST(Radio, stop_when_off_does_nothing)
{
    reset();
    CHECK(radio_stop() == 0, "stop while off");
    CHECK(stops == 0 && starts == 0, "the chip is not touched");
}

TEST(Radio, failed_start_stays_off)
{
    reset();
    start_result = ESP_FAIL_CODE;
    CHECK(radio_start() == ESP_FAIL_CODE, "the error is passed on");
    CHECK(!radio_is_on(), "still off");
    start_result = 0;
    CHECK(radio_start() == 0, "a later start is tried again");
    CHECK(starts == 2 && radio_is_on(), "and works");
}

TEST(Radio, failed_stop_stays_on)
{
    reset();
    radio_start();
    stop_result = ESP_FAIL_CODE;
    CHECK(radio_stop() == ESP_FAIL_CODE, "the error is passed on");
    CHECK(radio_is_on() && chip_on, "still on");
    stop_result = 0;
    CHECK(radio_stop() == 0, "a later stop is tried again");
    CHECK(stops == 2 && !radio_is_on(), "and works");
}

TEST(Radio, off_and_on_again)
{
    reset();
    for (int i = 0; i < 100; i++) {
        CHECK(radio_start() == 0, "start");
        CHECK(radio_stop() == 0, "stop");
    }
    CHECK(starts == 100 && stops == 100, "one chip call per change");
    CHECK(!radio_is_on() && !chip_on, "ends off");
}

// radio_boot.c: after a power-up or a reset by the Ultimate the radio waits, whatever the
// record says; after a restart of its own the record decides, and one that does not check
// out counts as none (radio on)
TEST(Radio, boot_decisions)
{
    radio_record_t on, off, rec;
    radio_record_set(&on, true);
    radio_record_set(&off, false);
    CHECK(radio_boot_decision(false, &on) == RADIO_BOOT_WAIT, "power-up, record on: wait");
    CHECK(radio_boot_decision(false, &off) == RADIO_BOOT_WAIT, "power-up, record off: wait");
    CHECK(radio_boot_decision(true, &on) == RADIO_BOOT_ON, "own restart, record on: on");
    CHECK(radio_boot_decision(true, &off) == RADIO_BOOT_OFF, "own restart, record off: off");
    rec = off; rec.magic ^= 1;
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "a wrong magic is no record");
    rec = off; rec.magic = 0x12345678u; rec.check = ~(rec.magic ^ rec.state);
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "another magic with a matching check is no record");
    rec = off; rec.check ^= 0x100;
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "a wrong check is no record");
    rec = on; rec.state = RADIO_RECORD_OFF;
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "a state that does not match its check");
    rec.magic = RADIO_RECORD_MAGIC; rec.state = 0x12345678u; rec.check = ~(RADIO_RECORD_MAGIC ^ rec.state);
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "a state that is neither on nor off");
    rec.magic = 0xFFFFFFFFu; rec.state = 0xFFFFFFFFu; rec.check = 0xFFFFFFFFu;
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_ON, "erased memory is no record");
    rec = on; radio_record_set(&rec, false);
    CHECK(radio_boot_decision(true, &rec) == RADIO_BOOT_OFF, "rewritten from on to off");
}

// A GET_RANDOM draw that overlaps a stop is refused: the radio no longer counts as on once the
// stop begins, and the epoch has moved, also when the stop fails
TEST(Radio, a_stop_moves_the_epoch_first)
{
    reset();
    radio_start();
    uint32_t e = radio_epoch();
    on_during_stop = 1;
    radio_stop();
    CHECK(!on_during_stop, "off while stopping");
    CHECK(radio_epoch() == e + 1, "one stop, one step");
    radio_start();
    CHECK(radio_epoch() == e + 1, "a start does not move it");
    stop_result = ESP_FAIL_CODE;
    radio_stop();
    CHECK(radio_epoch() == e + 2, "a failed stop moves it too");
    radio_stop();
    CHECK(radio_epoch() == e + 3, "and so does its retry");
    stop_result = 0;
    radio_stop();
    radio_stop();
    CHECK(radio_epoch() == e + 4, "a stop while off does not");
}
