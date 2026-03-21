#include "http_target.h"
#include "dump_hex.h"

// these globals will be filled in by the clients
CommandTarget *command_targets[CMD_IF_MAX_TARGET+1];

Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" }; 
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };

static Message c_cmd_identify      = {  2, true, (uint8_t *)"\x06\x01" };
static Message c_cmd_header_create = { 49, true, (uint8_t *)"\x06\x11\x03""commoserve.files.commodore.net/leet/search/bin" };
static Message c_cmd_header_add1   = { 33, true, (uint8_t *)"\x06\x13\x00""Content-Type: application/json" };
static Message c_cmd_header_add2   = { 20, true, (uint8_t *)"\x06\x13\x00""Connection: close" };
static Message c_cmd_header_add3   = { 32, true, (uint8_t *)"\x06\x13\x00""Content-Type: application/xml" };
static Message c_cmd_header_query  = { 15, true, (uint8_t *)"\x06\x14\x00""Content-Type" };
static Message c_cmd_header_list1  = {  4, true, (uint8_t *)"\x06\x15\x00\x00" };
static Message c_cmd_header_list2  = {  4, true, (uint8_t *)"\x06\x15\x00\x01" };
static Message c_cmd_header_list3  = {  4, true, (uint8_t *)"\x06\x15\x01\x00" };
static Message c_cmd_header_free   = {  3, true, (uint8_t *)"\x06\x12\x00" };

void dump(Message *cmd, Message *reply, Message *status)
{
    printf("command:\n");
    dump_hex_relative((void *)cmd->message, cmd->length);
    printf("reply:\n");
    dump_hex_relative((void *)reply->message, reply->length);
    printf("status:\n");
    dump_hex_relative((void *)status->message, status->length);
    printf("\n");
}

int main(int argc, char **argv)
{
    CommandTarget *t = command_targets[6];
    if (!t) return -1;

    Message *reply, *status;
    t->parse_command(&c_cmd_identify, &reply, &status);
    dump(&c_cmd_identify, reply, status);

    t->parse_command(&c_cmd_header_create, &reply, &status);
    dump(&c_cmd_header_create, reply, status);

    t->parse_command(&c_cmd_header_add1, &reply, &status);
    dump(&c_cmd_header_add1, reply, status);

    t->parse_command(&c_cmd_header_add2, &reply, &status);
    dump(&c_cmd_header_add2, reply, status);

    t->parse_command(&c_cmd_header_add3, &reply, &status);
    dump(&c_cmd_header_add3, reply, status);

    t->parse_command(&c_cmd_header_query, &reply, &status);
    dump(&c_cmd_header_query, reply, status);

    t->parse_command(&c_cmd_header_list1, &reply, &status);
    dump(&c_cmd_header_list1, reply, status);

    t->parse_command(&c_cmd_header_list2, &reply, &status);
    dump(&c_cmd_header_list2, reply, status);

    t->parse_command(&c_cmd_header_list3, &reply, &status);
    dump(&c_cmd_header_list3, reply, status);

    t->parse_command(&c_cmd_header_free, &reply, &status);
    dump(&c_cmd_header_free, reply, status);

    return 0;
}

// stubs
void outbyte(int b)
{
	fputc(b, stdout);
}
