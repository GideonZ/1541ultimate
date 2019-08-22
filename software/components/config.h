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
#include "filemanager.h"
#include "menu.h"

#define CFG_TYPE_VALUE  0x01
#define CFG_TYPE_ENUM   0x02
#define CFG_TYPE_STRING 0x03
#define CFG_TYPE_END    0xFF


struct t_cfg_definition
{
    uint8_t id;
    uint8_t type;
    const char *item_text;
    const char *item_format;
    const char **items;
    int  min, max;
    int  def;
};

class ConfigStore;
class ConfigurableObject;
class ConfigItem;
typedef void (*t_change_hook)(ConfigItem *);

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
	t_change_hook hook;
	bool enabled;
public:    
    ConfigStore *store;
    t_cfg_definition *definition;
    int     value;
    char    *string;

    ConfigItem(ConfigStore *s, t_cfg_definition *d);
    ~ConfigItem();

    int pack(uint8_t *buffer, int len);
    void unpack(uint8_t *buffer, int len);

    const char *get_item_name() { return definition->item_text; }
    const char *get_display_string(char *buffer, int width);
    int  fetch_possible_settings(IndexedList<ConfigSetting *> &list);
    void execute(int sel);
    void setChanged(void);
    void setChangeHook(t_change_hook hook) { this->hook = hook; }
    bool isEnabled(void) { return enabled; }
    void setEnabled(bool en) { enabled = en; }
};


class ConfigStore
{
    int    flash_page;
    uint8_t  *mem_block;
    int    block_size;
    IndexedList<ConfigurableObject *> objects;
//    ConfigurableObject *obj;
    mstring store_name;
    
    void pack(void);
    void unpack(void);
public:
    IndexedList <ConfigItem*> items;
    uint32_t id;
    bool  dirty;

    ConfigStore(uint32_t id, const char *name, int page, int page_size, t_cfg_definition *defs, ConfigurableObject *obj);
    virtual ~ConfigStore();
    void addObject(ConfigurableObject *obj);
    int  unregister(ConfigurableObject *obj);

// Interface functions
    virtual void read(void);
    virtual void write(void);
    virtual void effectuate(void);

    int  get_page(void) { return flash_page; }
    int  get_page_size(void) { return block_size; }

    void set_change_hook(uint8_t id, t_change_hook hook);
    void disable(uint8_t id);

    ConfigItem *find_item(uint8_t id);
    int  get_value(uint8_t id);
    const char *get_store_name() { return store_name.c_str(); }
    const char *get_string(uint8_t id);
    void set_value(uint8_t id, int value);
    void set_string(uint8_t id, char *s);
    void dump(void);
    void check_bounds(void);
    void write_to_file(File *f);

    IndexedList <ConfigItem *> *getItems() { return &items; }
};
    
class ConfigManager : public ObjectWithMenu
{
	IndexedList<ConfigStore*> stores;
    int num_pages;
    Flash *flash;

    ConfigManager();
    ~ConfigManager();
public:
	static ConfigManager* getConfigManager() {
		static ConfigManager config_manager;
		return &config_manager;
	}
    
    ConfigStore *register_store(uint32_t store_id, const char *name, t_cfg_definition *defs, ConfigurableObject *ob);
    ConfigStore *open_store(uint32_t store_id);
    void add_custom_store(ConfigStore *cfg);
    void remove_store(ConfigStore *cfg);
    void write_to_file(File *f);

	Flash *get_flash_access(void) { return flash; }
	IndexedList<ConfigStore*> *getStores() { return &stores; }

    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
    static int S_save_to_file(SubsysCommand *cmd);
};


// Base class for any configurable object
class ConfigurableObject
{
public:
    ConfigStore *cfg;

    ConfigurableObject() {
    	cfg = NULL;
    }

    virtual ~ConfigurableObject() {
        if(cfg) {
            int remainingObjects = cfg->unregister(this);
            if (!remainingObjects) {
                ConfigManager :: getConfigManager()->remove_store(cfg);
                delete cfg;
            }
        }
    }
    
    virtual bool register_store(uint32_t store_id, const char *name, t_cfg_definition *defs)
    {
        cfg = ConfigManager :: getConfigManager()->register_store(store_id, name, defs, this);
        return (cfg != NULL);
    }

    virtual void effectuate_settings() { }
};

#endif
