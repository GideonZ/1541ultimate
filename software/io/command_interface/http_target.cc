#include "http_target.h"
#include <string.h>
#include <stdio.h>

// Register the target as ID $06 [cite: 117]
HttpTarget http_target(6);

static Message c_http_identification  = { 25, true, (uint8_t *)"ULTIMATE HTTP TARGET V1.0" };
static Message c_status_http_ok       = {  6, true, (uint8_t *)"000 OK" };
static Message c_status_bad_cmd       = { 15, true, (uint8_t *)"400 BAD COMMAND" };
static Message c_status_bad_req       = { 15, true, (uint8_t *)"400 BAD REQUEST" };
static Message c_status_no_hdr_slot   = { 18, true, (uint8_t *)"507 NO HEADER SLOT" };
static Message c_status_no_data_slot  = { 16, true, (uint8_t *)"507 NO DATA SLOT" };
static Message c_status_bad_format    = { 14, true, (uint8_t *)"500 BAD FORMAT" };
static Message c_status_key_missing   = { 19, true, (uint8_t *)"404 KEY NOT PRESENT" };
static Message c_status_not_found     = { 19, true, (uint8_t *)"404 ENTRY NOT FOUND" };

HttpTarget::HttpTarget(int id)
{
    command_targets[id] = this;
    memset(headers, 0, sizeof(headers));
    memset(headers, 0, sizeof(bodies));
    data_message.message = new uint8_t[CMD_MAX_REPLY_LEN];
    status_message.message = new uint8_t[CMD_MAX_STATUS_LEN];
}

HttpTarget::~HttpTarget()
{
    for(int i=0; i<MAX_HTTP_HANDLES; i++) {
        if (headers[i]) {
            delete headers[i];
        }
    }
    for(int i=0; i<MAX_HTTP_HANDLES; i++) {
        if (bodies[i]) {
            delete bodies[i];
        }
    }
    delete[] data_message.message;
    delete[] status_message.message;
}

void HttpTarget::parse_command(Message *command, Message **reply, Message **status)
{
    *reply = &c_message_empty;
    *status = &c_status_bad_cmd;

    switch(command->message[1]) {
        case HTTP_CMD_IDENTIFY:
            *reply = &c_http_identification;
            *status = &c_status_http_ok;
            break;

        case HTTP_CMD_HEADER_CREATE:
            cmd_header_create(command, reply, status);
            break;

        case HTTP_CMD_HEADER_FREE:
            if (command->message[2] < MAX_HTTP_HANDLES) {
                if (headers[command->message[2]]) {
                    delete headers[command->message[2]];
                    headers[command->message[2]] = NULL;
                }
            }
            *status = &c_status_http_ok; // Free never fails [cite: 149]
            break;

        case HTTP_CMD_HEADER_ADD:
            cmd_header_add(command, reply, status);
            break;

        case HTTP_CMD_HEADER_QUERY:
            cmd_header_query(command, reply, status);
            break;

        case HTTP_CMD_HEADER_LIST:
            cmd_header_list(command, reply, status);
            break;

        case HTTP_CMD_BODY_CREATE:
            cmd_body_create(command, reply, status);
            break;

        case HTTP_CMD_BODY_FREE:
            if (command->message[2] < MAX_HTTP_HANDLES) {
                if (bodies[command->message[2]]) {
                    delete bodies[command->message[2]];
                    bodies[command->message[2]] = NULL;
                }
            }
            *status = &c_status_http_ok;
            break;

        case HTTP_CMD_BODY_ADD_INT:
        case HTTP_CMD_BODY_ADD_BOOL:
        case HTTP_CMD_BODY_ADD_STRING:
        case HTTP_CMD_BODY_ADD_OBJECT:
        case HTTP_CMD_BODY_ADD_ARRAY:
            cmd_body_add_primitive(command, reply, status, command->message[1]);
            break;

        case HTTP_CMD_BODY_UP:
            cmd_body_up(command, reply, status);
            *status = &c_status_http_ok;
            break;

        case HTTP_CMD_BODY_REMOVE:
            cmd_body_remove(command, reply, status);
            break;

        case HTTP_CMD_BODY_QUERY:
            cmd_body_query(command, reply, status);
            break;

        case HTTP_CMD_BODY_MOVE:
            cmd_body_move(command, reply, status);
            break;

        case HTTP_CMD_BODY_ADD_BINARY:
            cmd_body_add_binary(command, reply, status);
            break;

        case HTTP_CMD_DO_EXCHANGE_OBJ:
            cmd_exchange(command, reply, status, false);
            break;

        case HTTP_CMD_DO_EXCHANGE_RAW:
            cmd_exchange(command, reply, status, true);
            break;

        default:
            *status = &c_status_bad_cmd;
            break;
    }
}

void HttpTarget::cmd_header_create(Message *command, Message **reply, Message **status)
{
    int slot = -1;
    for(int i=0; i<MAX_HTTP_HANDLES; i++) {
        if(!headers[i]) {
            slot = i;
            break;
        }
    }

    if(slot == -1) {
        *status = &c_status_no_hdr_slot;
        return;
    }

    headers[slot] = new HttpHeaderSlot(command->message[2], (const char *)&command->message[3], command->length - 3);
    
    data_message.message[0] = (uint8_t)slot;
    data_message.length = 1;
    data_message.last_part = true;
    *reply = &data_message;
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_header_add(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !headers[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[handle];

    mstring line((const char *)command->message, 3, command->length); // copy
    const char *value = line.split(": ");
    if (!value) {
        *status = &c_status_bad_format;
        return;
    }

    // Logic to store/replace key-value pair in headers[handle].lines
    hdr->set(line.c_str(), value);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_header_query(Message *command, Message **reply, Message **status)
{
    // Command format ``$06 $14 <HANDLE> <KEY>``

    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !headers[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[handle];
    mstring key((const char *)command->message, 3, command->length);
    JSON *j = hdr->get(key.c_str());
    if (!j) {
        *status = &c_status_key_missing;
        return;
    }
    *reply = &data_message;
    const char *rend;
    if (j->type() == eString) {
        JSON_String *str = (JSON_String *)j;
        rend = str->get_string();
    } else {
        rend = j->render();
    }
    data_message.length = strlen(rend);
    data_message.last_part = true;
    strncpy((char *)data_message.message, rend, CMD_MAX_REPLY_LEN);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_header_list(Message *command, Message **reply, Message **status)
{
    // Command format ``$06 $15 <HANDLE> <INDEX>``

    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !headers[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[handle];
    StreamRamFile s(800);

    const char *key;
    if (command->message[3]) {
        JSON *j = hdr->get(command->message[3], &key);
        if (!j) {
            *status = &c_status_key_missing;
            return;
        }
        s.format("%s: ", key);
        if (j->type() == eString) {
            s.format("%s\r", ((JSON_String *)j)->get_string());
        } else {
            j->render(&s);
            s.format("\r");
        }
    } else { // list all
        int index = 1;
        do {
            JSON *j = hdr->get(index, &key);
            if (j) {
                s.format("%s: ", key);
                if (j->type() == eString) {
                    s.format("%s\r", ((JSON_String *)j)->get_string());
                } else {
                    j->render(&s);
                    s.format("\r");
                }
            } else {
                break;
            }
            index++;
        } while(1);
    }

    *reply = &data_message;
    data_message.length = s.read((char *)data_message.message, 895);
    data_message.last_part = true;

    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_create(Message *command, Message **reply, Message **status)
{
    if ((command->message[2] < 1) || (command->message[2] > 4)) {
        *status = &c_status_bad_format;
        return;
    }
    int slot = -1;
    for(int i=0; i<MAX_HTTP_HANDLES; i++) {
        if(!bodies[i]) {
            slot = i;
            break;
        }
    }

    if(slot == -1) {
        *status = &c_status_no_data_slot;
        return;
    }

    bodies[slot] = new HttpBodySlot(command->message[2]);

    data_message.message[0] = (uint8_t)slot;
    data_message.length = 1;
    data_message.last_part = true;
    *reply = &data_message;
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_add_primitive(Message *command, Message **reply, Message **status, uint8_t type)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    if (body->get_format() == HTTP_TYPE_BINARY) {
        *status = &c_status_bad_format;
        return;
    }

    bool value_bool = false;
    int value_int = 0;

    // extract key
    // ``$06 $23 <HANDLE> <KEYLEN> <KEY> <INT>``
    mstring key((const char *)command->message, 4, 3 + command->message[3]);
    int pos_val = 4 + command->message[3];

    switch(type) {
        case HTTP_CMD_BODY_ADD_INT:
            {
                int len_int = command->length - pos_val;
                len_int = (len_int > 4) ? 4 : len_int; // maximize
                // clunky way to sign extend
                for(int i=len_int-1; i >= 0; i--) {
                    if(i == len_int-1) {
                        value_int = (char)command->message[pos_val + i];
                    } else {
                        value_int <<= 8;
                        value_int |= command->message[pos_val + i];
                    }
                }
                body->add_int(key.c_str(), value_int);
            }
            break;
        case HTTP_CMD_BODY_ADD_BOOL:
            body->add_bool(key.c_str(), command->message[pos_val] != 0);
            break;
        case HTTP_CMD_BODY_ADD_STRING:
            {
                mstring s((const char *)command->message, pos_val + 1, pos_val + 1 + command->message[pos_val]);
                body->add_str(key.c_str(), s.c_str());
            }
            break;

        case HTTP_CMD_BODY_ADD_OBJECT:
            body->add_obj(key.c_str());
            break;
        
        case HTTP_CMD_BODY_ADD_ARRAY:
            body->add_array(key.c_str());
            break;

        default:
            *status = &c_status_bad_cmd;
            return;
    }
    printf("After add:\n");
    body->dump();
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_add_binary(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    if (body->get_format() != HTTP_TYPE_BINARY) {
        *status = &c_status_bad_req;
        return;
    }
    body->add_binary(&command->message[3], command->length-3);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_up(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    body->move_up();
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_remove(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    mstring path((const char *)command->message, 3, command->length - 1);
    JSON *j = body->walk_path(path, (char *)status_message.message);
    if(!j) {
        *status = &status_message;
        status_message.length = strlen((char *)status_message.message);
        return;
    }
    printf("Entry to remove: %s\n", j->render());
    body->remove(j);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_move(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    mstring path((const char *)command->message, 3, command->length - 1);
    JSON *j = body->walk_path(path, (char *)status_message.message);
    if(!j) {
        *status = &status_message;
        status_message.length = strlen((char *)status_message.message);
        return;
    }
    body->set_current(j);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_body_query(Message *command, Message **reply, Message **status)
{
    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !bodies[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpBodySlot *body = bodies[handle];
    mstring path((const char *)command->message, 3, command->length-1);
    JSON *j = body->walk_path(path, (char *)status_message.message);
    if(!j) {
        *status = &status_message;
        status_message.length = strlen((char *)status_message.message);
        return;
    }
    *reply = &data_message;
    int value;
    const char *value_str;
    switch(j->type()) {
    case eInteger:
        data_message.message[0] = 0x01;
        value = ((JSON_Integer *)j)->get_value();
        data_message.message[1] = value & 0xFF; value >>= 8;
        data_message.message[2] = value & 0xFF; value >>= 8;
        data_message.message[3] = value & 0xFF; value >>= 8;
        data_message.message[4] = value & 0xFF;
        data_message.length = 5;
        break;
    case eBool:
        data_message.message[0] = 0x02;
        value = ((JSON_Bool *)j)->get_value();
        data_message.message[1] = value ? 1 : 0;
        data_message.length = 2;
        break;
    case eString:
        data_message.message[0] = 0x03;
        value_str = ((JSON_String *)j)->get_string();
        data_message.length = 1 + strlen(value_str);
        strncpy((char *)&data_message.message[1], value_str, CMD_MAX_REPLY_LEN - 1);
        break;
    case eObject:
        data_message.message[0] = 0x04;
        value_str = j->render();
        data_message.length = 1 + strlen(value_str);
        strncpy((char *)&data_message.message[1], value_str, CMD_MAX_REPLY_LEN - 1);
        break;
    case eList:
        data_message.message[0] = 0x05;
        data_message.length = 1 + strlen(value_str);
        strncpy((char *)&data_message.message[1], value_str, CMD_MAX_REPLY_LEN - 1);
        break;
    }
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_exchange(Message *command, Message **reply, Message **status, bool raw)
{
    uint8_t hdr_handle = command->message[2];
    uint8_t body_handle = command->message[3];

    if(hdr_handle >= MAX_HTTP_HANDLES || !headers[hdr_handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    if(body_handle >= MAX_HTTP_HANDLES || !bodies[body_handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[hdr_handle];
    HttpBodySlot *body = bodies[body_handle];
    
    StreamRamFile req(512), resp(512);
    hdr->render(&req);
    body->render(&req);

    int result = http_exchange(&req, &resp);

    if(raw) {
        // Return raw data in data channel [cite: 264]
        *status = &c_status_http_ok; 
    } else {
        // Create response objects and return two handles [cite: 258]
        data_message.message[0] = 0x01; // New Header Handle
        data_message.message[1] = 0x01; // New Body Handle
        data_message.length = 2;
        *reply = &data_message;
        *status = &c_status_http_ok;
    }
}

void HttpTarget::get_more_data(Message **reply, Message **status)
{
    // Used for raw transfers exceeding 896 bytes [cite: 265]
    *reply = &c_message_empty;
    *status = &c_status_http_ok;
}

void HttpTarget::abort(int a)
{
    // Reset any pending exchange state
}

int HttpTarget::create_body_from_json(char *body, int size, uint8_t *handle)
{
    JSON *json = NULL;
    int j = convert_text_to_json_objects(body, size, 1000, &json);
    if (j < 0) {
        if (json) {
            delete json;
        }
        return j;
    }

    int slot = -1;
    for(int i=0; i<MAX_HTTP_HANDLES; i++) {
        if(!bodies[i]) {
            slot = i;
            break;
        }
    }
    if(slot == -1) {
        return -99;
    }
    bodies[slot] = new HttpBodySlot(json);
    *handle = slot;
    return j;
}
