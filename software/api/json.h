#ifndef _JSON_H
#define _JSON_H
#include "mystring.h"
#include "dict.h"
#include "indexed_list.h"
#include <stdio.h>
#include "stream_ramfile.h"

class JSON_Object;
class JSON_List;

class JSON
{
public:
    JSON() { }
    virtual const char *render() {
        return "base?";
    }
    virtual void render(StreamRamFile *s) {
        s->format("base?");
    }
    virtual ~JSON() { }
    static JSON_List *List();
    static JSON_Object *Obj();
};

class JSON_String : public JSON
{
    mstring str;
public:
    JSON_String(const char *s) {
        str = "\"";
        str += s;
        str += "\"";
    }
    const char *render() { return str.c_str(); }
    void render(StreamRamFile *s) { s->format("%s", str.c_str()); }
};

class JSON_Integer : public JSON
{
    int value;
    char work[12];
public:
    JSON_Integer(int v) : value(v) {}
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
};

class JSON_List : public JSON
{
    IndexedList<JSON *> members;
    mstring renderspace;
public:
    JSON_List() : members(4, NULL) { }

    ~JSON_List() {
        for (int i=0;i<members.get_elements();i++) {
            delete members[i];
        }
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


#endif