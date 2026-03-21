/*
 * config_collection.h
 *
 *  Created on: Sep 20, 2025
 *      Author: gideon
 */

#ifndef CONFIG_COLLECTION_H_
#define CONFIG_COLLECTION_H_

#include <stdint.h>
#include "mystring.h"
#include "indexed_list.h"
#include "config.h"

class ConfigGroup
{
    mstring name;
    const int sortOrder;
    IndexedList<ConfigItem *> cfgItems;
    IndexedList<ConfigStore *> referencedStores;
public:
    ConfigGroup(const char *catname, int so) : cfgItems(8, NULL), referencedStores(4, NULL), name(catname), sortOrder(so) {

    }
    ~ConfigGroup();

    const char *getName(void) { return name.c_str(); }
    const int getOrder(void) { return sortOrder; }
    IndexedList<ConfigItem *> *getConfigItems(void) { return &cfgItems; }
    IndexedList<ConfigStore *> *getStores(void) { return &referencedStores; }
    void append(ConfigItem *a)
    {
        cfgItems.append(a);
        if (!a->store) return; // separators for instance
        bool found = false;
        for(int i=0;i<referencedStores.get_elements();i++) {
            if (referencedStores[i] == a->store) found = true;
        }
        if (!found) {
            referencedStores.append(a->store);
        }
    }
};

class ConfigGroupCollection
{
    IndexedList<ConfigGroup *> groups;
    ConfigGroupCollection() : groups(8, NULL) { };
public:
    static ConfigGroupCollection *getConfigGroupCollection(void) {
        static ConfigGroupCollection *col = NULL;
        if (!col) {
            col = new ConfigGroupCollection();
        }
        return col;
    }

    ~ConfigGroupCollection() {
        for (int i=0; i < groups.get_elements(); i++) {
            ConfigGroup *c = groups[i];
            if (c) {
                delete c;
            }
        }
    }

    static ConfigGroup *getGroup(const char *name, int sortOrder) {
        ConfigGroupCollection *col = getConfigGroupCollection();
        for (int i=0; i < col->groups.get_elements(); i++) {
            ConfigGroup *c = col->groups[i];
            if (c) {
                if (strcmp(c->getName(), name) == 0) {
                    return c;
                }
            }
        }
        if (sortOrder >= 0) {
            ConfigGroup *c = new ConfigGroup(name, sortOrder);
            col->groups.append(c);
            return c;
        }
        return NULL;
    }



    static IndexedList<ConfigGroup *> *getGroups() {
        ConfigGroupCollection *col = getConfigGroupCollection();
        return &(col->groups);
    }

    static int compare(IndexedList<ConfigGroup *> *list, int a, int b) {
        ConfigGroup *ca = (*list)[a];
        ConfigGroup *cb = (*list)[b];
        if (!ca || !cb) {
            return 0;
        }
        return ca->getOrder() - cb->getOrder();
    }

    static void sort(void) {
        ConfigGroupCollection *col = getConfigGroupCollection();
        col->groups.sort(ConfigGroupCollection :: compare);
    }
};

#endif /* CONFIG_COLLECTION_H_ */
