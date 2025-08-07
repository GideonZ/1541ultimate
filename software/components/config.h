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

#include <stdint.h>
#include "flash.h"
#include "indexed_list.h"
#include "mystring.h"
#include "file.h"

#define CFG_FILEPATH "/flash/config"

#define CFG_TYPE_VALUE   0x01
#define CFG_TYPE_ENUM    0x02
#define CFG_TYPE_STRING  0x03
#define CFG_TYPE_FUNC    0x04
#define CFG_TYPE_SEP     0x05
#define CFG_TYPE_INFO    0x06
#define CFG_TYPE_STRFUNC 0x07
#define CFG_TYPE_STRPASS 0x08
#define CFG_TYPE_END     0xFF

class UserInterface;

struct t_cfg_definition
{
    uint8_t id;
    uint8_t type;
    const char *item_text;
    const char *item_format;
    const char **items;
    int  min, max;
    long int def; // also used as pointer to default string. For 64 bit systems it needs to be a long!
};

class ConfigPage;
class ConfigStore;
class ConfigurableObject;
class ConfigItem;
class Action;

typedef int (*t_change_hook)(ConfigItem *);
typedef void (*t_cfg_func)(UserInterface *, ConfigItem *);
typedef void (*t_cfg_strfunc)(ConfigItem *, IndexedList<char *> &strings);

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
    int  value;
    char *string;

    int setChanged(void);
    int  pack(uint8_t *buffer, int len);
    void unpack(uint8_t *buffer, int len);
    void reset(void);
public:    
    ConfigStore *store;
    const t_cfg_definition *definition;

    ConfigItem(ConfigStore *s, t_cfg_definition *d);
    ~ConfigItem();

    const char *get_item_name() { return definition->item_text; }
    const char *get_display_string(char *buffer, int width);
    int  fetch_possible_settings(IndexedList<ConfigSetting *> &list);
    void execute(int sel);
    void setChangeHook(t_change_hook hook) { this->hook = hook; }
    bool isEnabled(void) { return enabled; }
    void setEnabled(bool en) { enabled = en; }

    const int getValue() { return value; }
    const char *getString() { return string; }
    int  setValue(int v) { value = v; return setChanged(); }
    void setValueQuietly(int v) { value = v; }
    void setString(const char *str);
    int next(int);
    int previous(int);

    friend class ConfigStore;
    friend class RtcConfigStore;
    friend class ConfigIO; // this is an extension to config.cc, so direct access is granted
};


class ConfigStore
{
    IndexedList<ConfigurableObject *> objects;
    mstring store_name;
    ConfigPage *page;
    bool  staleEffect;
    bool  staleFlash;
    
    int  pack(uint8_t *buffer, int len);
    void unpack(uint8_t *buffer, int len);
public:
    IndexedList <ConfigItem*> items;

    ConfigStore(ConfigPage *page, const char *name, t_cfg_definition *defs, ConfigurableObject *obj);
    virtual ~ConfigStore();
    void addObject(ConfigurableObject *obj);
    int  unregister(ConfigurableObject *obj);

// Interface functions
    virtual void reset(void);
    virtual void read(bool ignore);
    virtual void write(void);
    virtual void at_open_config(void) { }

    virtual void at_close_config(void)
    {
        if (need_effectuate()) {
            effectuate();
            set_effectuated();
        }
    }

    virtual void effectuate(void);

/*
    int  get_page(void) { return flash_page; }
    int  get_page_size(void) { return block_size; }
*/

    void set_change_hook(uint8_t id, t_change_hook hook);
    void disable(uint8_t id);
    void enable(uint8_t id);
    ConfigurableObject *get_first_object(void) { return objects[0]; }

    ConfigItem *find_item(uint8_t id);
    ConfigItem *find_item(const char *str);
    int  get_value(uint8_t id);
    const char *get_store_name() { return store_name.c_str(); }
    const char *get_string(uint8_t id);
    void set_value(uint8_t id, int value);
    void set_string(uint8_t id, const char *s);
    void dump(void);
    void check_bounds(void);
    bool is_flash_stale(void) { return staleFlash; }
    bool need_effectuate(void) { return staleEffect; }
    void set_effectuated(void) { staleEffect = false; }
    const void set_need_flash_write(bool b) { staleFlash = b; }
    const void set_need_effectuate(void) { staleEffect = true; }
    ConfigPage *get_page(void) { return page; }

    IndexedList <ConfigItem *> *getItems() { return &items; }

    friend class ConfigIO;
    friend class ConfigItem;
    friend class ConfigPage;
};
    
class ConfigPage
{
    uint32_t id;
    int  block_size;
    int  flash_page;
    uint8_t *mem_block;
    IndexedList<ConfigStore*> stores;
    Flash *flash;

    int pack(void);
    void unpack(void);

public:
    ConfigPage(Flash *fl, int id, int page, int page_size) : flash(fl), id(id), flash_page(page), block_size(page_size), stores(4, NULL) {
        mem_block = new uint8_t[page_size];
    }

    virtual ~ConfigPage() {
        delete[] mem_block;
    }

    void add_store(ConfigStore *s) {
        stores.append(s);
    }
    uint32_t get_id() {
        return id;
    }

    void read(bool ignore);
    void read_from_file(File *f);
    void write();
    void unpack(ConfigStore *s);
};

class ConfigManager
{
	IndexedList<ConfigStore*> stores;
	IndexedList<ConfigPage*> pages;
    Flash *flash;
    int num_pages;
    bool safeMode;

    ConfigManager();
    ~ConfigManager();
public:
	static ConfigManager* getConfigManager() {
		static ConfigManager config_manager;
		return &config_manager;
	}
    
    ConfigStore *register_store(uint32_t page_id, const char *name, t_cfg_definition *defs, ConfigurableObject *ob);
    ConfigStore *find_store(const char *storename);
    void add_custom_store(ConfigStore *cfg);
    void remove_store(ConfigStore *cfg);

	Flash *get_flash_access(void) { return flash; }
	IndexedList<ConfigStore*> *getStores() { return &stores; }

	friend class ConfigIO;
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

extern const char *en_dis[];

#endif
