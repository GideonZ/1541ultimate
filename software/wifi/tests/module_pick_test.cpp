/*
 * module_pick_test.cpp
 *
 * Fix 5: the AP the user picked from the list. The dispatcher's cmd_wifi_connect() and the
 * connector's handle_connect_command() are the module's own; the request is built as the
 * application builds it (wifi_wifi_connect, checked byte by byte in software/io/wifi/tests).
 */

#include <string.h>
#include "module_aps.h"

/* The request as an application that predates the BSSID sends it */
typedef struct {
    rpc_header_t hdr;
    uint8_t auth_mode;
    char ssid[32];
    char password[64];
} old_wifi_connect_req;

static command_buf_t request;

/* A CMD_WIFI_CONNECT of `size` bytes. Whatever lies beyond it is the far AP's BSSID and
 * channel, as left in the buffer by an earlier packet: a module must not read it. */
static void make_request(int size, const char *ssid, const uint8_t *bssid, uint8_t channel)
{
    memset(&request, 0, sizeof(request));
    rpc_wifi_connect_req *req = (rpc_wifi_connect_req *)request.data;
    req->hdr.command = CMD_WIFI_CONNECT;
    req->auth_mode = WIFI_AUTH_WPA2_PSK;
    strncpy(req->ssid, ssid, 32);
    strncpy(req->password, "secret-pw", 64);
    memcpy(req->bssid, air[far_ap].bssid, 6);
    req->channel = air[far_ap].channel;
    if (size >= (int)sizeof(rpc_wifi_connect_req)) {
        memcpy(req->bssid, bssid, 6);
        req->channel = channel;
    }
    request.size = size;
}

/* The dispatcher, then the connector, as for a request from the Ultimate */
static int connect_request(void)
{
    return air_connect_request(&request);
}

TEST(Pick, picked_ap_is_joined)
{
    three_aps();
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[far_ap].bssid, air[far_ap].channel);
    CHECK(connect_request(), "the connect succeeds");
    CHECK(joined == far_ap, "the AP picked is joined, not the strongest");
    CHECK(last_config.sta.bssid_set && (memcmp(last_config.sta.bssid, air[far_ap].bssid, 6) == 0), "its BSSID is set");
    CHECK(last_config.sta.channel == 1, "its channel is scanned first");
    CHECK(transmitted == 1, "the Ultimate gets its answer");
}

TEST(Pick, picked_mid_ap_is_joined)
{
    three_aps();
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[mid_ap].bssid, air[mid_ap].channel);
    connect_request();
    CHECK(joined == mid_ap, "the AP picked on channel 11 is joined");
}

TEST(Pick, old_application_joins_the_strongest)
{
    three_aps();
    make_request(sizeof(old_wifi_connect_req), "Home", NULL, 0);
    CHECK(connect_request(), "the shorter request is taken");
    CHECK(strncmp(queued.ssid, "Home", 32) == 0 && strncmp(queued.pw, "secret-pw", 64) == 0, "with its SSID and password");
    CHECK(!last_config.sta.bssid_set, "the bytes beyond it are not read as a BSSID");
    CHECK(joined == near_ap, "the strongest AP of the SSID is joined");
}

TEST(Pick, no_bssid_joins_the_strongest)
{
    three_aps();
    const uint8_t zero[6] = { 0 };
    make_request(sizeof(rpc_wifi_connect_req), "Home", zero, 0);
    connect_request();
    CHECK(!last_config.sta.bssid_set && (last_config.sta.channel == 0), "an all zero BSSID sets none");
    CHECK(joined == near_ap, "the strongest AP of the SSID is joined");
}

TEST(Pick, picked_ap_gone)
{
    three_aps();
    const uint8_t gone[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x77 };
    make_request(sizeof(rpc_wifi_connect_req), "Home", gone, 6);
    CHECK(!connect_request(), "the connect fails");
    CHECK(joined < 0, "no other AP is joined in its place");
}

/* The pick is for that request: the module's own walk afterwards takes the strongest */
TEST(Pick, walk_after_a_pick)
{
    three_aps();
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[far_ap].bssid, air[far_ap].channel);
    connect_request();
    const int order[] = { far_ap, mid_ap, near_ap };
    air_to_scan_list(order, 3);
    wifi_connect_to_scanned(0);
    CHECK(!last_config.sta.bssid_set, "the walk sets no BSSID");
    CHECK(joined == near_ap, "and joins the strongest");
}

// The channels the module sets: 13 is the last one, 14 is none
TEST(Pick, picked_ap_on_channel_13)
{
    three_aps();
    int ch13 = air_add("Home", 0x0D, 13, -90, WIFI_AUTH_WPA2_PSK);
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[ch13].bssid, 13);
    CHECK(connect_request(), "the connect succeeds");
    CHECK(joined == ch13, "the AP on channel 13 is joined");
    CHECK(last_config.sta.channel == 13, "its channel is scanned first");
}

TEST(Pick, channel_14_is_not_set)
{
    three_aps();
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[mid_ap].bssid, 14);
    connect_request();
    CHECK(joined == mid_ap, "the picked AP is joined by its BSSID");
    CHECK(last_config.sta.channel == 0, "a channel the module does not set is left out");
}

/* cmd_wifi_connect() of a module that predates the BSSID, as on master 60f16dc15
 * (raw_u64 rpc_dispatch.c), against which the new application's request must still work */
static void old_module_cmd_wifi_connect(command_buf_t *buf)
{
    rpc_wifi_connect_req *param = (rpc_wifi_connect_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    if (buf->size < 98) {
        resp->esp_err = ESP_ERR_INVALID_ARG;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = CMD_WIFI_CONNECT;
    ev.auth_mode = param->auth_mode;
    memcpy(&ev.ssid, param->ssid, 32);
    memcpy(&ev.pw, param->password, 64);

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }
    xSemaphoreGive(connect_semaphore);
}

TEST(Pick, old_module_takes_the_new_request)
{
    three_aps();
    make_request(sizeof(rpc_wifi_connect_req), "Home", air[far_ap].bssid, air[far_ap].channel);
    old_module_cmd_wifi_connect(&request);
    CHECK(queued_count == 1 && transmitted == 0, "the longer request is taken, not refused");
    CHECK(strncmp(queued.ssid, "Home", 32) == 0, "the SSID where it reads it");
    CHECK(strncmp(queued.pw, "secret-pw", 64) == 0, "the password where it reads it");
    CHECK(queued.auth_mode == WIFI_AUTH_WPA2_PSK, "the auth mode where it reads it");
}

