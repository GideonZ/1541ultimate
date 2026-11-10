/*
 * pick_task_test.cpp
 *
 * The AP the user picks from the list is the AP the connect request names: the BSSID and
 * the channel of its scan record go to the module (through the real getAccessPointItems,
 * connect_ap and wifi_wifi_connect), and a connect by SSID alone (the manual entry) sends
 * them as zero. The fields a module that predates them reads stay where they were.
 */

#include "wifi_task_test.h"

/* The request as it was before the BSSID was added */
typedef struct {
    rpc_header_t hdr;
    uint8_t auth_mode;
    char ssid[32];
    char password[64];
} old_wifi_connect_req;

static const uint8_t zero[6] = { 0 };

TEST(WifiPickTask, request_layout_is_the_old_one_then_the_bssid)
{
    EXPECT_EQ(offsetof(rpc_wifi_connect_req, auth_mode), offsetof(old_wifi_connect_req, auth_mode));
    EXPECT_EQ(offsetof(rpc_wifi_connect_req, ssid), offsetof(old_wifi_connect_req, ssid));
    EXPECT_EQ(offsetof(rpc_wifi_connect_req, password), offsetof(old_wifi_connect_req, password));
    EXPECT_TRUE(offsetof(rpc_wifi_connect_req, bssid) >= offsetof(old_wifi_connect_req, password) + 64);
    // a module tells the new request from the old one by its size
    EXPECT_TRUE(sizeof(rpc_wifi_connect_req) > sizeof(old_wifi_connect_req));
}

TEST(WifiPickTask, pick_sends_the_bssid_and_the_channel)
{
    reset_world();
    world.enabled = true;
    world.pick_auth = 3;
    world.password = "secret-pw";
    boot({ EVENT_CONNECTED, pick });
    const rpc_wifi_connect_req *req = (const rpc_wifi_connect_req *)request.data;
    CHECK(requests == 1, "one request");
    CHECK(request.size == (int)sizeof(rpc_wifi_connect_req), "the full request is sent");
    CHECK(req->hdr.command == CMD_WIFI_CONNECT, "as CMD_WIFI_CONNECT");
    char ssid[32] = "HomeNet", password[64] = "secret-pw"; // zero padded, to the end of the fields
    CHECK(memcmp(req->ssid, ssid, 32) == 0, "the SSID, zero padded");
    CHECK(memcmp(req->password, password, 64) == 0, "the password, zero padded");
    CHECK(req->auth_mode == 3, "the auth mode");
    CHECK(memcmp(req->bssid, world.pick_bssid, 6) == 0, "the picked BSSID");
    CHECK(req->channel == 6, "its channel");
}

// Several APs share an SSID: the one picked from the list, with its own BSSID and channel
TEST(WifiPickTask, pick_from_the_list_names_that_ap)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    wifi_aps.num_records = 2;
    for (int i = 0; i < 2; i++) {
        memset(&wifi_aps.aps[i], 0, sizeof(wifi_aps.aps[i]));
        strcpy((char *)wifi_aps.aps[i].ssid, "Shared");
        wifi_aps.aps[i].bssid[5] = 0x10 + i;
        wifi_aps.aps[i].channel = 1 + 5 * i;
    }
    IndexedList<Browsable *> list(0, NULL);
    CHECK(wifi.getAccessPointItems(NULL, list) == 2, "both are listed");
    SubsysCommand cmd;
    cmd.functionID = (intptr_t)list[1];
    cmd.user_interface = NULL;
    BrowsableWifiAP :: connect_ap(&cmd);
    const rpc_wifi_connect_req *req = (const rpc_wifi_connect_req *)request.data;
    CHECK(requests == 1, "one request");
    CHECK(req->bssid[5] == 0x11 && req->channel == 6, "the second one, not the first with that SSID");
}

TEST(WifiPickTask, ssid_only_sends_zeros)
{
    reset_world();
    wifi_wifi_connect("Home", "secret-pw", 3);
    const rpc_wifi_connect_req *req = (const rpc_wifi_connect_req *)request.data;
    CHECK(requests == 1, "one request");
    CHECK(memcmp(req->bssid, zero, 6) == 0, "no BSSID: all zero");
    CHECK(req->channel == 0, "no channel");
    CHECK(strncmp(req->ssid, "Home", 32) == 0, "the SSID");
}

// The longest password the box takes, 64 characters, goes out whole
TEST(WifiPickTask, longest_password_is_sent_whole)
{
    reset_world();
    world.enabled = true;
    world.pick_auth = 3;
    char pw[65];
    memset(pw, 'p', 64);
    pw[64] = 0;
    world.password = pw;
    boot({ EVENT_CONNECTED, pick });
    const rpc_wifi_connect_req *req = (const rpc_wifi_connect_req *)request.data;
    CHECK(memcmp(req->password, pw, 64) == 0, "all 64 characters");
}
