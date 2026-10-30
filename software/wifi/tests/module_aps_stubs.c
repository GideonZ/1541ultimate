/*
 * module_aps_stubs.c
 *
 * The calls the AP selection cases make into the module's code (module_aps.h). Included at the
 * end of module_stubs.c.
 */

esp_err_t air_attempt_connect(const char *ssid, const char *pw, uint8_t auth, const uint8_t *bssid, uint8_t channel)
{
    return attempt_connect(ssid, pw, auth, bssid, channel);
}

// The dispatcher, then the connector, as for a request from the Ultimate
int air_connect_request(command_buf_t *request)
{
    ConnectState_t state = Disconnected;
    queued_count = 0;
    cmd_wifi_connect(request);
    if (queued_count != 1) {
        return 0;
    }
    handle_connect_command(&queued, &state);
    return state == Connected;
}
