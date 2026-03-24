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

static Message c_cmd_body_create   = {  3, true, (uint8_t *)"\x06\x21\x02"}; // ``$06 $21 <FORMAT>`` 2 = json object
static Message c_cmd_body_add_int  = { 11, true, (uint8_t *)"\x06\x23\x00\x05""width""\xF4\x01"}; // ``$06 $23 $XX $05 “width” $F4 $01``
static Message c_cmd_body_add_bool = { 12, true, (uint8_t *)"\x06\x24\x00\x07""visible""\x01"}; // ``$06 $24 $XX $07 “visible” $01``
static Message c_cmd_body_add_str  = { 24, true, (uint8_t *)"\x06\x25\x00\x05""title""\x0e""Commodore Intl"}; // ``$06 $25 $XX $05 “title” $0E “Commodore Intl”``
static Message c_cmd_body_add_obj  = {  8, true, (uint8_t *)"\x06\x26\x00\x04""user"}; // ``$06 $26 $XX $04 “user”``
static Message c_cmd_body_add_array= {  8, true, (uint8_t *)"\x06\x27\x00\x04""cars"}; // ``$06 $27 $XX $04 “cars”``
static Message c_cmd_body_add_car1 = {  9, true, (uint8_t *)"\x06\x25\x00\x00\x04""Opel" };
static Message c_cmd_body_add_car2 = { 12, true, (uint8_t *)"\x06\x25\x00\x00\x07""Renault" };
static Message c_cmd_body_add_car3 = { 11, true, (uint8_t *)"\x06\x25\x00\x00\x06""Toyota" };
static Message c_cmd_body_add_car4 = { 15, true, (uint8_t *)"\x06\x25\x00\x00\x0a""Mitsubishi" };
static Message c_cmd_body_add_car5 = { 13, true, (uint8_t *)"\x06\x25\x00\x00\x08""Mercedes" };
static Message c_cmd_body_up       = {  3, true, (uint8_t *)"\x06\x28\x00" };
static Message c_cmd_body_add_bool2= { 10, true, (uint8_t *)"\x06\x24\x00\x05""close""\x01"}; 
static Message c_cmd_body_query1   = { 14, true, (uint8_t *)"\x06\x2A\x00""user/cars%2"};

    // Build
    // [
    //     { “user” : “1541u”, “name”: “Gideon” },
    //     { “user” : “bvl1999”, “name”: “Bart” }
    // ]

static Message c_cmd_body_create2  = {  3, true, (uint8_t *)"\x06\x21\x03"}; // ``$06 $21 <FORMAT>`` 2 = json list
static Message c_cmd_body2_add_1   = {  4, true, (uint8_t *)"\x06\x26\x01\x00" }; // unnamed object
static Message c_cmd_body2_add_2   = { 14, true, (uint8_t *)"\x06\x25\x01\x04""user""\x05""1541u"}; // user 1541u
static Message c_cmd_body2_add_3   = { 15, true, (uint8_t *)"\x06\x25\x01\x04""name""\x06""Gideon"}; // name Gideon
static Message c_cmd_body2_up_1    = {  3, true, (uint8_t *)"\x06\x28\x01" };
static Message c_cmd_body2_add_4   = {  4, true, (uint8_t *)"\x06\x26\x01\x00" }; // unnamed object
static Message c_cmd_body2_add_5   = { 16, true, (uint8_t *)"\x06\x25\x01\x04""user""\x07""bvl1999"}; // user bvl1999
static Message c_cmd_body2_add_6   = { 13, true, (uint8_t *)"\x06\x25\x01\x04""name""\x04""Bart"}; // name Bart
static Message c_cmd_body2_up_2    = {  3, true, (uint8_t *)"\x06\x28\x01" };
static Message c_cmd_body2_query1  = { 10, true, (uint8_t *)"\x06\x2A\x01""%1/name"};
static Message c_cmd_body2_add_7   = {  4, true, (uint8_t *)"\x06\x27\x01\x00"}; // unnamed array
static Message c_cmd_body2_add_8   = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xEB"}; // unnamed -21
static Message c_cmd_body2_add_9   = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xEC"}; // unnamed -20
static Message c_cmd_body2_add_10  = {  5, true, (uint8_t *)"\x06\x23\x01\x00\xED"}; // unnamed -19
static Message c_cmd_body2_query2  = {  8, true, (uint8_t *)"\x06\x2A\x01""%2/%1"};

    // Read external JSON and query it

static Message c_cmd_get_chocolate  = { 20, true, (uint8_t *)"\x06\x2A\x02""%1/topping%3/type" };
static Message c_cmd_get_chocolate2 = { 21, true, (uint8_t *)"\x06\x2A\x02""%1/topping/%3/type" };
static Message c_cmd_get_chocolate3 = { 28, true, (uint8_t *)"\x06\x2A\x02""%2/batters/batter/%1/type" };
static Message c_cmd_get_chocolate4 = { 28, true, (uint8_t *)"\x06\x2A\x02""%2/batters/batter[1]/type" };

static Message c_cmd_rem_blueberry1 = { 28, true, (uint8_t *)"\x06\x29\x02""%0/batters/batter[2]/type" };
static Message c_cmd_rem_blueberry2 = { 23, true, (uint8_t *)"\x06\x29\x02""%0/batters/batter[2]" };

static Message c_cmd_move_batters   = { 13, true, (uint8_t *)"\x06\x2B\x02""%1/batters" }; // Command format: ``$06 $2B <HANDLE> <PATH>``
static Message c_cmd_add_to_batters = { 15, true, (uint8_t *)"\x06\x25\x02\x04""name""\x06""Gideon"}; // name Gideon
static Message c_cmd_query_object   = {  5, true, (uint8_t *)"\x06\x2A\x02""%1" };
static Message c_cmd_delete_object  = {  5, true, (uint8_t *)"\x06\x29\x02""%1" };

static Message c_cmd_body_free_00   = {  3, true, (uint8_t *)"\x06\x22\x00" };
static Message c_cmd_body_free_01   = {  3, true, (uint8_t *)"\x06\x22\x01" };
static Message c_cmd_body_free_02   = {  3, true, (uint8_t *)"\x06\x22\x02" };

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
    HttpTarget *t = (HttpTarget *)command_targets[6];
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

    t->parse_command(&c_cmd_body_create, &reply, &status);   
    t->parse_command(&c_cmd_body_add_int, &reply, &status);  
    t->parse_command(&c_cmd_body_add_bool, &reply, &status); 
    t->parse_command(&c_cmd_body_add_str, &reply, &status);  
    t->parse_command(&c_cmd_body_add_obj, &reply, &status);  
    t->parse_command(&c_cmd_body_add_array, &reply, &status);
    t->parse_command(&c_cmd_body_add_car1, &reply, &status); 
    t->parse_command(&c_cmd_body_add_car2, &reply, &status); 
    t->parse_command(&c_cmd_body_add_car3, &reply, &status); 
    t->parse_command(&c_cmd_body_add_car4, &reply, &status); 
    t->parse_command(&c_cmd_body_add_car5, &reply, &status); 
    t->parse_command(&c_cmd_body_up, &reply, &status);       
    t->parse_command(&c_cmd_body_add_bool2, &reply, &status);
    t->parse_command(&c_cmd_body_query1, &reply, &status);
    dump(&c_cmd_body_query1, reply, status);

    t->parse_command(&c_cmd_body_create2, &reply, &status);
    t->parse_command(&c_cmd_body2_add_1, &reply, &status);
    t->parse_command(&c_cmd_body2_add_2, &reply, &status);
    t->parse_command(&c_cmd_body2_add_3, &reply, &status);
    t->parse_command(&c_cmd_body2_up_1, &reply, &status);
    t->parse_command(&c_cmd_body2_add_4, &reply, &status);
    t->parse_command(&c_cmd_body2_add_5, &reply, &status);
    t->parse_command(&c_cmd_body2_add_6, &reply, &status);
    t->parse_command(&c_cmd_body2_up_2, &reply, &status);
    t->parse_command(&c_cmd_body2_query1, &reply, &status);
    dump(&c_cmd_body2_query1, reply, status);

    t->parse_command(&c_cmd_body2_add_7, &reply, &status);
    t->parse_command(&c_cmd_body2_add_8, &reply, &status);
    t->parse_command(&c_cmd_body2_add_9, &reply, &status);
    t->parse_command(&c_cmd_body2_add_10, &reply, &status);
    t->parse_command(&c_cmd_body2_query2, &reply, &status);
    dump(&c_cmd_body2_query2, reply, status);

    FILE *fi = fopen("nested.json", "r");
    char *json_body = new char[4096];
    int size = fread(json_body, 1, 4096, fi);
    fclose(fi);

    uint8_t h;
    t->create_body_from_json(json_body, size, &h);
    delete[] json_body;

    t->parse_command(&c_cmd_get_chocolate, &reply, &status);
    dump(&c_cmd_get_chocolate, reply, status);

    t->parse_command(&c_cmd_get_chocolate2, &reply, &status);
    dump(&c_cmd_get_chocolate2, reply, status);

    t->parse_command(&c_cmd_get_chocolate3, &reply, &status);
    dump(&c_cmd_get_chocolate3, reply, status);

    t->parse_command(&c_cmd_get_chocolate4, &reply, &status);
    dump(&c_cmd_get_chocolate4, reply, status);

    t->parse_command(&c_cmd_rem_blueberry1, &reply, &status);
    dump(&c_cmd_rem_blueberry1, reply, status);

    t->parse_command(&c_cmd_rem_blueberry2, &reply, &status);
    dump(&c_cmd_rem_blueberry2, reply, status);

    t->parse_command(&c_cmd_move_batters, &reply, &status);
    dump(&c_cmd_move_batters, reply, status);

    t->parse_command(&c_cmd_add_to_batters, &reply, &status);
    dump(&c_cmd_add_to_batters, reply, status);

    t->parse_command(&c_cmd_query_object, &reply, &status);
    dump(&c_cmd_query_object, reply, status);

    t->parse_command(&c_cmd_delete_object, &reply, &status);
    dump(&c_cmd_delete_object, reply, status);

    t->parse_command(&c_cmd_body_free_00, &reply, &status);
    dump(&c_cmd_body_free_00, reply, status);

    t->parse_command(&c_cmd_body_free_01, &reply, &status);
    dump(&c_cmd_body_free_01, reply, status);

    t->parse_command(&c_cmd_body_free_02, &reply, &status);
    dump(&c_cmd_body_free_02, reply, status);


    return 0;
}

// stubs
void outbyte(int b)
{
	fputc(b, stdout);
}
