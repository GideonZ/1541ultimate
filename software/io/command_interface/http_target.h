#ifndef HTTP_TARGET_H
#define HTTP_TARGET_H

#include <stdint.h>
#include <stdlib.h>
#include "mystring.h"
#include "json.h"
#include "command_target.h"

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

class HttpHeaderSlot
{
    uint8_t verb;
    bool secure;
    mstring host;
    mstring url;
    uint16_t port;
    JSON_Object lines;
public:
    HttpHeaderSlot(uint8_t v, const char *u, int ulen) : verb(v), lines(true)
    {
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
        s->format("%s /%s HTTP/1.1\n", verbs[verb-1], url.c_str());

        IndexedList<const char *> *keys = lines.get_keys();
        IndexedList<JSON *> *values = lines.get_values();
        for(int i=0;i<keys->get_elements();i++) {
            const char *key = (*keys)[i];
            JSON *j = (*values)[i];
            if(j) {
                s->format("%s: ", key);
                if (j->type() == eString) {
                    s->format("%s\n", ((JSON_String *)j)->get_string());
                } else {
                    j->render(s);
                    s->format("\n");
                }
            }
        }
        s->format("\n");
    }
};

class HttpBodySlot {
    uint8_t format;
    JSON *root_object;
    JSON *current;
public:
    HttpBodySlot(uint8_t fmt) : format(fmt)
    {
        root_object = NULL;
        current = NULL;
    }
    ~HttpBodySlot()
    {
        if (root_object) {
            delete root_object;
        }
    }

    JSON *add_int(const char *key, int value) {
        if (!root_object) {
            root_object = new JSON_Object();
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
            root_object = new JSON_Object();
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
            root_object = new JSON_Object();
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
            current = root_object = new JSON_Object();
            return root_object;
        }
        if (!current) {
            current = root_object;
        }
        if (current->type() == eObject) {
            JSON_Object *obj = JSON::Obj();
            ((JSON_Object *)current)->add(key, obj);
            current = obj;
            return current;
        } else if (current->type() == eList) {
            JSON_Object *obj = JSON::Obj();
            return ((JSON_List *)current)->add(obj);
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
            JSON_List *obj = JSON::List();
            ((JSON_Object *)current)->add(key, obj);
            current = obj;
            return current;
        } else if (current->type() == eList) {
            JSON_List *obj = JSON::List();
            return ((JSON_List *)current)->add(obj);
            current = obj;
            return current;
        }
        return NULL;
    }
};

class HttpTarget : public CommandTarget {
    Message data_message;
    Message status_message;
    
    HttpHeaderSlot *headers[MAX_HTTP_HANDLES];
    HttpBodySlot *bodies[MAX_HTTP_HANDLES];

    // Helper methods for command processing
    void cmd_header_create(Message *command, Message **reply, Message **status);
    void cmd_header_add(Message *command, Message **reply, Message **status);
    void cmd_header_query(Message *command, Message **reply, Message **status);
    void cmd_header_list(Message *command, Message **reply, Message **status);

    void cmd_body_create(Message *command, Message **reply, Message **status);
    void cmd_body_add_primitive(Message *command, Message **reply, Message **status, uint8_t type);
    void cmd_exchange(Message *command, Message **reply, Message **status, bool raw);

public:
    HttpTarget(int id);
    virtual ~HttpTarget();

    void parse_command(Message *command, Message **reply, Message **status) override;
    void get_more_data(Message **reply, Message **status) override;
    void abort(int) override;
};

#endif
