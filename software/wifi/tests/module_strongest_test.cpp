/*
 * module_strongest_test.cpp
 *
 * Fix 4: which access point a WiFi module joins when several share one SSID. attempt_connect()
 * and wifi_connect_to_scanned() are the module's own, run against the station model of
 * module_stubs.c, whose esp_wifi_connect() picks an AP from a scripted air the way the IDF
 * station does.
 */

#include <string.h>
#include "module_aps.h"


TEST(Strongest, ssid_joins_the_strongest)
{
    three_aps();
    char ssid[32] = "Home";
    char pw[68] = "secret-pw";
    CHECK(air_attempt_connect(ssid, pw, WIFI_AUTH_WPA2_PSK, NULL, 0) == ESP_OK, "the connect is accepted");
    CHECK(joined == near_ap, "the strongest AP of the SSID is joined, not the first on channel 1");
    CHECK(last_config.sta.scan_method == WIFI_ALL_CHANNEL_SCAN, "all channels are scanned");
    CHECK(last_config.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL, "the APs are sorted by signal");
}

TEST(Strongest, config_otherwise_unchanged)
{
    three_aps();
    char ssid[32] = "Home";
    char pw[68] = "secret-pw";
    air_attempt_connect(ssid, pw, WIFI_AUTH_WPA2_PSK, NULL, 0);
    CHECK(strncmp((char *)last_config.sta.ssid, "Home", 32) == 0, "the SSID is passed");
    CHECK(strncmp((char *)last_config.sta.password, "secret-pw", 64) == 0, "the password is passed");
    CHECK(last_config.sta.threshold.authmode == WIFI_AUTH_WPA2_PSK, "the auth mode is the threshold");
    CHECK(last_config.sta.pmf_cfg.capable && !last_config.sta.pmf_cfg.required, "PMF capable, not required");
    CHECK(!last_config.sta.bssid_set && (last_config.sta.channel == 0), "no BSSID, no channel");
}

/* A stronger WPA2 AP and a weaker WPA3 one under one SSID: by signal, not by security */
TEST(Strongest, signal_before_security)
{
    air_reset();
    int wpa3 = air_add("Mesh", 0x21, 1, -75, WIFI_AUTH_WPA3_PSK);
    int wpa2 = air_add("Mesh", 0x22, 6, -40, WIFI_AUTH_WPA2_PSK);
    char ssid[32] = "Mesh";
    char pw[68] = "secret-pw";
    air_attempt_connect(ssid, pw, WIFI_AUTH_WPA2_PSK, NULL, 0);
    CHECK(joined == wpa2, "the stronger WPA2 AP is joined, not the weaker WPA3 one");
    (void)wpa3;
}

/* The module's own walk (ScannedAPs) goes through its scan list in the driver's order;
 * whichever entry of the SSID it is at, the strongest AP of that SSID is the one joined. */
TEST(Strongest, scanned_walk_joins_the_strongest)
{
    three_aps();
    const int order[] = { far_ap, other_ap, mid_ap, near_ap };
    air_to_scan_list(order, 4);
    CHECK(wifi_connect_to_scanned(0) == ESP_OK, "the walk's first entry (the far AP) is tried");
    CHECK(joined == near_ap, "the strongest AP of its SSID is joined");
    wifi_connect_to_scanned(2);
    CHECK(joined == near_ap, "and from the middle entry as well");
}

/* The only AP of the SSID is the weak one: it is still joined */
TEST(Strongest, a_lone_weak_ap)
{
    air_reset();
    int weak = air_add("Far", 0x31, 13, -90, WIFI_AUTH_WPA2_PSK);
    air_add("Home", 0x32, 1, -40, WIFI_AUTH_WPA2_PSK);
    char ssid[32] = "Far";
    char pw[68] = "secret-pw";
    air_attempt_connect(ssid, pw, WIFI_AUTH_WPA2_PSK, NULL, 0);
    CHECK(joined == weak, "the only AP of the SSID is joined");
}

