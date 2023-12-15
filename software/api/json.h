#ifndef _JSON_H
#define _JSON_H
#include "mystring.h"
#include "dict.h"
#include "indexed_list.h"
#include <stdio.h>
#include "stream_ramfile.h"

class JSON_Object;
class JSON_List;

typedef enum {
    eBase = 0,
    eString,
    eInteger,
    eBool,
    eObject,
    eList,
} JsonType_t;

class JSON
{
  public:
    JSON() {}
    virtual const char *render() { return "base?"; }
    virtual void render(StreamRamFile *s) { s->format("base?"); }
    virtual ~JSON() {}
    virtual JsonType_t type() { return eBase; }
    static JSON_List *List();
    static JSON_Object *Obj();
};

class JSON_String : public JSON
{
    mstring str;
    mstring renderspace;
public:
    JSON_String(const char *s) {
        str += s;
    }
    JsonType_t type() { return eString; }
    const char *get_string() { return str.c_str(); }
    const char *render() {
        renderspace = "\"";
        renderspace += str;
        renderspace += "\"";
        return renderspace.c_str();
    }
    void render(StreamRamFile *s) { s->format("\"%s\"", str.c_str()); }
};

class JSON_Integer : public JSON
{
    int value;
    char work[12];
public:
    JSON_Integer(int v) : value(v) {}
    JsonType_t type() { return eInteger; }
    const char *render() {
        sprintf(work, "%d", value);
        return work;
    }
    void render(StreamRamFile *s) { s->format("%d", value); }
};

class JSON_Bool : public JSON
{
    bool value;
public:
    JSON_Bool(int v) : value(v) {}
    JsonType_t type() { return eBool; }
    const char *render() {
        return value ? "true" : "false";
    }
    void render(StreamRamFile *s) {
        s->format(value ? "true" : "false");
    }
};

class JSON_Object : public JSON
{
    IndexedList<const char *>keys;
    IndexedList<JSON *>values;
    mstring renderspace;
public:
    JSON_Object() : keys(4, NULL), values(4, NULL) { }
    JsonType_t type() { return eObject; }

    ~JSON_Object() {
        for (int i=0;i<values.get_elements();i++) {
            delete values[i];
        }
    }

    JSON_Object *add(const char *key, JSON *value) {
        keys.append(key);
        values.append(value);
        return this;
    }

    JSON_Object *add(const char *key, const char *str) {
        return add(key, new JSON_String(str));
    }

    JSON_Object *add(const char *key, int val) {
        return add(key, new JSON_Integer(val));
    }

    JSON_Object *add(const char *key, const bool val) {
        return add(key, new JSON_Bool(val));
    }

    const char *render() {
        renderspace = "{ \n";
        for (int i=0;i<keys.get_elements();i++) {
            renderspace += "  \"";
            renderspace += keys[i];
            renderspace += "\" : ";
            renderspace += values[i]->render();
            if (i != keys.get_elements()-1) {
                renderspace += ",";
            }
            renderspace += "\n";
        }
        renderspace += "}";
        return renderspace.c_str();
    }

    void render(StreamRamFile *s) {
        s->format("{ \n");
        for (int i=0;i<keys.get_elements();i++) {
            s->format("  \"%s\" : ", keys[i]);
            values[i]->render(s);
            if (i != keys.get_elements()-1) {
                s->charout(',');
            }
            s->charout('\n');
        }
        s->charout('}');
    }

    JSON * get(const char *key) {
        for (int i=0;i<keys.get_elements();i++) {
            if (strcasecmp(key, keys[i]) == 0) {
                return values[i];
            }
        }
        return NULL;
    }

    IndexedList<const char *> *get_keys() {
        return &keys;
    }

    IndexedList<JSON *> *get_values() {
        return &values;
    }
};

class JSON_List : public JSON
{
    IndexedList<JSON *> members;
    mstring renderspace;
public:
    JSON_List() : members(4, NULL) { }
    JsonType_t type() { return eList; }

    ~JSON_List() {
        for (int i=0;i<members.get_elements();i++) {
            delete members[i];
        }
    }

    int get_num_elements() {
        return members.get_elements();
    }
    JSON * operator[] (int i) {
        return members[i];
    }

    JSON_List *add(JSON *value) {
        members.append(value);
        return this;
    }

    JSON_List *add(const char *str) {
        members.append(new JSON_String(str));
        return this;
    }

    JSON_List *add(int val) {
        members.append(new JSON_Integer(val));
        return this;
    }

    JSON_List *add(const bool val) {
        members.append(new JSON_Bool(val));
        return this;
    }

    const char *render() {
        renderspace = "[ ";
        for (int i=0;i<members.get_elements();i++) {
            renderspace += members[i]->render();
            if (i != members.get_elements()-1) {
                renderspace += ", ";
            }
        }
        renderspace += " ]";
        return renderspace.c_str();
    }

    void render(StreamRamFile *s) {
        s->format("[ ");
        for (int i=0;i<members.get_elements();i++) {
            members[i]->render(s);
            if (i != members.get_elements()-1) {
                s->format(", ");
            }
        }
        s->format(" ]");
    }

};

int convert_text_to_json_objects(char *text, size_t text_size, size_t max_tokens, JSON **out);

#endif