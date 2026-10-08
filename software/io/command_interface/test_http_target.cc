#include "http_target.h"
#include "dump_hex.h"
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

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

static Message c_cmd_free_all      = {  2, true, (uint8_t *)"\x06\x10" };

static const char status_ok[]      = "000 OK";
static const char status_bad_cmd[] = "400 BAD COMMAND";
static const char status_bad_req[] = "400 BAD REQUEST";
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

static void test_body_removal(HttpTarget *target)
{
    static const uint8_t gideon[] = { HTTP_DATA_STRING, 6, 'G', 'i', 'd', 'e', 'o', 'n' };
    const char *removed[] = { "branch/child", "branch", "sibling", "items[0]" };
    const char *selected[] = { "branch/child", "branch/child", "branch/child", "items[0]" };
    const char *destination[] = { "branch/name", "name", "branch/child/name", "items[0]/name" };
    for (unsigned i = 0; i < sizeof(removed) / sizeof(removed[0]); ++i) {
        char json[] = "{\"branch\":{\"child\":{}},\"sibling\":{},\"items\":[{}]}";
        uint8_t handle = 0xFF;
        target->create_body_from_json(json, sizeof(json) - 1, &handle);
        command_external_empty(target, "select editing position", HTTP_CMD_BODY_MOVE, handle, selected[i], status_ok);
        command_external_empty(target, "delete selected node, ancestor or sibling", HTTP_CMD_BODY_REMOVE, handle, removed[i], status_ok);
        if (i == 3) {
            // Deleting an array element should leave its containing array selected.
            uint8_t data[] = { 6, HTTP_CMD_BODY_ADD_OBJECT, handle, 0 };
            Message command = make_msg(data, sizeof(data));
            run_expect_empty(target, "append after deleting array selection", &command, status_ok);
        }
        add_external_name(target, handle);
        query_external(target, "edit after deletion reaches surviving parent", handle, destination[i], gideon, sizeof(gideon));
        command_external_empty(target, "free navigation body", HTTP_CMD_BODY_FREE, handle, "", status_ok);
    }
}

static void test_free_all(HttpTarget *target)
{
    // After test_external_json: headers[0] and bodies[0..2] are all allocated.
    // FREE_ALL must release all of them.
    static const uint8_t handle0[] = { 0x00 };
    static const uint8_t handle1[] = { 0x01 };

    run_expect_empty(target, "free all",              &c_cmd_free_all,       status_ok);
    // All slots freed: next allocations start from handle 0.
    run_expect(target, "free all: header create",     &c_cmd_header_create,  handle0, sizeof(handle0), status_ok);
    run_expect(target, "free all: body create",       &c_cmd_body_create,    handle0, sizeof(handle0), status_ok);
    run_expect(target, "free all: second header",     &c_cmd_header_create,  handle1, sizeof(handle1), status_ok);
    // FREE_ALL on already-allocated slots must also return OK.
    run_expect_empty(target, "free all: second call", &c_cmd_free_all,       status_ok);
    // All slots freed again: first allocation returns handle 0.
    run_expect(target, "free all: confirm header",    &c_cmd_header_create,  handle0, sizeof(handle0), status_ok);
    run_expect(target, "free all: confirm body",      &c_cmd_body_create,    handle0, sizeof(handle0), status_ok);
    // Leave clean for the next test.
    run_expect_empty(target, "free all: final cleanup", &c_cmd_free_all,     status_ok);
}

static void test_body_clear(HttpTarget *target)
{
    // test_free_all leaves all slots empty, so handle numbers are predictable.
    static const uint8_t handle0[] = { 0x00 };
    static const uint8_t handle1[] = { 0x01 };

    // --- Binary body: clear resets accumulated data, handle remains valid ---

    static uint8_t bin_create_data[] = { 0x06, HTTP_CMD_BODY_CREATE, HTTP_TYPE_BINARY };
    static uint8_t bin_add_data[]    = { 0x06, HTTP_CMD_BODY_ADD_BINARY, 0x00,
                                         0xDE, 0xAD, 0xBE, 0xEF };
    static uint8_t bin_clear_data[]  = { 0x06, HTTP_CMD_BODY_CLEAR, 0x00 };
    Message create_binary = make_msg(bin_create_data, sizeof(bin_create_data));
    Message add_binary    = make_msg(bin_add_data,    sizeof(bin_add_data));
    Message clear_binary  = make_msg(bin_clear_data,  sizeof(bin_clear_data));

    run_expect(target, "body clear: binary create",   &create_binary, handle0, sizeof(handle0), status_ok);
    run_expect_empty(target, "body clear: binary add",          &add_binary,    status_ok);
    run_expect_empty(target, "body clear: binary clear",        &clear_binary,  status_ok);
    // Handle is still valid; re-adding binary data after clear must succeed.
    run_expect_empty(target, "body clear: binary re-add",       &add_binary,    status_ok);

    // --- JSON object body: clear resets to empty root object ---

    // Expected query result before clear: { "count": 1 }
    static const uint8_t count_obj[] = {
        HTTP_DATA_OBJECT, 0x01,
        0x05, 'c', 'o', 'u', 'n', 't',
        HTTP_DATA_INTEGER, 0x01, 0x00, 0x00, 0x00
    };
    // Expected query result after clear: { }
    static const uint8_t empty_obj[] = { HTTP_DATA_OBJECT, 0x00 };

    // JSON OBJ is type 0x02, matching c_cmd_body_create
    static uint8_t json_add_data[]   = { 0x06, HTTP_CMD_BODY_ADD_INT, 0x01,
                                          0x05, 'c', 'o', 'u', 'n', 't', 0x01 };
    static uint8_t json_query_data[] = { 0x06, HTTP_CMD_BODY_QUERY, 0x01 };
    static uint8_t json_clear_data[] = { 0x06, HTTP_CMD_BODY_CLEAR, 0x01 };
    Message add_count     = make_msg(json_add_data,   sizeof(json_add_data));
    Message query_root    = make_msg(json_query_data, sizeof(json_query_data));
    Message clear_json    = make_msg(json_clear_data, sizeof(json_clear_data));

    run_expect(target, "body clear: json create",              &c_cmd_body_create, handle1, sizeof(handle1), status_ok);
    run_expect_empty(target, "body clear: json add count",     &add_count,    status_ok);
    run_expect(target, "body clear: json query before clear",  &query_root,   count_obj, sizeof(count_obj), status_ok);
    run_expect_empty(target, "body clear: json clear",         &clear_json,   status_ok);
    run_expect(target, "body clear: json query after clear",   &query_root,   empty_obj, sizeof(empty_obj), status_ok);
    run_more_expect(target, "body clear: json no more",        (const uint8_t *)"", 0, status_no_more);

    // --- Error case: clearing an unallocated handle returns BAD REQUEST ---

    static uint8_t clear_bad_data[] = { 0x06, HTTP_CMD_BODY_CLEAR, 0x0F }; // handle 15: not allocated
    Message clear_bad = make_msg(clear_bad_data, sizeof(clear_bad_data));
    run_expect_empty(target, "body clear: invalid handle", &clear_bad, status_bad_req);
}

static void test_integer_widths(HttpTarget *target)
{
    char json[] = "{}";
    uint8_t handle = 0xFF;
    target->create_body_from_json(json, sizeof(json) - 1, &handle);
    for (int width = 1; width <= 4; ++width) {
        // Zero, maximum positive, minimum negative, and -1 at each wire width.
        for (int boundary = 0; boundary < 4; ++boundary) {
            uint8_t data[] = { 6, HTTP_CMD_BODY_ADD_INT, handle, 1, 'n', 0, 0, 0, 0 };
            memset(data + 5, boundary == 1 || boundary == 3 ? 0xFF : 0, width);
            data[4 + width] = boundary == 1 ? 0x7F : boundary == 2 ? 0x80 : boundary == 3 ? 0xFF : 0;
            Message command = make_msg(data, 5 + width);
            run_expect_empty(target, "add signed integer boundary", &command, status_ok);
            uint8_t expected[] = { HTTP_DATA_INTEGER, 0, 0, 0, 0 };
            memset(expected + 1, boundary >= 2 ? 0xFF : 0, 4);
            memcpy(expected + 1, data + 5, width);
            query_external(target, "integer sign extension", handle, "n", expected, sizeof(expected));
        }
    }
    command_external_empty(target, "free integer body", HTTP_CMD_BODY_FREE, handle, "", status_ok);
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

class LoopbackEndpoint
{
    int socket_fd;
    uint16_t bound_port;

    LoopbackEndpoint(const LoopbackEndpoint&);
    LoopbackEndpoint& operator=(const LoopbackEndpoint&);
public:
    LoopbackEndpoint() : socket_fd(socket(AF_INET, SOCK_STREAM, 0)), bound_port(0)
    {
        struct sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        if (socket_fd >= 0 && bind(socket_fd, (struct sockaddr *)&address, length) == 0 &&
            getsockname(socket_fd, (struct sockaddr *)&address, &length) == 0) {
            bound_port = ntohs(address.sin_port);
        }
    }

    ~LoopbackEndpoint()
    {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }

    uint16_t port() const { return bound_port; }
    bool start_listening() { return listen(socket_fd, 1) == 0; }
    int accept_connection() { return accept(socket_fd, NULL, NULL); }
};

class ResponsePeer
{
    LoopbackEndpoint endpoint;
    pid_t child;

    ResponsePeer(const ResponsePeer&);
    ResponsePeer& operator=(const ResponsePeer&);
public:
    ResponsePeer(const char *response, int response_length = -1) : child(-1)
    {
        if (!endpoint.port() || !endpoint.start_listening()) {
            return;
        }
        child = fork();
        if (child == 0) {
            alarm(5);
            int client = endpoint.accept_connection();
            char request[1024] = {};
            int used = 0;
            while (client >= 0 && used < (int)sizeof(request) - 1 &&
                   !strstr(request, "\r\n\r\n")) {
                int n = recv(client, request + used, sizeof(request) - 1 - used, 0);
                if (n <= 0) {
                    _exit(2);
                }
                used += n;
            }
            if (client < 0 || !strstr(request, "\r\n\r\n")) {
                _exit(3);
            }
            size_t sent = 0, length = response_length < 0 ? strlen(response) : response_length;
            while (sent < length) {
                int n = send(client, response + sent, length - sent, MSG_NOSIGNAL);
                if (n <= 0) {
                    _exit(4);
                }
                sent += n;
            }
            shutdown(client, SHUT_WR);
            close(client);
            _exit(0);
        }
    }

    ~ResponsePeer()
    {
        if (child > 0) {
            kill(child, SIGKILL);
            waitpid(child, NULL, 0);
        }
    }

    uint16_t port() const { return child > 0 ? endpoint.port() : 0; }
    bool completed()
    {
        if (child <= 0) {
            return false;
        }
        int status;
        pid_t result = waitpid(child, &status, 0);
        if (result != child) {
            return false;
        }
        child = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
};

static int open_descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) {
        return -1;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(directory);
    return count;
}

static void expect_descriptor_count(const char *label, int expected)
{
    int actual = open_descriptor_count();
    checks++;
    if (actual < 0 || actual != expected) {
        failures++;
        printf("FAIL %s: expected %d descriptors, got %d\n", label, expected, actual);
    }
}

static void test_connect_cleanup(void)
{
    // Keep the port bound but not listening: no other process can claim it.
    LoopbackEndpoint endpoint;
    int baseline = open_descriptor_count();
    checks++;
    if (!endpoint.port() || baseline < 0) {
        failures++;
        printf("FAIL connect cleanup: cannot prepare loopback port or count descriptors\n");
        return;
    }

    for (int attempt = 0; attempt < 64; ++attempt) {
        {
            HttpRequest request;
            quiet_begin();
            int result = request.connect_to_server("127.0.0.1", endpoint.port());
            quiet_end();
            checks++;
            if (result != -1) {
                failures++;
                printf("FAIL refused connection %d: expected -1, got %d\n", attempt, result);
            }
            expect_descriptor_count("refused connection releases socket immediately", baseline);
        }
        expect_descriptor_count("failed request destruction leaves no socket", baseline);
    }

    // A closed descriptor can be reused before request destruction. Keep a new
    // descriptor alive across that destruction to catch stale socket ownership.
    int guard_fd = -1;
    {
        HttpRequest request;
        int result = request.connect_to_server("127.0.0.1", endpoint.port());
        checks++;
        if (result != -1) {
            failures++;
            printf("FAIL descriptor reuse: expected a refused connection\n");
        }
        guard_fd = open("/dev/null", O_RDONLY);
        checks++;
        if (guard_fd < 0) {
            failures++;
            printf("FAIL descriptor reuse: cannot open guard descriptor\n");
        }
    }
    checks++;
    if (guard_fd < 0 || fcntl(guard_fd, F_GETFD) < 0) {
        failures++;
        printf("FAIL failed request destruction closed an unrelated descriptor\n");
    }
    if (guard_fd >= 0) {
        close(guard_fd);
    }
    expect_descriptor_count("descriptor reuse leaves no socket", baseline);

    checks++;
    if (!endpoint.start_listening()) {
        failures++;
        printf("FAIL connect cleanup: cannot listen on loopback port\n");
        return;
    }
    {
        HttpRequest request;
        quiet_begin();
        int connected = request.connect_to_server("127.0.0.1", endpoint.port());
        quiet_end();
        checks++;
        if (connected < 0 || fcntl(connected, F_GETFD) < 0) {
            failures++;
            printf("FAIL successful connection must retain an open socket\n");
        }
        expect_descriptor_count("successful request owns one socket", baseline + 1);
    }
    expect_descriptor_count("successful request destruction releases socket", baseline);
}

static void test_response_completion(HttpTarget *target, int selected)
{
    const char *responses[] = {
        "",
        "HTTP/1.1 200 OK\r\nContent-Len",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
    };
    int baseline = open_descriptor_count();
    for (unsigned i = 0; i < sizeof(responses) / sizeof(responses[0]); ++i) {
        if (selected >= 0 && (int)i != selected) {
            continue;
        }
        // Exercise both target-6 exchange forms, followed by a successful recovery.
        for (int raw = 0; raw <= 1; ++raw) {
            for (int recovery = 0; recovery <= 1; ++recovery) {
                const char *response = recovery ?
                    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}" : responses[i];
                {
                    ResponsePeer peer(response);
                    checks++;
                    if (!peer.port()) {
                        failures++;
                        printf("FAIL response peer setup\n");
                        return;
                    }
                    run_expect_empty(target, "reset response fixture", &c_cmd_free_all, status_ok);
                    uint8_t create[128] = { 6, HTTP_CMD_HEADER_CREATE, 1 };
                    int length = snprintf((char *)create + 3, sizeof(create) - 3,
                                          "http://127.0.0.1:%u/", peer.port());
                    Message command = make_msg(create, length + 3);
                    const uint8_t handle[] = { 0 };
                    run_expect(target, "create response header", &command, handle, 1, status_ok);
                    Message *reply, *status;
                    quiet_begin();
                    target->parse_command(raw ? &c_exchange_raw : &c_exchange, &reply, &status);
                    quiet_end();
                    char label[64];
                    snprintf(label, sizeof(label), "response case %u raw=%d recovery=%d", i, raw, recovery);
                    if (!recovery && i < 4) {
                        expect_empty(label, "reply", reply);
                        expect_text(label, "status", status, "503 SERVICE UNAVAILABLE");
                    } else if (raw) {
                        expect_bytes(label, "body", reply, (const uint8_t *)(recovery ? "{}" : i == 4 ? "OK" : ""),
                                     recovery || i == 4 ? 2 : 0);
                        int header_size = strstr(response, "\r\n\r\n") - response + 4;
                        expect_bytes(label, "status", status, (const uint8_t *)response, header_size);
                    } else if (recovery) {
                        const uint8_t handles[] = { 1, 0 };
                        expect_bytes(label, "handles", reply, handles, sizeof(handles));
                        expect_text(label, "status", status, "200 OK");
                    } else {
                        expect_empty(label, "reply", reply);
                        expect_text(label, "status", status, "400 NO VALID JSON");
                    }
                    expect_last(label, reply, true);
                    checks++;
                    if (!peer.completed()) {
                        failures++;
                        printf("FAIL %s peer did not finish\n", label);
                    }
                    run_expect_empty(target, "release response fixture", &c_cmd_free_all, status_ok);
                }
                expect_descriptor_count("response releases sockets", baseline);
            }
        }
    }
}

static void begin_raw_response(HttpTarget *target, ResponsePeer& peer, Message **reply, Message **status)
{
    uint8_t create[128] = { 6, HTTP_CMD_HEADER_CREATE, 1 };
    int length = snprintf((char *)create + 3, sizeof(create) - 3,
                          "http://127.0.0.1:%u/", peer.port());
    Message command = make_msg(create, length + 3);
    const uint8_t handle[] = { 0 };
    run_expect(target, "create loopback header", &command, handle, 1, status_ok);
    quiet_begin();
    target->parse_command(&c_exchange_raw, reply, status);
    quiet_end();
}

static void test_response_boundaries(HttpTarget *target)
{
    const int sizes[] = { 894, 895, 896, 1790, 2048 };
    int baseline = open_descriptor_count();
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        for (int header_size = 254; header_size <= 256; ++header_size) {
            char response[4096];
            int prefix = snprintf(response, sizeof(response),
                                  "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nX-Pad: ", sizes[i]);
            memset(response + prefix, 'p', header_size - prefix - 4);
            memcpy(response + header_size - 4, "\r\n\r\n", 4);
            for (int j = 0; j < sizes[i]; ++j) {
                response[header_size + j] = (char)(j & 255);
            }
            {
                ResponsePeer peer(response, header_size + sizes[i]);
                checks++;
                if (!peer.port()) {
                    failures++;
                    printf("FAIL boundary peer setup\n");
                    return;
                }
                run_expect_empty(target, "reset boundary fixture", &c_cmd_free_all, status_ok);
                Message *reply, *status;
                begin_raw_response(target, peer, &reply, &status);
                expect_bytes("raw header boundary", "status", status,
                             (const uint8_t *)response, header_size < 255 ? header_size : 255);
                int received = 0;
                for (int block = 0; block < 5; ++block) {
                    int count = sizes[i] - received;
                    if (count > 895) {
                        count = 895;
                    }
                    expect_bytes("raw body boundary", "reply", reply,
                                 (const uint8_t *)response + header_size + received, count);
                    expect_last("raw boundary continuation", reply, count < 895);
                    received += count;
                    if (count < 895) {
                        break;
                    }
                    quiet_begin();
                    target->get_more_data(&reply, &status);
                    quiet_end();
                    expect_text("continuation status", "status", status, status_ok);
                }
                run_more_expect(target, "boundary exhausted", (const uint8_t *)"", 0, status_no_more);
                checks++;
                if (!peer.completed()) {
                    failures++;
                    printf("FAIL boundary peer did not finish\n");
                }
                run_expect_empty(target, "release boundary fixture", &c_cmd_free_all, status_ok);
            }
            expect_descriptor_count("boundary sockets released", baseline);
        }
    }
}

static void test_response_cleanup(HttpTarget *target)
{
    int baseline = open_descriptor_count();
    for (int reset = 0; reset <= 1; ++reset) {
        for (int repeat = 0; repeat < 4; ++repeat) {
            char response[1200];
            int header = snprintf(response, sizeof(response),
                                  "HTTP/1.1 200 OK\r\nContent-Length: 1024\r\n\r\n");
            memset(response + header, 'x', 1024);
            {
                ResponsePeer peer(response, header + 1024);
                checks++;
                if (!peer.port()) {
                    failures++;
                    printf("FAIL cleanup peer setup\n");
                    return;
                }
                run_expect_empty(target, "reset cleanup fixture", &c_cmd_free_all, status_ok);
                const uint8_t handle[] = { 0 };
                run_expect(target, "create owned body", &c_cmd_body_create, handle, 1, status_ok);
                Message *reply, *status;
                begin_raw_response(target, peer, &reply, &status);
                expect_last("unread response before cleanup", reply, false);
                checks++;
                if (!peer.completed()) {
                    failures++;
                    printf("FAIL cleanup peer did not finish\n");
                }
                // The peer's listening socket stays owned by the fixture.
                if (reset) {
                    target->c64_reset();
                    target->c64_reset();
                } else {
                    run_expect_empty(target, "free pending exchange", &c_cmd_free_all, status_ok);
                    run_expect_empty(target, "free twice", &c_cmd_free_all, status_ok);
                }
                expect_descriptor_count("cleanup closes pending socket", baseline + 1);
                run_more_expect(target, "cleanup discards unread bytes", (const uint8_t *)"", 0, status_no_more);
                run_expect_empty(target, "old header invalid", &c_cmd_header_query, status_bad_cmd);
                run_expect_empty(target, "old body invalid", &c_cmd_body_queryall1, status_bad_req);
                run_expect(target, "reuse body slot zero", &c_cmd_body_create, handle, 1, status_ok);
                run_expect(target, "reuse header slot zero", &c_cmd_header_create, handle, 1, status_ok);
                // Also release state when running the regression against the broken baseline.
                target->abort(0);
                run_expect_empty(target, "release cleanup fixture", &c_cmd_free_all, status_ok);
            }
            expect_descriptor_count("cleanup fixture releases sockets", baseline);
        }
    }
}

int main(int argc, char **argv)
{
    if ((argc > 1) && (strcmp(argv[1], "--connect-cleanup") == 0)) {
        test_connect_cleanup();
        printf("Connect cleanup: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    HttpTarget *target = (HttpTarget *)command_targets[6];
    if (target && (argc > 1) && (strcmp(argv[1], "--integer-widths") == 0)) {
        test_integer_widths(target);
        printf("Integer widths: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (!target) {
        printf("FAIL target 6 was not registered\n");
        return 1;
    }

    if ((argc > 1) && (strcmp(argv[1], "--response-completion") == 0)) {
        test_response_completion(target, argc > 2 ? atoi(argv[2]) : -1);
        printf("Response completion: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    if ((argc > 1) && (strcmp(argv[1], "--response-boundaries") == 0)) {
        test_response_boundaries(target);
        printf("Response boundaries: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    if ((argc > 1) && (strcmp(argv[1], "--response-cleanup") == 0)) {
        test_response_cleanup(target);
        printf("Response cleanup: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    if ((argc > 1) && (strcmp(argv[1], "--body-removal") == 0)) {
        test_body_removal(target);
        printf("Body removal: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    test_headers(target);
    test_legacy_body_object(target);
    test_legacy_body_array(target);
    run_expect_empty(target, "free legacy body 0", &c_cmd_body_free_00, status_ok);
    run_expect_empty(target, "free legacy body 1", &c_cmd_body_free_01, status_ok);
    test_structured_body_add(target);
    test_external_json(target);
    test_free_all(target);
    test_integer_widths(target);
    test_body_clear(target);
    test_body_removal(target);

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
