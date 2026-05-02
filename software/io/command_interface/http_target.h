#ifndef HTTP_TARGET_H
#define HTTP_TARGET_H

#include <stdint.h>
#include <stdlib.h>
#include "mystring.h"
#include "json.h"
#include "command_target.h"
#include "pattern.h"
#include "http_request.h"

// Command Definitions
#define HTTP_CMD_IDENTIFY         0x01
#define HTTP_CMD_HEADER_CREATE    0x11
#define HTTP_CMD_HEADER_FREE      0x12
#define HTTP_CMD_HEADER_ADD       0x13
#define HTTP_CMD_HEADER_QUERY     0x14
#define HTTP_CMD_HEADER_LIST      0x15
#define HTTP_CMD_BODY_CREATE      0x21
#define HTTP_CMD_BODY_FREE        0x22
#define HTTP_CMD_BODY_ADD_INT     0x23
#define HTTP_CMD_BODY_ADD_BOOL    0x24
#define HTTP_CMD_BODY_ADD_STRING  0x25
#define HTTP_CMD_BODY_ADD_OBJECT  0x26
#define HTTP_CMD_BODY_ADD_ARRAY   0x27
#define HTTP_CMD_BODY_UP          0x28
#define HTTP_CMD_BODY_REMOVE      0x29
#define HTTP_CMD_BODY_QUERY       0x2A
#define HTTP_CMD_BODY_MOVE        0x2B
#define HTTP_CMD_BODY_ADD_BINARY  0x2C
#define HTTP_CMD_DO_EXCHANGE_OBJ  0x31
#define HTTP_CMD_DO_EXCHANGE_RAW  0x32

#define MAX_HTTP_HANDLES 16

#define HTTP_TYPE_BINARY      0x01
#define HTTP_TYPE_JSON_OBJ    0x02
#define HTTP_TYPE_JSON_ARRAY  0x03
#define HTTP_TYPE_URL_ENCODED 0x04

#define HTTP_DATA_INTEGER 0x01
#define HTTP_DATA_BOOL    0x02
#define HTTP_DATA_STRING  0x03
#define HTTP_DATA_OBJECT  0x04
#define HTTP_DATA_ARRAY   0x05

class HttpHeaderSlot
{
    uint8_t verb;
    bool secure;
    mstring host;
    mstring url;
    mstring renderspace;
    uint16_t port;
    JSON_Object lines;
public:
    HttpHeaderSlot(uint8_t v, const char *u, int ulen) : verb(v), lines(true)
    {
        // Force null-termination
        mstring text(u, 0, ulen-1);
        u = text.c_str();

        if(strncmp(u, "https://", 8) == 0) {
            port = 443;
            secure = true;
            host = u+8;
        } else if(strncmp(u, "http://", 7) == 0) {
            port = 80;
            secure = false;
            host = u+7;
        } else {
            port = 80;
            host = u;
            secure = false;
        }
        // first select sub-url
        const char *sub = host.split("/");
        if (sub) {
            url += sub;
        }
        // now try to find port number
        const char *portnr = host.split(":");
        if (portnr) {
            port = (uint16_t)strtol(portnr, NULL, 10);
        }
        set("Host", host.c_str());
    }

    HttpHeaderSlot(HttpHeaderSlot *src) : verb(src->verb), lines(true)
    {
        host = src->host.c_str();
        url = src->url.c_str();
        port = src->port;
        secure = src->secure;
    }

    ~HttpHeaderSlot()
    {
    }

    void set(const char *key, const char *value)
    {
        lines.set(key, value);
    }

    JSON *get(const char *key)
    {
        return lines.get(key);
    }

    JSON *get(int index, const char **key)
    {
        IndexedList<const char *> *keys = lines.get_keys();
        IndexedList<JSON *> *values = lines.get_values();
        if (index <= keys->get_elements()) {
            *key = (*keys)[index-1];
            return (*values)[index-1];
        }
        return NULL;
    }

    const char *get_host()
    {
        return host.c_str();
    }

    const uint16_t get_port()
    {
        return port;
    }

    void convert_from_response(HTTPReqHeader *hdr)
    {
        for(int i=0;i<hdr->FieldCount;i++) {
            set(hdr->Fields[i].key, hdr->Fields[i].value);
        }
    }

    void dump()
    {
        printf("Host = %s\n", host.c_str());
        printf("Port: %d\n", port);
        printf("URL = %s\n", url.c_str());
        printf("Content:\n%s\n", lines.render());

        // StreamRamFile s(800);
        // char buf[800] = { 0 };
        // render(&s);
        // int len = s.read(buf, 700);
        // printf("Length: %d\n", len);
        // buf[len] = 0;
        // printf(buf);
    }

    void render(StreamRamFile *s)
    {
        static const char *verbs[] = { "GET", "PUT", "POST", "PATCH", "DELETE", "HEAD", "OPTIONS", "CONNECT", "TRACE" };
        s->format("%s /%s HTTP/1.1\r\n", verbs[verb-1], url.c_str());

        IndexedList<const char *> *keys = lines.get_keys();
        IndexedList<JSON *> *values = lines.get_values();
        for(int i=0;i<keys->get_elements();i++) {
            const char *key = (*keys)[i];
            JSON *j = (*values)[i];
            if(j) {
                s->format("%s: ", key);
                if (j->type() == eString) {
                    s->format("%s\r\n", ((JSON_String *)j)->get_string());
                } else {
                    j->render(s);
                    s->format("\r\n");
                }
            }
        }
        s->format("\r\n");
    }

    const char *render()
    {
        static const char *verbs[] = { "GET", "PUT", "POST", "PATCH", "DELETE", "HEAD", "OPTIONS", "CONNECT", "TRACE" };
        renderspace = verbs[verb-1];
        renderspace += " /";
        renderspace += url;
        renderspace += "\n";        

        IndexedList<const char *> *keys = lines.get_keys();
        IndexedList<JSON *> *values = lines.get_values();
        for(int i=0;i<keys->get_elements();i++) {
            const char *key = (*keys)[i];
            JSON *j = (*values)[i];
            if(j) {
                renderspace += key;
                renderspace += ": ";
                if (j->type() == eString) {
                    renderspace += ((JSON_String *)j)->get_string();
                } else {
                    renderspace += j->render();
                }
                renderspace += "\n";
            }
        }
        renderspace += "\n";
        return renderspace.c_str();
    }

};

class HttpBodySlot {
    uint8_t format;
    JSON *root_object;
    JSON *current;
    StreamRamFile *binary_blob;
public:
    HttpBodySlot(uint8_t fmt) : format(fmt)
    {
        root_object = NULL;
        binary_blob = NULL;
        switch(fmt) {
        case HTTP_TYPE_BINARY:
            binary_blob = new StreamRamFile(512);
            break;
        case HTTP_TYPE_JSON_OBJ: // object
        case HTTP_TYPE_URL_ENCODED: // url
            root_object = new JSON_Object(true);
            break;
        case HTTP_TYPE_JSON_ARRAY: // list
            root_object = JSON::List();
            break;
        default:
            root_object = new JSON_Object(true);
            format = HTTP_TYPE_JSON_OBJ;
        }
        current = root_object;
    }

    HttpBodySlot(JSON *j)
    {
        binary_blob = NULL;
        root_object = current = j;
        if (j) {
            if (j->type() == eObject) {
                format = HTTP_TYPE_JSON_OBJ;
            } else if (j->type() == eList) {
                format = HTTP_TYPE_JSON_ARRAY;
            }
        }
    }

    ~HttpBodySlot()
    {
        if (root_object) {
            delete root_object;
        }
        if (binary_blob) {
            delete binary_blob;
        }
    }

    uint8_t get_format(void) {
        return format;
    }

    void render(StreamRamFile *s)
    {
        switch(format) {
        case HTTP_TYPE_BINARY:
            s->copy_from(binary_blob);
            break;
        case HTTP_TYPE_JSON_ARRAY:
        case HTTP_TYPE_JSON_OBJ:
            if (root_object) {
                root_object->render(s);
            }
            s->format("\n");
            break;
        case HTTP_TYPE_URL_ENCODED:
            if (root_object) {
                root_object->url(s);
            }
            s->format("\n");
            break;         
        }
    }

    JSON *add_int(const char *key, int value) {
        if (!root_object) {
            root_object = new JSON_Object(true);
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            return ((JSON_Object *)current)->set(key, value);
        } else if (current->type() == eList) {
            return ((JSON_List *)current)->add(value);
        }
        return NULL;
    }

    JSON *add_bool(const char *key, bool value) {
        if (!root_object) {
            root_object = new JSON_Object(true);
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            return ((JSON_Object *)current)->set(key, value);
        } else if (current->type() == eList) {
            return ((JSON_List *)current)->add(value);
        }
        return NULL;
    }

    JSON *add_str(const char *key, const char *value) {
        if (!root_object) {
            root_object = new JSON_Object(true);
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            return ((JSON_Object *)current)->set(key, value);
        } else if (current->type() == eList) {
            return ((JSON_List *)current)->add(value);
        }
        return NULL;
    }

    JSON *add_obj(const char *key) {
        if (!root_object) {
            current = root_object = new JSON_Object(true);
            return root_object;
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            JSON_Object *co = (JSON_Object *)current;
            // printf("Adding object %s to object. %s\n", key, co->render());
            JSON_Object *obj = new JSON_Object(true);
            co->add(key, obj);
            current = obj;
            return current;
        } else if (current->type() == eList) {
            JSON_List *cl = (JSON_List *)current;
            // printf("Adding object %s to array %s.\n", key, cl->render());
            JSON_Object *obj = new JSON_Object(true);
            cl->add(obj);
            current = obj;
            return current;
        }
        return NULL;
    }

    JSON *add_array(const char *key) {
        if (!root_object) {
            current = root_object = new JSON_List();
            return root_object;
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            JSON_Object *co = (JSON_Object *)current;
            // printf("Adding array %s to object %s.\n", key, co->render());
            JSON_List *obj = JSON::List();
            co->add(key, obj);
            current = obj;
            return current;
        } else if (current->type() == eList) {
            JSON_List *cl = (JSON_List *)current;
            // printf("Adding array %s to list %s.\n", key, cl->render());
            JSON_List *obj = JSON::List();
            cl->add(obj);
            current = obj;
            return current;
        }
        return NULL;
    }

    void add_binary(uint8_t *data, int length)
    {
        binary_blob->write(data, length);
    }

    void move_up(void)
    {
        if (!current) {
            current = root_object;
        }
        if(current == root_object) {
            return;
        }
        if(current->parent) {
            current = current->parent;
        }
    }

    void remove(JSON *j)
    {
        if (j && j->parent) {
            if (j->parent->type() == eObject) {
                JSON_Object *p = (JSON_Object  *)j->parent;
                p->remove(j);
                delete j;
            } else if (j->parent->type() == eList) {
                JSON_List *p = (JSON_List  *)j->parent;
                p->remove(j);
                delete j;
            }
        }
    }

    void set_current(JSON *j)
    {
        current = j;
    }

    static int read_decimals(const char *buf, int *val)
    {
        *val = 0;
        int consumed = 0;
        while((*buf >= '0') && (*buf <= '9')) {
            *val *= 10;
            *val += (*buf++)-'0';
            consumed++;
        }
        return consumed;
    }

    JSON *walk_path(mstring& path, char *error)
    {
        JSON *cur = root_object;

        if (path.length() == 0) {
            if (cur) {
                return cur;
            } else {
                sprintf(error, "500 INTERNAL ERROR: CURRENT OBJECT IS NULL");
                return NULL;
            }
        }

        char *dup = strdup(path.c_str());
        char *start = dup;
        char *pnt = dup;
        bool last, bracket;

        do {
            if (!cur) {
                free(dup);
                sprintf(error, "500 INTERNAL ERROR: CURRENT OBJECT IS NULL");
                return NULL;
            }
            bracket = (*pnt == '[');
            if ((*pnt == '%')|| bracket) {
                *pnt = 0;                
                // printf("% encountered. Pre = '%s' Cur = %s\n", start, cur->render());
                if (strlen(start)) { // string preceeding index, first look up key
                    if (cur->type() == eObject) {
                        cur = ((JSON_Object *)cur)->get(start);
                    } else {
                        sprintf(error, "400 BAD FORMAT: ELEMENT IS NOT AN OBJECT (%s)", start);
                        free(dup);
                        return NULL;
                    }
                    if (!cur) {
                        sprintf(error, "400 BAD FORMAT: ENTRY %s NOT FOUND", start);
                        free(dup);
                        return NULL;
                    }
                }
                pnt++;
                int idx = 0;
                int skip = read_decimals(pnt, &idx);
                if (cur->type() != eList) {
                    sprintf(error, "400 BAD FORMAT: ELEMENT IS NOT INDEXABLE (%d)", idx);
                    free(dup);
                    return NULL;
                }
                JSON_List *lst = (JSON_List *)cur;
                cur = (*lst)[idx];
                if (!cur) {
                    sprintf(error, "400 BAD FORMAT: INDEX %d OUT OF BOUNDS", idx);
                    free(dup);
                    return NULL;
                }

                // printf("Index was %d. Cur = %s\n", idx, cur->render());
                pnt += skip;
                // printf("After index: %s\n", pnt);
                if (bracket) {
                    if (*pnt != ']') {
                        sprintf(error, "400 BAD FORMAT: EXPECTED ']' AFTER INDEX AT %d", pnt - dup);
                        free(dup);
                        return NULL;
                    } else {
                        pnt++;
                    }
                } else if((*pnt != 0) && (*pnt != '/')) {
                    sprintf(error, "400 BAD FORMAT: EXPECTED '/' AFTER INDEX AT %d", pnt - dup);
                    free(dup);
                    return NULL;
                }
                if (*pnt) { // if not at the end of the string, skip /
                    pnt++;
                    start = pnt;
                } else {
                    break;
                }
            } else if (*pnt == 0 || *pnt == '/') { // path separator or end of path
                last = (*pnt == 0);
                *pnt = 0;
                // printf("/ or end encountered. Pre = '%s', Cur = %s\n", start, cur->render());
                if (strlen(start)) {
                    if (cur->type() == eObject) {
                        cur = ((JSON_Object *)cur)->get(start);
                    } else {
                        sprintf(error, "400 BAD FORMAT: ELEMENT IS NOT AN OBJECT (%s)", start);
                        free(dup);
                        return NULL;
                    }
                    if (!cur) {
                        sprintf(error, "400 BAD FORMAT: ELEMENT %s NOT FOUND", start);
                        free(dup);
                        return NULL;
                    }
                } else {
                    sprintf(error, "400 BAD FORMAT: EMPTY SEARCH STRING");
                    free(dup);
                    return NULL;
                }
                // printf("After selection: %s\n", cur->render());
                if (last) {
                    break;
                } else {
                    pnt++;
                    start = pnt;
                }
            } else {
                pnt++;
            }
            // printf("Pnt = '%s'\n", pnt);
        } while(1);

        free(dup);
        return cur;
    }

    void dump(void)
    {
        if(root_object) {
            printf("%s\n", root_object->render());
            printf("In URL format: %s\n", root_object->url());
        } else {
            printf("No root object\n");
        }
    }
};

class HttpTarget : public CommandTarget {
    Message data_message;
    Message status_message;
    
    HttpHeaderSlot *headers[MAX_HTTP_HANDLES];
    HttpBodySlot *bodies[MAX_HTTP_HANDLES];
    HttpRequest *exch;
    StreamRamFile *response_data;

    // Helper methods for command processing
    void cmd_header_create(Message *command, Message **reply, Message **status);
    void cmd_header_add(Message *command, Message **reply, Message **status);
    void cmd_header_query(Message *command, Message **reply, Message **status);
    void cmd_header_list(Message *command, Message **reply, Message **status);

    void cmd_body_create(Message *command, Message **reply, Message **status);
    void cmd_body_add_primitive(Message *command, Message **reply, Message **status, uint8_t type);
    void cmd_body_add_binary(Message *command, Message **reply, Message **status);
    void cmd_body_up(Message *command, Message **reply, Message **status);
    void cmd_body_move(Message *command, Message **reply, Message **status);
    void cmd_body_remove(Message *command, Message **reply, Message **status);
    void cmd_body_query(Message *command, Message **reply, Message **status);
    void cmd_exchange(Message *command, Message **reply, Message **status, bool raw);

    void express(JSON *j);
    void reset_responses();
public:
    HttpTarget(int id);
    virtual ~HttpTarget();

    void parse_command(Message *command, Message **reply, Message **status) override;
    void get_more_data(Message **reply, Message **status) override;
    void abort(int) override;

    int create_body_from_json(char *body, int size, uint8_t *handle);
};

#endif
