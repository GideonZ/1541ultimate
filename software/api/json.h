#ifndef _JSON_H
#define _JSON_H
#include "mystring.h"
#include "dict.h"
#include "indexed_list.h"
#include <stdio.h>

class JSON_Object;
class JSON_List;

class JSON
{
public:
    JSON() { }
    virtual const char *render() {
        return "base?";
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
};

class JSON_Object : public JSON
{
    Dict<const char *, JSON *> members;
    mstring renderspace;
public:
    JSON_Object() : members(4, NULL, NULL, NULL) { }

    ~JSON_Object() {
        for (int i=0;i<members.get_elements();i++) {
            delete members.get_value(i);
        }
    }

    JSON_Object *add(const char *key, JSON *value) {
        members.set(key, value);
        return this;
    }

    JSON_Object *add(const char *key, const char *str) {
        members.set(key, new JSON_String(str));
        return this;
    }

    JSON_Object *add(const char *key, int val) {
        members.set(key, new JSON_Integer(val));
        return this;
    }

    const char *render() {
        renderspace = "{ \n";
        for (int i=0;i<members.get_elements();i++) {
            renderspace += "  \"";
            renderspace += members.get_key(i);
            renderspace += "\" : ";
            renderspace += members.get_value(i)->render();
            if (i != members.get_elements()-1) {
                renderspace += ",";
            }
            renderspace += "\n";
        }
        renderspace += "}";
        return renderspace.c_str();
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
};


#endif