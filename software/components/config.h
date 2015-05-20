/*
 *   1541 Ultimate - The Storage Solution for your Commodore computer.
 *   Copyright (C) 2009  Gideon Zweijtzer - Gideon's Logic Architectures
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Abstract:
 *   Definitions of the existing configuration items, structs and functions.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "integer.h"
#include "flash.h"
#include "indexed_list.h"
#include "mystring.h"
//#include "path.h"

#define CFG_TYPE_VALUE  0x01
#define CFG_TYPE_ENUM   0x02
#define CFG_TYPE_STRING 0x03
#define CFG_TYPE_END    0xFF


struct t_cfg_definition
{
    uint8_t id;
    uint8_t type;
    char *item_text;
    char *item_format;
    char **items;
    int  min, max;
    int  def;
};

class ConfigStore;
class ConfigurableObject;
class ConfigItem;

class ConfigSetting
{
public:
	int	setting_index;
	ConfigItem *parent;
	mstring setting_name;

	ConfigSetting(int index, ConfigItem *parent, char *name);
};

class ConfigItem
{
public:    
    ConfigStore *store;
    t_cfg_definition *definition;
    int     value;
    char    *string;

    ConfigItem(ConfigStore *s, t_cfg_definition *d);
    ~ConfigItem();

    int pack(uint8_t *buffer, int len);
    void unpack(uint8_t *buffer, int len);

    char *get_item_name() { return definition->item_text; }
    char *get_display_string();
    int  fetch_possible_settings(IndexedList<ConfigSetting *> &list);
    void execute(int sel);
};

class ConfigStore
{
    int    flash_page;
    uint8_t  *mem_block;
    int    block_size;
    ConfigurableObject *obj;
    char  *store_name;
    
    void pack(void);
    void unpack(void);
public:
    IndexedList <ConfigItem*> items;
    uint32_t id;
    bool  dirty;

    ConfigStore(uint32_t id, char *name, int page, int page_size, t_cfg_definition *defs, ConfigurableObject *obj);
    virtual ~ConfigStore();

// Interface functions
    virtual void read(void);
    virtual void write(void);
    virtual void effectuate(void);

    ConfigItem *find_item(uint8_t id);
    int  get_value(uint8_t id);
    char *get_store_name() { return store_name; }
    char *get_string(uint8_t id);
    void set_value(uint8_t id, int value);
    void set_string(uint8_t id, char *s);
    void dump(void);
    void check_bounds(void);
    
    IndexedList <ConfigItem *> *getItems() { return &items; }
};
    
class ConfigManager
{
	IndexedList<ConfigStore*> stores;
    int num_pages;
    Flash *flash;
public:
    ConfigManager();
    ~ConfigManager();
    
    ConfigStore *register_store(uint32_t store_id, char *name, t_cfg_definition *defs, ConfigurableObject *ob);
    ConfigStore *open_store(uint32_t store_id);
    void add_custom_store(ConfigStore *cfg);
    void remove_store(ConfigStore *cfg);

	Flash *get_flash_access(void) { return flash; }
	IndexedList<ConfigStore*> *getStores() { return &stores; }
};

extern ConfigManager config_manager; // global!

// Base class for any configurable object

class ConfigurableObject
{
public:
    ConfigStore *cfg;

    ConfigurableObject() { }
    virtual ~ConfigurableObject() { }
    
    virtual bool register_store(uint32_t store_id, char *name, t_cfg_definition *defs)
    {
        cfg = config_manager.register_store(store_id, name, defs, this);
        return (cfg != NULL);
    }
    
    virtual void effectuate_settings() { }
};

#endif
