/*
 * module_request_test.cpp
 *
 * Fix 10: the dispatcher takes a CMD_WIFI_CONNECT only when it holds the whole password
 * (offsetof(password) + 64 = 101 bytes). A shorter one would have the password end in
 * whatever the buffer held from an earlier packet.
 */

#include <stddef.h>
#include <string.h>
#include "module_script.h"

extern "C" void cmd_wifi_connect(command_buf_t *buf);

TEST(Request, connect_holds_the_password)
{
    const int whole = (int)(offsetof(rpc_wifi_connect_req, password) + 64);
    static_assert(offsetof(rpc_wifi_connect_req, password) + 64 == 101, "connect request layout");
    for (int size = whole - 4; size <= whole; size++) {
        command_buf_t request;
        memset(&request, 0xEE, sizeof(request)); // an earlier packet
        rpc_wifi_connect_req *req = (rpc_wifi_connect_req *)request.data;
        memset(req, 0, size);
        req->hdr.command = CMD_WIFI_CONNECT;
        req->auth_mode = WIFI_AUTH_WPA2_PSK;
        strncpy(req->ssid, "Home", 32);
        memset(req->password, 'p', size - (int)offsetof(rpc_wifi_connect_req, password));
        request.size = size;
        air_reset();
        cmd_wifi_connect(&request);
        snprintf(trace, sizeof(trace), "request of %d bytes", size); // shown by a failing check
        ended = "";
        if (size < whole) {
            CHECK(queued_count == 0, "a request without the whole password is refused");
            CHECK(((rpc_espcmd_resp *)request.data)->esp_err == ESP_ERR_INVALID_ARG, "with ESP_ERR_INVALID_ARG");
        } else {
            CHECK(queued_count == 1, "the request with the whole password is taken");
            CHECK(strspn(queued.pw, "p") == 64, "the password as sent");
        }
    }
}
