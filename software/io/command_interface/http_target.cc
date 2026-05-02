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
static Message c_status_out_of_bounds = { 23, true, (uint8_t *)"400 INDEX OUT OF BOUNDS" };
static Message c_status_key_missing   = { 19, true, (uint8_t *)"404 KEY NOT PRESENT" };
static Message c_status_not_found     = { 19, true, (uint8_t *)"404 ENTRY NOT FOUND" };
static Message c_status_no_more       = { 16, true, (uint8_t *)"500 NO MORE DATA" };
static Message c_status_not_available = { 23, true, (uint8_t *)"503 SERVICE UNAVAILABLE" };
static Message c_status_no_valid_json = { 17, true, (uint8_t *)"400 NO VALID JSON"};

HttpTarget::HttpTarget(int id)
{
    command_targets[id] = this;
    memset(headers, 0, sizeof(headers));
    memset(bodies, 0, sizeof(bodies));
    data_message.message = new uint8_t[CMD_MAX_REPLY_LEN];
    status_message.message = new uint8_t[CMD_MAX_STATUS_LEN];
    exch = NULL;
    response_data = NULL;
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
    reset_responses();
}

void HttpTarget::reset_responses()
{
    if (exch) {
        delete exch;
        exch = NULL;
    }
    if (response_data) {
        delete response_data;
        response_data = NULL;
    }
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
    if (command->length < 7) {
        *status = &c_status_bad_cmd;
        return;
    }
    if ((command->message[2] < 1) || (command->message[2] > 9)) { // only accept valid verb values
        *status = &c_status_bad_cmd;
        return;
    }

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
    if (command->length < 7) {
        *status = &c_status_bad_cmd;
        return;
    }

    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !headers[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[handle];

    mstring line((const char *)command->message, 3, command->length-1); // copy
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
    if (command->length < 4) {
        *status = &c_status_bad_cmd;
        return;
    }

    uint8_t handle = command->message[2];
    if(handle >= MAX_HTTP_HANDLES || !headers[handle]) {
        *status = &c_status_bad_cmd;
        return;
    }
    HttpHeaderSlot *hdr = headers[handle];
    mstring key((const char *)command->message, 3, command->length-1);
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
    int len_out = strlen(rend);
    if (len_out > CMD_MAX_REPLY_LEN)
        len_out = CMD_MAX_REPLY_LEN;
    data_message.length = len_out;
    data_message.last_part = true;
    memcpy(data_message.message, rend, len_out);
    *status = &c_status_http_ok;
}

void HttpTarget::cmd_header_list(Message *command, Message **reply, Message **status)
{
    // Command format ``$06 $15 <HANDLE> <INDEX>``
    if (command->length != 4) {
        *status = &c_status_bad_cmd;
        return;
    }

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
            *status = &c_status_out_of_bounds;
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
    if (command->length != 3) {
        *status = &c_status_bad_cmd;
        return;
    }

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
                mstring s((const char *)command->message, pos_val + 1, pos_val + 1 + command->message[pos_val] - 1);
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
        *status = &c_status_bad_req;
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
        *status = &c_status_bad_req;
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
        *status = &c_status_bad_req;
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
    reset_responses();
    response_data = new StreamRamFile(CMD_MAX_REPLY_LEN);
    express(j);
    get_more_data(reply, status);
}

void HttpTarget :: express(JSON *j)
{
    int value;
    const char *value_str;
    IndexedList<const char *> *keys;
    IndexedList<JSON *> *values;
    JSON_List *list;
    JSON_Object *obj;

    switch(j->type()) {
    case eInteger:
        response_data->charout(HTTP_DATA_INTEGER);
        value = ((JSON_Integer *)j)->get_value();
        response_data->charout(value & 0xFF); value >>= 8;
        response_data->charout(value & 0xFF); value >>= 8;
        response_data->charout(value & 0xFF); value >>= 8;
        response_data->charout(value & 0xFF);
        break;
    case eBool:
        response_data->charout(HTTP_DATA_BOOL);
        value = ((JSON_Bool *)j)->get_value();
        response_data->charout(value ? 1 : 0);
        break;
    case eString:
        response_data->charout(HTTP_DATA_STRING);
        value_str = ((JSON_String *)j)->get_string();
        value = strlen(value_str);
        response_data->charout(value);
        response_data->write((uint8_t *)value_str, value);
        break;
    case eObject:
        response_data->charout(HTTP_DATA_OBJECT);
        obj = (JSON_Object *)j;
        keys = obj->get_keys();
        values = obj->get_values();
        response_data->charout(keys->get_elements());
        for(int i=0;i<keys->get_elements();i++) {
            // express key
            value = strlen((*keys)[i]);
            response_data->charout(value);
            response_data->write((uint8_t *)(*keys)[i], value);
            express((*values)[i]);
        }
        break;
    case eList:
        response_data->charout(HTTP_DATA_ARRAY);
        list = (JSON_List *)j;
        value = list->get_num_elements();
        response_data->charout(value);
        for(int i=0;i<value;i++) {
            express((*list)[i]);
        }
        break;
    }
}

void HttpTarget::cmd_exchange(Message *command, Message **reply, Message **status, bool raw)
{
    uint8_t hdr_handle = command->message[2];
    uint8_t body_handle = command->message[3];
    HttpBodySlot *bdy = NULL;
    HttpHeaderSlot *hdr = NULL;
    if(hdr_handle >= MAX_HTTP_HANDLES || !headers[hdr_handle]) {
        *status = &c_status_bad_cmd;
        return;
    } else {
        hdr = headers[hdr_handle];
    }
    if(body_handle >= MAX_HTTP_HANDLES || !bodies[body_handle]) {
        // bdy remains NULL, which means that there is no body to be sent
    } else {
        bdy = bodies[body_handle];
    }
    
    StreamRamFile req(512);
    hdr->render(&req);
    if(bdy) {
        bdy->render(&req);
    }

    hdr->dump();

    reset_responses();

    exch = new HttpRequest();
    if (exch->connect_to_server(hdr->get_host(), hdr->get_port()) >= 0) {
        if (!exch->send_request(&req)) {
            if (raw) {
                HTTPReqHeader *hdr = exch->get_header();
                if (hdr) {
                    hdr->RawCopy = status_message.message;
                    hdr->RawCopySize = CMD_MAX_STATUS_LEN;
                }
            }
            exch->recv_response();
        } else {
            printf("Failed to send request\n");
            delete exch;
            exch = NULL;
            *status = &c_status_not_available;
            return;
        }
    } else {
        printf("Failed to connect.\n");
        delete exch;
        exch = NULL;
        *status = &c_status_not_available;
        return;
    }

    // When we are here, the exch object exists and should contain the reply.

    int slot = -1;
    if(raw) {
        // Return raw data in data channel [cite: 264]
        *reply = &data_message;
        data_message.length = exch->read_response_data(CMD_MAX_REPLY_LEN, data_message.message);
        data_message.last_part = (data_message.length != CMD_MAX_REPLY_LEN);
        // Copy at most CMD_MAX_STATUS_LEN bytes into the status. Note that parse header will insert
        // zeros at string boundaries
        HTTPReqHeader *hdr = exch->get_header();
        *status = &status_message;
        status_message.length = hdr->RawCopyLength;
        // Clean up if this was the last message
        if (data_message.last_part) {
            delete exch;
            exch = NULL;
        }
    } else {
        JSON *json = exch->get_json();
        if(!json) {
            *reply = &c_message_empty;
            *status = &c_status_no_valid_json;
            delete exch;
            exch = NULL;
            return;
        }
        for(int i=0; i<MAX_HTTP_HANDLES; i++) {
            if(!bodies[i]) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            *status = &c_status_no_data_slot;
            *reply = &c_message_empty;
            delete json;
            delete exch;
            exch = NULL;
            return;
        }
        body_handle = slot;
        bodies[body_handle] = new HttpBodySlot(json);

        slot = -1;
        for(int i=0; i<MAX_HTTP_HANDLES; i++) {
            if(!headers[i]) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            *status = &c_status_no_hdr_slot;
            *reply = &c_message_empty;

            delete bodies[body_handle]; // also deletes the json
            bodies[body_handle] = NULL;
            delete exch;
            exch = NULL;
            return;
        }

        hdr_handle = slot;
        headers[hdr_handle] = new HttpHeaderSlot(hdr);
        headers[hdr_handle]->convert_from_response(exch->get_header());

        // Create response objects and return two handles [cite: 258]
        data_message.message[0] = hdr_handle; // New Header Handle
        data_message.message[1] = body_handle; // New Body Handle
        data_message.length = 2;
        *reply = &data_message;

        // Status
        HTTPReqHeader *hdr = exch->get_header();
        if (hdr->Response) {
            *status = &status_message;
            status_message.length = snprintf((char *)status_message.message, CMD_MAX_STATUS_LEN, "%s", hdr->Response);
            hdr->Response = NULL;
        } else {
            *status = &c_status_http_ok;
        }

        // we should have taken all the data from the exchange
        delete exch;
        exch = NULL;
    }
}

void HttpTarget::get_more_data(Message **reply, Message **status)
{
    if (response_data) {
        *reply = &data_message;
        data_message.length = response_data->read((char *)data_message.message, CMD_MAX_REPLY_LEN);
        data_message.last_part = (data_message.length != CMD_MAX_REPLY_LEN);
        *status = &c_status_http_ok; 
        if (data_message.last_part) {
            delete response_data;
            response_data = NULL;
        }
    } else if (exch) {
        *reply = &data_message;
        data_message.length = exch->read_response_data(CMD_MAX_REPLY_LEN, data_message.message);
        data_message.last_part = (data_message.length != CMD_MAX_REPLY_LEN);
        if (data_message.last_part) {
            delete exch;
            exch = NULL;
        }
        *status = &c_status_http_ok;
    } else {
        *reply = &c_message_empty;
        *status = &c_status_no_more;
    }
}

void HttpTarget::abort(int a)
{
    // Reset any pending exchange state
    reset_responses();
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
        delete json;
        return -99;
    }
    bodies[slot] = new HttpBodySlot(json);
    *handle = slot;
    return j;
}
