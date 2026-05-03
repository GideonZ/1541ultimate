#include "http_target.h"
#include "dump_hex.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// these globals will be filled in by the clients
CommandTarget *command_targets[CMD_IF_MAX_TARGET+1];

Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" };
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };

static Message c_cmd_identify      = {  2, true, (uint8_t *)"\x06\x01" };
static Message c_cmd_header_create = { 46, true, (uint8_t *)"\x06\x11\x01""hackerswithstyle.se/leet/search/aql/presets" };
static Message c_cmd_header_add1   = { 28, true, (uint8_t *)"\x06\x13\x00""Accept-encoding: identity" };
static Message c_cmd_header_add2   = { 29, true, (uint8_t *)"\x06\x13\x00""User-Agent: Assembly Query" };
static Message c_cmd_header_add3   = { 22, true, (uint8_t *)"\x06\x13\x00""Client-Id: Ultimate" };
static Message c_cmd_header_add4   = { 20, true, (uint8_t *)"\x06\x13\x00""Connection: close" };
static Message c_cmd_header_query  = { 15, true, (uint8_t *)"\x06\x14\x00""Client-Id" };
static Message c_cmd_header_list1  = {  4, true, (uint8_t *)"\x06\x15\x00\x00" };
static Message c_cmd_header_list2  = {  4, true, (uint8_t *)"\x06\x15\x00\x01" };
static Message c_cmd_header_list3  = {  4, true, (uint8_t *)"\x06\x15\x01\x00" };
static Message c_cmd_header_free   = {  3, true, (uint8_t *)"\x06\x12\x00" };

static Message c_cmd_body_create   = {  3, true, (uint8_t *)"\x06\x21\x02" };
static Message c_cmd_body_add_int  = { 11, true, (uint8_t *)"\x06\x23\x00\x05""width""\xF4\x01" };
static Message c_cmd_body_add_bool = { 12, true, (uint8_t *)"\x06\x24\x00\x07""visible""\x01" };
static Message c_cmd_body_add_str  = { 24, true, (uint8_t *)"\x06\x25\x00\x05""title""\x0e""Commodore Intl" };
static Message c_cmd_body_add_obj  = {  8, true, (uint8_t *)"\x06\x26\x00\x04""user" };
static Message c_cmd_body_add_array= {  8, true, (uint8_t *)"\x06\x27\x00\x04""cars" };
static Message c_cmd_body_add_car1 = {  9, true, (uint8_t *)"\x06\x25\x00\x00\x04""Opel" };
static Message c_cmd_body_add_car2 = { 12, true, (uint8_t *)"\x06\x25\x00\x00\x07""Renault" };
static Message c_cmd_body_add_car3 = { 11, true, (uint8_t *)"\x06\x25\x00\x00\x06""Toyota" };
static Message c_cmd_body_add_car4 = { 15, true, (uint8_t *)"\x06\x25\x00\x00\x0a""Mitsubishi" };
static Message c_cmd_body_add_car5 = { 13, true, (uint8_t *)"\x06\x25\x00\x00\x08""Mercedes" };
static Message c_cmd_body_up       = {  3, true, (uint8_t *)"\x06\x28\x00" };
static Message c_cmd_body_add_bool2= { 10, true, (uint8_t *)"\x06\x24\x00\x05""close""\x01" };
static Message c_cmd_body_query1   = { 14, true, (uint8_t *)"\x06\x2A\x00""user/cars%2" };
static Message c_cmd_body_queryall1= {  3, true, (uint8_t *)"\x06\x2A\x00" };

static Message c_cmd_body_create2  = {  3, true, (uint8_t *)"\x06\x21\x03" };
static Message c_cmd_body2_add_1   = {  4, true, (uint8_t *)"\x06\x26\x01\x00" };
static Message c_cmd_body2_add_2   = { 14, true, (uint8_t *)"\x06\x25\x01\x04""user""\x05""1541u" };
static Message c_cmd_body2_add_3   = { 15, true, (uint8_t *)"\x06\x25\x01\x04""name""\x06""Gideon" };
static Message c_cmd_body2_up_1    = {  3, true, (uint8_t *)"\x06\x28\x01" };
static Message c_cmd_body2_add_4   = {  4, true, (uint8_t *)"\x06\x26\x01\x00" };
static Message c_cmd_body2_add_5   = { 16, true, (uint8_t *)"\x06\x25\x01\x04""user""\x07""bvl1999" };
static Message c_cmd_body2_add_6   = { 13, true, (uint8_t *)"\x06\x25\x01\x04""name""\x04""Bart" };
static Message c_cmd_body2_up_2    = {  3, true, (uint8_t *)"\x06\x28\x01" };
static Message c_cmd_body2_query1  = { 10, true, (uint8_t *)"\x06\x2A\x01""%1/name" };
static Message c_cmd_body2_add_7   = {  4, true, (uint8_t *)"\x06\x27\x01\x00" };
static Message c_cmd_body2_add_8   = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xEB" };
static Message c_cmd_body2_add_9   = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xEC" };
static Message c_cmd_body2_add_10  = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xED" };
static Message c_cmd_body2_query2  = {  8, true, (uint8_t *)"\x06\x2A\x01""%2/%1" };
static Message c_cmd_body2_queryall= {  3, true, (uint8_t *)"\x06\x2A\x01" };

static Message c_cmd_body_free_00  = {  3, true, (uint8_t *)"\x06\x22\x00" };
static Message c_cmd_body_free_01  = {  3, true, (uint8_t *)"\x06\x22\x01" };
static Message c_exchange          = {  4, true, (uint8_t *)"\x06\x31\x00\xFF" };
static Message c_exchange_raw      = {  4, true, (uint8_t *)"\x06\x32\x00\xFF" };

static const char status_ok[]      = "000 OK";
static const char status_bad_cmd[] = "400 BAD COMMAND";
static const char status_no_more[] = "500 NO MORE DATA";

static int checks = 0;
static int failures = 0;
static int saved_stdout = -1;

static void quiet_begin(void)
{
    fflush(stdout);
    saved_stdout = dup(fileno(stdout));
    int nullfd = open("/dev/null", O_WRONLY);
    if ((saved_stdout >= 0) && (nullfd >= 0)) {
        dup2(nullfd, fileno(stdout));
    }
    if (nullfd >= 0) {
        close(nullfd);
    }
}

static void quiet_end(void)
{
    if (saved_stdout >= 0) {
        fflush(stdout);
        dup2(saved_stdout, fileno(stdout));
        close(saved_stdout);
        saved_stdout = -1;
    }
}

static void dump_bytes(const char *name, const uint8_t *data, int length)
{
    printf("%s (%d bytes):\n", name, length);
    dump_hex_relative((void *)data, length);
}

static bool same_bytes(const uint8_t *a, int alen, const uint8_t *b, int blen)
{
    return (alen == blen) && ((alen == 0) || (memcmp(a, b, alen) == 0));
}

static void expect_bytes(const char *label, const char *channel, Message *msg, const uint8_t *expected, int expected_len)
{
    checks++;
    if (same_bytes(msg->message, msg->length, expected, expected_len)) {
        return;
    }

    failures++;
    printf("FAIL %s %s\n", label, channel);
    dump_bytes("expected", expected, expected_len);
    dump_bytes("actual", msg->message, msg->length);
}

static void expect_text(const char *label, const char *channel, Message *msg, const char *expected)
{
    expect_bytes(label, channel, msg, (const uint8_t *)expected, strlen(expected));
}

static void expect_empty(const char *label, const char *channel, Message *msg)
{
    expect_bytes(label, channel, msg, (const uint8_t *)"", 0);
}

static void expect_last(const char *label, Message *reply, bool expected)
{
    checks++;
    if (reply->last_part == expected) {
        return;
    }

    failures++;
    printf("FAIL %s last_part expected %d actual %d\n", label, expected ? 1 : 0, reply->last_part ? 1 : 0);
}

static void run_expect(HttpTarget *target, const char *label, Message *command,
                       const uint8_t *expected_reply, int expected_reply_len,
                       const char *expected_status)
{
    Message *reply;
    Message *status;

    quiet_begin();
    target->parse_command(command, &reply, &status);
    quiet_end();
    expect_bytes(label, "reply", reply, expected_reply, expected_reply_len);
    expect_text(label, "status", status, expected_status);
    expect_last(label, reply, true);
}

static void run_expect_empty(HttpTarget *target, const char *label, Message *command, const char *expected_status)
{
    run_expect(target, label, command, (const uint8_t *)"", 0, expected_status);
}

static void run_more_expect(HttpTarget *target, const char *label,
                            const uint8_t *expected_reply, int expected_reply_len,
                            const char *expected_status)
{
    Message *reply;
    Message *status;

    quiet_begin();
    target->get_more_data(&reply, &status);
    quiet_end();
    expect_bytes(label, "reply", reply, expected_reply, expected_reply_len);
    expect_text(label, "status", status, expected_status);
    expect_last(label, reply, true);
}

static Message make_msg(uint8_t *data, int length)
{
    Message msg = { length, true, data };
    return msg;
}

static FILE *open_nested_json(void)
{
    FILE *fi = fopen("nested.json", "r");
    if (fi) {
        return fi;
    }
    return fopen("../../../target/pc/linux/test_http_target/nested.json", "r");
}

static void test_headers(HttpTarget *target)
{
    static const uint8_t handle0[] = { 0x00 };
    static const char all_headers[] =
        "Host: hackerswithstyle.se\r"
        "Accept-encoding: identity\r"
        "User-Agent: Assembly Query\r"
        "Client-Id: Ultimate\r"
        "Connection: close\r";
    static const char first_header[] = "Host: hackerswithstyle.se\r";

    run_expect(target, "identify", &c_cmd_identify, (const uint8_t *)"ULTIMATE HTTP TARGET V1.0", 25, status_ok);
    run_expect(target, "header create", &c_cmd_header_create, handle0, sizeof(handle0), status_ok);
    run_expect_empty(target, "header add accept", &c_cmd_header_add1, status_ok);
    run_expect_empty(target, "header add agent", &c_cmd_header_add2, status_ok);
    run_expect_empty(target, "header add client", &c_cmd_header_add3, status_ok);
    run_expect_empty(target, "header add connection", &c_cmd_header_add4, status_ok);
    run_expect(target, "header query", &c_cmd_header_query, (const uint8_t *)"Ultimate", 8, status_ok);
    run_expect(target, "header list all", &c_cmd_header_list1, (const uint8_t *)all_headers, strlen(all_headers), status_ok);
    run_expect(target, "header list first", &c_cmd_header_list2, (const uint8_t *)first_header, strlen(first_header), status_ok);
    run_expect_empty(target, "header list invalid handle", &c_cmd_header_list3, status_bad_cmd);
}

static void test_legacy_body_object(HttpTarget *target)
{
    static const uint8_t handle0[] = { 0x00 };
    static const uint8_t toyota[] = { HTTP_DATA_STRING, 0x06, 'T', 'o', 'y', 'o', 't', 'a' };
    static const uint8_t root_object[] = {
        HTTP_DATA_OBJECT, 0x04,
        0x05, 'w', 'i', 'd', 't', 'h',
            HTTP_DATA_INTEGER, 0xF4, 0x01, 0x00, 0x00,
        0x07, 'v', 'i', 's', 'i', 'b', 'l', 'e',
            HTTP_DATA_BOOL, 0x01,
        0x05, 't', 'i', 't', 'l', 'e',
            HTTP_DATA_STRING, 0x0E, 'C', 'o', 'm', 'm', 'o', 'd', 'o', 'r', 'e', ' ', 'I', 'n', 't', 'l',
        0x04, 'u', 's', 'e', 'r',
            HTTP_DATA_OBJECT, 0x02,
            0x04, 'c', 'a', 'r', 's',
                HTTP_DATA_ARRAY, 0x05,
                HTTP_DATA_STRING, 0x04, 'O', 'p', 'e', 'l',
                HTTP_DATA_STRING, 0x07, 'R', 'e', 'n', 'a', 'u', 'l', 't',
                HTTP_DATA_STRING, 0x06, 'T', 'o', 'y', 'o', 't', 'a',
                HTTP_DATA_STRING, 0x0A, 'M', 'i', 't', 's', 'u', 'b', 'i', 's', 'h', 'i',
                HTTP_DATA_STRING, 0x08, 'M', 'e', 'r', 'c', 'e', 'd', 'e', 's',
            0x05, 'c', 'l', 'o', 's', 'e',
                HTTP_DATA_BOOL, 0x01
    };

    run_expect(target, "body object create", &c_cmd_body_create, handle0, sizeof(handle0), status_ok);
    run_expect_empty(target, "body object add int", &c_cmd_body_add_int, status_ok);
    run_expect_empty(target, "body object add bool", &c_cmd_body_add_bool, status_ok);
    run_expect_empty(target, "body object add string", &c_cmd_body_add_str, status_ok);
    run_expect_empty(target, "body object add object", &c_cmd_body_add_obj, status_ok);
    run_expect_empty(target, "body object add array", &c_cmd_body_add_array, status_ok);
    run_expect_empty(target, "body object add car1", &c_cmd_body_add_car1, status_ok);
    run_expect_empty(target, "body object add car2", &c_cmd_body_add_car2, status_ok);
    run_expect_empty(target, "body object add car3", &c_cmd_body_add_car3, status_ok);
    run_expect_empty(target, "body object add car4", &c_cmd_body_add_car4, status_ok);
    run_expect_empty(target, "body object add car5", &c_cmd_body_add_car5, status_ok);
    run_expect_empty(target, "body object up", &c_cmd_body_up, status_ok);
    run_expect_empty(target, "body object add close", &c_cmd_body_add_bool2, status_ok);
    run_expect(target, "body object query nested string", &c_cmd_body_query1, toyota, sizeof(toyota), status_ok);
    run_expect(target, "body object query root", &c_cmd_body_queryall1, root_object, sizeof(root_object), status_ok);
    run_more_expect(target, "body object no more", (const uint8_t *)"", 0, status_no_more);
}

static void test_legacy_body_array(HttpTarget *target)
{
    static const uint8_t handle1[] = { 0x01 };
    static const uint8_t bart[] = { HTTP_DATA_STRING, 0x04, 'B', 'a', 'r', 't' };
    static const uint8_t minus20[] = { HTTP_DATA_INTEGER, 0xEC, 0xFF, 0xFF, 0xFF };
    static const uint8_t full_array[] = {
        HTTP_DATA_ARRAY, 0x03,
        HTTP_DATA_OBJECT, 0x02,
            0x04, 'u', 's', 'e', 'r',
                HTTP_DATA_STRING, 0x05, '1', '5', '4', '1', 'u',
            0x04, 'n', 'a', 'm', 'e',
                HTTP_DATA_STRING, 0x06, 'G', 'i', 'd', 'e', 'o', 'n',
        HTTP_DATA_OBJECT, 0x02,
            0x04, 'u', 's', 'e', 'r',
                HTTP_DATA_STRING, 0x07, 'b', 'v', 'l', '1', '9', '9', '9',
            0x04, 'n', 'a', 'm', 'e',
                HTTP_DATA_STRING, 0x04, 'B', 'a', 'r', 't',
        HTTP_DATA_ARRAY, 0x03,
            HTTP_DATA_INTEGER, 0xEB, 0xFF, 0xFF, 0xFF,
            HTTP_DATA_INTEGER, 0xEC, 0xFF, 0xFF, 0xFF,
            HTTP_DATA_INTEGER, 0xED, 0xFF, 0xFF, 0xFF
    };

    run_expect(target, "body array create", &c_cmd_body_create2, handle1, sizeof(handle1), status_ok);
    run_expect_empty(target, "body array add object 1", &c_cmd_body2_add_1, status_ok);
    run_expect_empty(target, "body array add user 1", &c_cmd_body2_add_2, status_ok);
    run_expect_empty(target, "body array add name 1", &c_cmd_body2_add_3, status_ok);
    run_expect_empty(target, "body array up 1", &c_cmd_body2_up_1, status_ok);
    run_expect_empty(target, "body array add object 2", &c_cmd_body2_add_4, status_ok);
    run_expect_empty(target, "body array add user 2", &c_cmd_body2_add_5, status_ok);
    run_expect_empty(target, "body array add name 2", &c_cmd_body2_add_6, status_ok);
    run_expect_empty(target, "body array up 2", &c_cmd_body2_up_2, status_ok);
    run_expect(target, "body array query bart", &c_cmd_body2_query1, bart, sizeof(bart), status_ok);
    run_expect_empty(target, "body array add nested array", &c_cmd_body2_add_7, status_ok);
    run_expect_empty(target, "body array add -21", &c_cmd_body2_add_8, status_ok);
    run_expect_empty(target, "body array add -20", &c_cmd_body2_add_9, status_ok);
    run_expect_empty(target, "body array add -19", &c_cmd_body2_add_10, status_ok);
    run_expect(target, "body array query -20", &c_cmd_body2_query2, minus20, sizeof(minus20), status_ok);
    run_expect(target, "body array query all", &c_cmd_body2_queryall, full_array, sizeof(full_array), status_ok);
    run_more_expect(target, "body array no more", (const uint8_t *)"", 0, status_no_more);
}

static void test_structured_body_add(HttpTarget *target)
{
    static uint8_t add_object_data[] = {
        0x06, HTTP_CMD_BODY_ADD, 0x00,
        0x05, 'w', 'i', 'd', 't', 'h',
            HTTP_DATA_INTEGER, 0xF4, 0x01, 0x00, 0x00,
        0x07, 'v', 'i', 's', 'i', 'b', 'l', 'e',
            HTTP_DATA_BOOL, 0x01,
        0x04, 'u', 's', 'e', 'r',
            HTTP_DATA_OBJECT, 0x01,
            0x04, 'n', 'a', 'm', 'e',
                HTTP_DATA_STRING, 0x04, 'P', 'e', 'r', 'i'
    };
    static uint8_t query_peri_data[] = { 0x06, HTTP_CMD_BODY_QUERY, 0x00, 'u', 's', 'e', 'r', '/', 'n', 'a', 'm', 'e' };
    static uint8_t create_array_data[] = { 0x06, HTTP_CMD_BODY_CREATE, HTTP_TYPE_JSON_ARRAY };
    static uint8_t add_array_data[] = {
        0x06, HTTP_CMD_BODY_ADD, 0x01,
        HTTP_DATA_OBJECT, 0x02,
            0x04, 'u', 's', 'e', 'r',
                HTTP_DATA_STRING, 0x05, '1', '5', '4', '1', 'u',
            0x04, 'n', 'a', 'm', 'e',
                HTTP_DATA_STRING, 0x06, 'G', 'i', 'd', 'e', 'o', 'n',
        HTTP_DATA_OBJECT, 0x02,
            0x04, 'u', 's', 'e', 'r',
                HTTP_DATA_STRING, 0x07, 'b', 'v', 'l', '1', '9', '9', '9',
            0x04, 'n', 'a', 'm', 'e',
                HTTP_DATA_STRING, 0x04, 'B', 'a', 'r', 't'
    };
    static uint8_t query_bart_data[] = { 0x06, HTTP_CMD_BODY_QUERY, 0x01, '%', '1', '/', 'n', 'a', 'm', 'e' };
    static const uint8_t handle0[] = { 0x00 };
    static const uint8_t handle1[] = { 0x01 };
    static const uint8_t peri[] = { HTTP_DATA_STRING, 0x04, 'P', 'e', 'r', 'i' };
    static const uint8_t bart[] = { HTTP_DATA_STRING, 0x04, 'B', 'a', 'r', 't' };

    Message add_object = make_msg(add_object_data, sizeof(add_object_data));
    Message query_peri = make_msg(query_peri_data, sizeof(query_peri_data));
    Message create_array = make_msg(create_array_data, sizeof(create_array_data));
    Message add_array = make_msg(add_array_data, sizeof(add_array_data));
    Message query_bart = make_msg(query_bart_data, sizeof(query_bart_data));

    run_expect(target, "structured body object create", &c_cmd_body_create, handle0, sizeof(handle0), status_ok);
    run_expect_empty(target, "structured body object add", &add_object, status_ok);
    run_expect(target, "structured body object query", &query_peri, peri, sizeof(peri), status_ok);

    run_expect(target, "structured body array create", &create_array, handle1, sizeof(handle1), status_ok);
    run_expect_empty(target, "structured body array add", &add_array, status_ok);
    run_expect(target, "structured body array query", &query_bart, bart, sizeof(bart), status_ok);
}

static void query_external(HttpTarget *target, const char *label, uint8_t handle,
                           const char *path, const uint8_t *expected, int expected_len)
{
    uint8_t buffer[128];
    int path_len = strlen(path);
    buffer[0] = 0x06;
    buffer[1] = HTTP_CMD_BODY_QUERY;
    buffer[2] = handle;
    memcpy(buffer + 3, path, path_len);
    Message command = make_msg(buffer, path_len + 3);
    run_expect(target, label, &command, expected, expected_len, status_ok);
}

static void command_external_empty(HttpTarget *target, const char *label, uint8_t cmd, uint8_t handle,
                                   const char *path, const char *expected_status)
{
    uint8_t buffer[128];
    int path_len = strlen(path);
    buffer[0] = 0x06;
    buffer[1] = cmd;
    buffer[2] = handle;
    memcpy(buffer + 3, path, path_len);
    Message command = make_msg(buffer, path_len + 3);
    run_expect_empty(target, label, &command, expected_status);
}

static void add_external_name(HttpTarget *target, uint8_t handle)
{
    uint8_t buffer[] = { 0x06, HTTP_CMD_BODY_ADD_STRING, handle, 0x04, 'n', 'a', 'm', 'e', 0x06, 'G', 'i', 'd', 'e', 'o', 'n' };
    Message command = make_msg(buffer, sizeof(buffer));
    run_expect_empty(target, "external add name", &command, status_ok);
}

static void test_external_json(HttpTarget *target)
{
    static const uint8_t chocolate[] = { HTTP_DATA_STRING, 0x09, 'C', 'h', 'o', 'c', 'o', 'l', 'a', 't', 'e' };
    static const uint8_t gideon[] = { HTTP_DATA_STRING, 0x06, 'G', 'i', 'd', 'e', 'o', 'n' };

    FILE *fi = open_nested_json();
    checks++;
    if (!fi) {
        failures++;
        printf("FAIL external json open nested.json\n");
        return;
    }

    char *json_body = new char[4096];
    int size = fread(json_body, 1, 4096, fi);
    fclose(fi);

    uint8_t handle = 0xFF;
    quiet_begin();
    int tokens = target->create_body_from_json(json_body, size, &handle);
    quiet_end();
    delete[] json_body;

    checks++;
    if ((tokens < 0) || (handle >= MAX_HTTP_HANDLES)) {
        failures++;
        printf("FAIL external json create body tokens=%d handle=%d\n", tokens, handle);
        return;
    }

    query_external(target, "external query chocolate 1", handle, "%1/topping%3/type", chocolate, sizeof(chocolate));
    query_external(target, "external query chocolate 2", handle, "%1/topping/%3/type", chocolate, sizeof(chocolate));
    query_external(target, "external query chocolate 3", handle, "%2/batters/batter/%1/type", chocolate, sizeof(chocolate));
    query_external(target, "external query chocolate 4", handle, "%2/batters/batter[1]/type", chocolate, sizeof(chocolate));
    command_external_empty(target, "external remove blueberry type", HTTP_CMD_BODY_REMOVE, handle, "%0/batters/batter[2]/type", status_ok);
    command_external_empty(target, "external remove blueberry entry", HTTP_CMD_BODY_REMOVE, handle, "%0/batters/batter[2]", status_ok);
    command_external_empty(target, "external move batters", HTTP_CMD_BODY_MOVE, handle, "%1/batters", status_ok);
    add_external_name(target, handle);
    query_external(target, "external query added name", handle, "%1/batters/name", gideon, sizeof(gideon));
    command_external_empty(target, "external delete object", HTTP_CMD_BODY_REMOVE, handle, "%1", status_ok);
}

static void run_network_smoke(HttpTarget *target)
{
    Message *reply;
    Message *status;

    printf("Running optional network smoke test.\n");
    target->parse_command(&c_exchange_raw, &reply, &status);
    printf("raw exchange status: %.*s\n", status->length, status->message);
    while(!reply->last_part) {
        target->get_more_data(&reply, &status);
    }

    target->parse_command(&c_exchange, &reply, &status);
    printf("object exchange status: %.*s\n", status->length, status->message);
}

int main(int argc, char **argv)
{
    HttpTarget *target = (HttpTarget *)command_targets[6];
    if (!target) {
        printf("FAIL target 6 was not registered\n");
        return 1;
    }

    test_headers(target);
    test_legacy_body_object(target);
    test_legacy_body_array(target);
    run_expect_empty(target, "free legacy body 0", &c_cmd_body_free_00, status_ok);
    run_expect_empty(target, "free legacy body 1", &c_cmd_body_free_01, status_ok);
    test_structured_body_add(target);
    test_external_json(target);

    if ((argc > 1) && (strcmp(argv[1], "--network") == 0)) {
        run_network_smoke(target);
    } else {
        printf("Skipping optional network exchange smoke test. Use --network to run it.\n");
    }

    if (failures) {
        printf("FAIL: %d of %d checks failed\n", failures, checks);
        return 1;
    }

    printf("PASS: %d checks\n", checks);
    return 0;
}

// stubs
void outbyte(int b)
{
    fputc(b, stdout);
}
