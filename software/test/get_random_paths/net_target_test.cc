/*
 * net_target_test.cc
 *
 * The UCI network target's GET_RANDOM (NetworkTarget::get_random(), the GET_RANDOM case of
 * parse_command() and the status messages, cut out of network_target.cc as they are) with
 * the real wifi_random.cc behind it, and the module's answer stubbed: which UCI status and
 * which bytes each answer gives, and which command lengths are taken.
 */

#include "net_env.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, what); failures++; } } while (0)

// The module's answer, as wifi_get_random() returns it
static int module_result;
static int asked;
int wifi_get_random(uint8_t *buf, uint16_t len)
{
    asked++;
    if (module_result == 0) {
        memset(buf, 0x5A, len);
    }
    return module_result;
}

NetworkTarget :: NetworkTarget(int id) {}
NetworkTarget :: ~NetworkTarget() {}
void NetworkTarget :: get_more_data(Message **reply, Message **status) {}
void NetworkTarget :: abort(int a) {}
void NetworkTarget :: c64_reset(void) {}

static uint8_t buffer[2048];

static void ask(NetworkTarget &t, uint16_t length, Message **reply, Message **status)
{
    uint8_t cmd[4] = { 0x03, 0x20, (uint8_t)(length & 0xFF), (uint8_t)(length >> 8) };
    Message m = { 4, true, cmd };
    t.data_message.message = buffer;
    t.data_message.length = 0;
    *reply = NULL;
    *status = NULL;
    t.get_random(&m, reply, status);
}

static bool status_is(Message *status, const char *text)
{
    return status && (status->length == (int)strlen(text)) && (memcmp(status->message, text, status->length) == 0);
}

static void answers(void)
{
    NetworkTarget t(0);
    Message *reply, *status;

    module_result = 0;
    ask(t, 16, &reply, &status);
    CHECK(status_is(status, "00,OK"), "bytes: 00,OK");
    CHECK((reply == &t.data_message) && (reply->length == 16) && (buffer[0] == 0x5A), "and the bytes");

    module_result = WIFI_RANDOM_BUSY;
    ask(t, 16, &reply, &status);
    CHECK(status_is(status, "88,ENTROPY SOURCE BUSY"), "busy: 88");
    CHECK(reply == &c_message_empty, "busy carries no bytes");

    module_result = WIFI_RANDOM_REFUSED;
    ask(t, 16, &reply, &status);
    CHECK(status_is(status, "87,NO ENTROPY SOURCE"), "refused: 87");
    CHECK(reply == &c_message_empty, "refused carries no bytes");

    // The length is checked before the module is asked
    asked = 0;
    ask(t, 0, &reply, &status);
    CHECK(status_is(status, "82,PARAMETER(S) OUT OF RANGE") && (asked == 0), "0 bytes: 82, module not asked");
    ask(t, NET_MAX_RANDOM + 1, &reply, &status);
    CHECK(status_is(status, "82,PARAMETER(S) OUT OF RANGE") && (asked == 0), "513 bytes: 82, module not asked");
    module_result = 0;
    ask(t, NET_MAX_RANDOM, &reply, &status);
    CHECK(status_is(status, "00,OK") && (asked == 1), "512 bytes: asked");
}

// The command is exactly four bytes: target, command, length low, length high
static void command_length(void)
{
    NetworkTarget t(0);
    Message *reply, *status;
    for (int len = 2; len <= 6; len++) {
        uint8_t cmd[6] = { 0x03, 0x20, 16, 0, 0, 0 };
        Message m = { len, true, cmd };
        t.data_message.message = buffer;
        asked = 0;
        module_result = 0;
        reply = status = NULL;
        t.parse_command(&m, &reply, &status);
        if (len == 4) {
            CHECK(status_is(status, "00,OK") && (asked == 1), "four bytes: asked, 00,OK");
        } else {
            CHECK(status_is(status, "81,INVALID PARAMS") && (asked == 0) && (reply == &c_message_empty),
                  "any other length: 81, no data, the module not asked");
        }
    }
}

int main(void)
{
    answers();
    command_length();
    if (failures) {
        printf("getrandompaths net_target: %d check(s) failed\n", failures);
        return 1;
    }
    printf("getrandompaths net_target: all checks passed\n");
    return 0;
}
