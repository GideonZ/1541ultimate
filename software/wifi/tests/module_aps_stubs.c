/*
 * module_aps_stubs.c
 *
 * The calls the AP selection cases make into the module's code (module_aps.h). Included at the
 * end of module_stubs.c.
 */

esp_err_t air_attempt_connect(const char *ssid, const char *pw, uint8_t auth, const uint8_t *bssid, uint8_t channel)
{
    return attempt_connect(ssid, pw, auth); // the module takes the picked AP (BSSID, channel) only from fix 5
}
