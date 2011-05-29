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
#include "path.h"

#define CFG_TYPE_VALUE  0x01
#define CFG_TYPE_ENUM   0x02
#define CFG_TYPE_STRING 0x03
#define CFG_TYPE_END    0xFF


struct t_cfg_definition
{
    BYTE id;
    BYTE type;
    char *item_text;
    char *item_format;
    char **items;
    int  min, max;
    int  def;
};

class ConfigStore;
class ConfigurableObject;

class ConfigItem : public PathObject
{
public:    
    ConfigStore *store;
    t_cfg_definition *definition;
    int     value;
    char    *string;

    ConfigItem(ConfigStore *s, t_cfg_definition *d);
    ~ConfigItem();

    int pack(BYTE *buffer, int len);
    void unpack(BYTE *buffer, int len);

    char *get_name()  { return "blah"; } //definition->item_text; }
    char *get_display_string();
    int  fetch_context_items(IndexedList<PathObject*> &list);
    void execute(int sel);
};

class ConfigStore : public PathObject
{
    int    flash_page;
    BYTE  *mem_block;
    int    block_size;
    ConfigurableObject *obj;
    
    void pack(void);
    void unpack(void);
public:
//    t_cfg_definition *definitions;
//    List <ConfigItem*> items;
    DWORD id;
    bool  dirty;

    ConfigStore(DWORD id, char *name, int page, int page_size, t_cfg_definition *defs, ConfigurableObject *obj);
    virtual ~ConfigStore();

// Interface functions
    virtual void read(void);
    virtual void write(void);
    virtual void effectuate(void);

    ConfigItem *find_item(BYTE id);
    int  get_value(BYTE id);
    char *get_string(BYTE id);
    void set_value(BYTE id, int value);
    void set_string(BYTE id, char *s);
    void dump(void);
    void check_bounds(void);
    
// Browse functions:    
    virtual int  fetch_children(void);
};
    
class ConfigManager : public PathObject
{
//    List<ConfigStore*> stores;
    int num_pages;
    Flash *flash;
public:
    ConfigManager();
    ~ConfigManager();
    
    ConfigStore *register_store(DWORD store_id, char *name, t_cfg_definition *defs, ConfigurableObject *ob);
    ConfigStore *open_store(DWORD store_id);
    void add_custom_store(ConfigStore *cfg);
    void remove_store(ConfigStore *cfg);

	Flash *get_flash_access(void) { return flash; }

// Browser functions
    int  fetch_children(void);
    char *get_display_string()  { return "Configuration"; }
};

extern ConfigManager config_manager; // global!
//extern TreeNode config_root;

// Base class for any configurable object

class ConfigurableObject
{
public:
    ConfigStore *cfg;

    ConfigurableObject() { }
    virtual ~ConfigurableObject() { }
    
    virtual bool register_store(DWORD store_id, char *name, t_cfg_definition *defs)
    {
        cfg = config_manager.register_store(store_id, name, defs, this);
        return (cfg != NULL);
    }
    
    virtual void fetch_settings() { }
    virtual void effectuate_settings() { }
};

#endif
