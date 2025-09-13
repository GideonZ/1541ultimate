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
 *   This module implements functions to store/retrieve configuration items
 */
extern "C" {
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "config.h"
#include <string.h>
#include "blockdev_flash.h"
#include "filemanager.h"

#if U64
#include "u64.h"
#endif

#define CFG_FILEPATH "/flash/config"

/*** CONFIGURATION MANAGER ***/
ConfigManager :: ConfigManager() : stores(16, NULL), pages(16, NULL)
{
	flash = get_flash();
	if(!flash) {
		printf("Error opening Flash device...\n");
		num_pages = 0;
	} else {
    	num_pages = flash->get_number_of_config_pages();
    	printf("ConfigManager opened flash: %p\n", flash);
    }
	safeMode = false;

#if U64
    if (U64_RESTORE_REG == 1) {
        safeMode = true;
    }
#elif SAFEMODE
    safeMode = true;
#else
    // If the reset button is pressed during boot, and it's not U64, then
    // safe mode is enabled.
    if ((ioRead8(ITU_BUTTON_REG) & ITU_BUTTON0) != 0) {
        safeMode = true;
    }
#endif
    if (safeMode) {
        printf("SAFE MODE ENABLED. Loading defaults...\n");
    }

#ifndef RECOVERYAPP
    // Now, also open the Flash File system!
    init_flash_disk();
#endif
}

ConfigManager :: ~ConfigManager()
{
    printf("Destructor ConfigManger...\n");
    ConfigStore *s;
    for(int n = 0; n < stores.get_elements();n++) {
        s = (ConfigStore *)stores[n];
		delete s;
    }
    stores.clear_list();

    ConfigPage *p;
    for(int n = 0; n < pages.get_elements();n++) {
        p = (ConfigPage *)pages[n];
        delete p;
    }

}

ConfigStore *ConfigManager :: register_store(uint32_t page_id, const char *name,
                                t_cfg_definition *defs, ConfigurableObject *ob) 
{
	if(!flash) {
        flash = get_flash();
        printf("register_store %s: Can't open flash\n", name);
		return NULL; // fail
	}
    if(page_id == 0) {
        printf("ERROR: Requesting to register a store with ID=0\n");
        return NULL;
    }

    // Check if the store was already opened, just add the object to it
    for (int i=0; i<stores.get_elements(); i++) {
        ConfigStore *cfgStore = stores[i];
        if (strcmp(cfgStore->get_store_name(), name) == 0) {
            if (ob) {
                cfgStore->addObject(ob);
            }
            return cfgStore;
        }
    }

    int page_size = flash->get_config_page_size();

    // Try to obtain page object that already represents the page in Flash
    ConfigPage *page = NULL;
    for(int n=0; n<pages.get_elements(); n++) {
        ConfigPage *p = pages[n];
        if (p->get_id() == page_id) {
            page = p;
            break;
        }
    }

    bool write = false;
    // Try to find the page in Flash
    if (!page) {
        uint32_t id;
        for(int i=0;i<num_pages;i++) {
            flash->read_config_page(i, 4, &id);
            if (page_id == id) {
                page = new ConfigPage(flash, id, i, page_size);
                pages.append(page);
                break;
            }
        }
    }

    // Not found in config pages, maybe it's on the flash storage itself?
    if (!page) {
        char fn[20];
        sprintf(fn, "page_%08x.bin", page_id);
        File *df;
        FileManager *fm = FileManager :: getFileManager(); 
        FRESULT fres = fm->fopen(CFG_FILEPATH, fn, FA_READ, &df);
        if (fres == FR_OK) {
            page = new ConfigPage(NULL, page_id, -1, page_size);
            pages.append(page);
            page->read_from_file(df);
            fm->fclose(df);
        }
    }

    // if still no luck, try to create a page in flash
    if (!page) {
        uint32_t id;
        for(int i=0;i<num_pages;i++) {
            flash->read_config_page(i, 4, &id);
            if (id == 0xFFFFFFFF) {
                //printf("Found empty spot on config page %d, for ID=%8x. Size=%d. Defs=%p. Name=%s\n", i, store_id, page_size, defs, name);
                page = new ConfigPage(flash, page_id, i, page_size);
                pages.append(page);
                write = true;
                break;
            }
        }
    }

    // No space in Flash! Issue warning and create page on disk
    if (!page) {
        page = new ConfigPage(NULL, page_id, -1, page_size);
        pages.append(page);
        write = true; // write to disk
    }
    ConfigStore *s = new ConfigStore(page, name, defs, ob);
    stores.append(s);
    page->add_store(s);

    if (write) {
        page->write();
    }
    s->read(safeMode);
    return s;
}

void ConfigManager :: add_custom_store(ConfigStore *cfg)
{
	stores.append(cfg);
}

void ConfigManager :: remove_store(ConfigStore *cfg)
{
    stores.remove(cfg);
}

ConfigStore *ConfigManager :: find_store(const char *storename)
{
    for(int i=0; i < stores.get_elements(); i++) {
        ConfigStore *st = stores[i];
        if (strcasecmp(st->get_store_name(), storename) == 0) {
            return st;
        }
    }
    return NULL;
}

ConfigStore *ConfigManager :: find_store(uint32_t page_id)
{
    for(int i=0; i < stores.get_elements(); i++) {
        ConfigStore *st = stores[i];
        if (st->get_page()->get_id() == page_id) {
            return st;
        }
    }
    return NULL;
}

//   ===================
/*** CONFIGURATION STORE ***/
//   ===================
ConfigStore :: ConfigStore(ConfigPage *page, const char *name,
                           t_cfg_definition *defs, ConfigurableObject *ob) : store_name(name), items(16, NULL), objects(4, NULL)
{
    if (ob) {
        objects.append(ob);
    }
    this->page = page;
    staleEffect = true;
    staleFlash = false;
    
    for(int i=0;i<64;i++) {
        if(defs[i].type == CFG_TYPE_END)
            break;
        if(defs[i].id == 0xFF)
            break;
        ConfigItem *item = new ConfigItem(this, &defs[i]);
        items.append(item);
    }
}

ConfigStore :: ~ConfigStore()
{
    ConfigItem *i;
    for(int n = 0; n < items.get_elements();n++) {
    	i = items[n];
		delete i;
    }
    items.clear_list();
}
        
void ConfigStore :: addObject(ConfigurableObject *obj)
{
    objects.append(obj);
}

int ConfigStore :: unregister(ConfigurableObject *obj)
{
    objects.remove(obj);
    return objects.get_elements();
}

void ConfigStore :: at_open_config(void)
{
    for(int i=0; i<objects.get_elements(); i++) {
        objects[i]->on_edit();
    }
}

int ConfigPage :: pack(void)
{
    memset(mem_block, 0xFF, block_size);

    uint8_t *b = &mem_block[4];
    (*(uint32_t *)mem_block) = id;
    int remain = block_size - 4;
    int size;
    for(int n = 0; n < stores.get_elements(); n++) {
        ConfigStore *s = stores[n];
        size = s->pack(b, remain);
        remain -= size;
        b += size;
    }
    return block_size - remain;
}

void ConfigPage :: unpack(void)
{
    for(int n = 0; n < stores.get_elements(); n++) {
        ConfigStore *s = stores[n];
        s->unpack(mem_block, block_size);
    }
}

void ConfigPage :: unpack(ConfigStore *s)
{
    s->unpack(mem_block, block_size);
}

int ConfigStore :: pack(uint8_t *b, int remain)
{
    ConfigItem *i;
    int len;
    int total = 0;
    //    printf("Packing ConfigStore %s.\n", store_name);
    for(int n = 0; n < items.get_elements();n++) {
    	i = items[n];
    	len = i->pack(b, remain);
        b += len;
        remain -= len;
        total += len;
        if(remain < 1) {
            printf("Configuration item doesn't fit. Dropped.\n");
            break;
        }
    }
    *b = 0xFF;

    return total;
}

void ConfigStore :: effectuate()
{
    if (objects.get_elements() == 0) {
        staleEffect = false;
        return;
    }
    if (!staleEffect) {
        printf("Effectuate for %s not needed; not stale.\n", get_store_name());
        return;
    }
    // printf("Calling Effectuate on %s for %d objects.\n", get_store_name(), objects.get_elements());
    for(int i=0; i<objects.get_elements(); i++) {
        ConfigurableObject *obj = objects[i];
        if(obj) {
            obj->effectuate_settings();
        }
    }
    staleEffect = false;
}
    
void ConfigStore :: write()
{
	printf("Writing config store '%s' to flash..", store_name.c_str());
	if(page) {
	    page->write();
        staleFlash = false;
	}
}

void ConfigPage :: write()
{
	int size = pack();

    if (flash_page < 0) {
        char fn[20];
        File *df;
        sprintf(fn, "page_%08x.bin", id);
        FileManager *fm = FileManager :: getFileManager();
        FRESULT fres = fm->create_dir(CFG_FILEPATH);
        fres = fm->fopen(CFG_FILEPATH, fn, FA_CREATE_ALWAYS | FA_WRITE, &df );
        if (fres == FR_OK) {
        	int size = pack();
            uint32_t tr;
            df->write(mem_block, (uint32_t)size, &tr);
            fm->fclose(df);
            printf("Written %s to flash disk\n", fn);
        } else {
            printf("Could not open '%s' for writing\n", fn);
        }
        return;
    }
    printf("Page: %d", flash_page);
	//dump_hex_relative(mem_block, size);
	if(flash) {
	    flash->write_config_page(flash_page, mem_block);
	    printf(" done.\n");
	} else {
		printf(" error.\n");
	}
}

void ConfigStore :: unpack(uint8_t *mem_block, int block_size)
{
    int index = 4;
    uint8_t id;
    int len;
    ConfigItem *i;
    
    while(index < block_size) {
        id  = mem_block[index];
        if(id == 0xFF) {
            break;
        }
        len = (int)mem_block[index+2];
        if(len > (block_size-index-3)) {
            printf("Illegal length. Doesn't fit.\n");
            break;
        }            
        // find ID in our store        
        for(int n = 0;n < items.get_elements(); n++) {
        	i = items[n];
        	if(id == i->definition->id) {
                i->unpack(&mem_block[index+1], len);
            }
        }
        index += len + 3;
    }
}

void ConfigPage :: read_from_file(File *f)
{
    uint32_t tr;
    f->read(mem_block, (uint32_t)block_size, &tr);
    printf("%d bytes read from config file\n", tr);
}

void ConfigPage :: read(bool ignoreData)
{
    if (flash_page < 0) {
        char fn[20];
        sprintf(fn, "page_%08x.bin", id);
        File *df;
        FileManager *fm = FileManager :: getFileManager(); 
        FRESULT fres = fm->fopen(CFG_FILEPATH, fn, FA_READ, &df);
        if (fres == FR_OK) {
            read_from_file(df);
            fm->fclose(df);
        }
        return;
    }

    if(flash && !ignoreData) {
        flash->read_config_page(flash_page, block_size, mem_block);
    } else {
        memset(mem_block, 0xFF, block_size);
    }
}

void ConfigStore :: read(bool ignore)
{
	if(page) {
	    page->read(ignore);
	    page->unpack(this);
	}
	staleEffect = true;
	staleFlash = false;
	check_bounds();
}

ConfigItem *ConfigStore :: find_item(uint8_t id)
{
    ConfigItem *i;
    for(int n = 0;n < items.get_elements(); n++) {
    	i = items[n];
        if(i->definition->id == id) {
            return i;
        }
    }
    return NULL;
}

ConfigItem *ConfigStore :: find_item(const char *str)
{
    ConfigItem *i;
    for(int n = 0;n < items.get_elements(); n++) {
    	i = items[n];
        if(strcasecmp(i->definition->item_text, str) == 0) {
            return i;
        }
    }
    return NULL;
}

void ConfigStore :: set_change_hook(uint8_t id, t_change_hook hook)
{
    ConfigItem *i = find_item(id);
    if(i) {
    	i->setChangeHook(hook);
    }
}

void ConfigStore :: disable(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if (i) {
        i->setEnabled(false);
    }
}

void ConfigStore :: enable(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if (i) {
        i->setEnabled(true);
    }
}

int ConfigStore :: get_value(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if(i)
        return i->value;
    return -1;
}
    
const char *ConfigStore :: get_string(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if(i) {
        if(i->string)
            return i->string;
        else
            return "__not a string__";
    }
    return "__id not found__";
}

void ConfigStore :: set_value(uint8_t id, int value)
{
    ConfigItem *i = find_item(id);
    if(i) {
        i->setValue(value);
    }
}

void ConfigStore :: set_string(uint8_t id, const char *s)
{
    ConfigItem *i = find_item(id);
    if(i) {
        i->setString(s);
    }
}

void ConfigStore :: dump(void)
{
    ConfigItem *i;
    for(int n = 0; n < items.get_elements(); n++) {
    	i = items[n];
        printf("ID %02x: ", i->definition->id);
        if ((i->definition->type == CFG_TYPE_STRING) || (i->definition->type ==CFG_TYPE_STRFUNC)) {
            printf("%s = '%s'\n", i->definition->item_text, i->string);
        } else if(i->definition->type == CFG_TYPE_STRPASS) {
            printf("%s = '%s'\n", i->definition->item_text, "*****");
        } else if(i->definition->type == CFG_TYPE_ENUM) {
            printf("%s = %s\n", i->definition->item_text, i->definition->items[i->value]);
        } else {
            printf("%s = %d\n", i->definition->item_text, i->value);
        }
    }
}

void ConfigStore :: reset(void)
{
    ConfigItem *i;
    for(int n = 0; n < items.get_elements(); n++) {
        i = items[n];
        i->reset();
    }
    staleEffect = true;
    staleFlash = true;
}

void ConfigStore :: check_bounds(void)
{
    ConfigItem *i;
    for(int n = 0; n < items.get_elements(); n++) {
    	i = items[n];
		if((i->definition->type == CFG_TYPE_ENUM) || 
		   (i->definition->type == CFG_TYPE_VALUE)) {
			if(i->value < i->definition->min) {
				i->value = i->definition->min;
			    staleEffect = true;
			    staleFlash = true;
			} else if(i->value > i->definition->max) {
				i->value = i->definition->max;
			    staleEffect = true;
			    staleFlash = true;
			}
		}
	}
}


/*** CONFIGURATION ITEM ***/
ConfigItem :: ConfigItem(ConfigStore *s, t_cfg_definition *d)
{
	hook = NULL;
	definition = d;
    store = s;
    if ((d->type == CFG_TYPE_STRING)||(d->type == CFG_TYPE_INFO)||(d->type == CFG_TYPE_STRFUNC)||(d->type == CFG_TYPE_STRPASS)) {
        string = new char[d->max+1];
    } else {
        string = NULL;
    }
    enabled = true;
    reset();
}

ConfigItem :: ~ConfigItem()
{
    if(string)
        delete[] string;
}

void ConfigItem :: reset(void)
{
    if ((definition->type == CFG_TYPE_STRING) || (definition->type == CFG_TYPE_STRFUNC) || (definition->type == CFG_TYPE_STRPASS)) {
        strncpy(string, (const char *)definition->def, definition->max);
        value = 0;
    } else {
        value = (int)definition->def;
    }
}

void ConfigItem :: unpack(uint8_t *buffer, int len)
{
    // first byte we get is the type
    if(*buffer != definition->type) {
        printf("Error! Entry type mismatch!!\n");
        return;
    }
    buffer++;
    int field = (int)*(buffer++);
    switch(definition->type) {
        case CFG_TYPE_VALUE:
        case CFG_TYPE_ENUM:
            value = 0;
            while(field--) {
                value <<= 8;
                value |= (int)*(buffer++);
            }
            if(value < definition->min)
                value = definition->def;
            if(value > definition->max)
                value = definition->def;
            break;
        case CFG_TYPE_STRING:
        case CFG_TYPE_STRFUNC:
        case CFG_TYPE_STRPASS:
            if(field > definition->max)
                field = definition->max;
            strncpy(string, (char *)buffer, field);
            string[field] = '\0';
            break;
        default:
            printf("Error: unknown type, reading flash configuration.\n");
    }
}

int ConfigItem :: pack(uint8_t *buffer, int len)
{
    // first byte we get is the id, then type, then length
    if(len > 0) {
        *(buffer++) = definition->id;
        len--;
    }
    if(len > 0) {
        *(buffer++) = definition->type;
        len--;
    }
    switch(definition->type) {
        case CFG_TYPE_VALUE:
//            printf("Value %s = %d\n", definition->item_text, value);
            if(len < 5) {
                printf("Int doesn't fit anymore.\n");
                return 0;
            }
            *(buffer++) = (uint8_t)4;
            *(buffer++) = (uint8_t)(value >> 24);
            *(buffer++) = (uint8_t)(value >> 16);
            *(buffer++) = (uint8_t)(value >> 8);
            *(buffer++) = (uint8_t)(value >> 0);
            return 7;
        case CFG_TYPE_ENUM:
//            printf("Enum %s = %s\n", definition->item_text, definition->items[value]);
            if(len < 2) {
                printf("Enum doesn't fit anymore.\n");
                return 0;
            }
            *(buffer++) = (uint8_t)1;
            *(buffer++) = (uint8_t)(value >> 0);
            return 4;
        case CFG_TYPE_STRING:
        case CFG_TYPE_STRFUNC:
        case CFG_TYPE_STRPASS:
//            printf("String %s = %s\n", definition->item_text, string);
            if(2+strlen(string) > len) {
                printf("String doesn't fit.\n");
                return 0;
            }
            *(buffer++) = (uint8_t)strlen(string);
            strcpy((char *)buffer, string);
            return 3+strlen(string);
        case CFG_TYPE_FUNC:
        case CFG_TYPE_SEP:
        case CFG_TYPE_INFO:
            break; // do nothing, do not store
        default:
            printf("Error: unknown type packing flash configuration.\n");
    }
    return 0;
}

// note:the buffer needs to be at least 3 bytes bigger than what width indicates
// width is the actual width in characters on the screen, and two additional bytes are needed to set the color
const char *ConfigItem :: get_display_string(char *buffer, int width)
{
	static char buf[32];

    memset(buffer, ' ', width+2);
    buffer[width+2] = '\0';
    
    switch(definition->type) {
        case CFG_TYPE_VALUE:
            sprintf(buf, definition->item_format, value);
            break;
        case CFG_TYPE_ENUM:
            sprintf(buf, definition->item_format, definition->items[value]);
            break;
        case CFG_TYPE_STRING:
        case CFG_TYPE_STRFUNC:
        case CFG_TYPE_INFO:
            sprintf(buf, definition->item_format, string);
            break;
        case CFG_TYPE_STRPASS:
            if (*string)
                strcpy(buf, "******");
            else
                strcpy(buf, "<none>");
            break;
        case CFG_TYPE_FUNC:
        case CFG_TYPE_SEP:
            sprintf(buf, definition->item_format);
            break;
        default:
            sprintf(buf, "Unknown type.");
    }

    int len = strlen(buf);

    const char *src;
    char *dst;
    // left align copy
    src = definition->item_text;
    dst = buffer;
    while(*src) {
        *(dst++) = *(src++);
    }
    // right align copy
    dst = &buffer[width+1];
    for(int b = len-1; b >= 0; b--)
        *(dst--) = buf[b];
    *(dst--) = (enabled) ? 7 : 11;
    *(dst--) = '\033'; // escape code = set color
    return (const char *)buffer;
}

int   ConfigItem :: fetch_possible_settings(IndexedList<ConfigSetting *> &list)
{
    char buf[32];
    int i,ret = 0;
    
    switch(definition->type) {
        case CFG_TYPE_VALUE:
            for(i=definition->min;i<=definition->max;i++) {
                sprintf(buf, definition->item_format, i);
                list.append(new ConfigSetting(i, this, buf));
            }
            ret = definition->max - definition->min + 1;
            break;
        case CFG_TYPE_ENUM:
            for(i=definition->min;i<=definition->max;i++) {
                sprintf(buf, definition->item_format, definition->items[i]);
                list.append(new ConfigSetting(i, this, buf));
            }
            ret = definition->max - definition->min + 1;
            break;
        default:
            printf("Item not contextable\n");
    }
    return ret;   
}

void ConfigItem :: execute(int sel)
{
    printf("Setting option to %d.\n", sel);
    value = sel;
    setChanged();
}

void ConfigItem :: setString(const char *s)
{
    if(this->string) {
        strncpy(this->string, s, this->definition->max);
        this->string[this->definition->max] = 0;  // Safe since the "string" buffer is max+1 (see ConfigItem constructor)
        if (strlen(s) > this->definition->max) {
            this->string[this->definition->max - 1] = '*';  // Indicate string was truncated
        }
        if ((definition->type == CFG_TYPE_STRING) || (definition->type == CFG_TYPE_STRFUNC) || (definition->type == CFG_TYPE_STRPASS)) {
            setChanged();
        }
    }
}

int ConfigItem :: next(int a)
{
    int value = getValue();
    int ret = 0;
    switch(definition->type) {
        case CFG_TYPE_ENUM:
        case CFG_TYPE_VALUE:
            value += a;
            // circular
            while (value > definition->max) {
                value -= (1 + definition->max - definition->min);
            }
            ret = setValue(value);
            break;

        default:
            break;
    }
    return ret;
}

int ConfigItem :: previous(int a)
{
    int value = getValue();
    int ret = 0;
    switch(definition->type) {
        case CFG_TYPE_ENUM:
        case CFG_TYPE_VALUE:
            value -= a;
            // circular
            while (value < definition->min) {
                value += (1 + definition->max - definition->min);
            }
            ret = setValue(value);
            break;
        default:
            break;
    }
    return ret;
}

int ConfigItem :: setChanged()
{
    store->set_need_flash_write(true);
    int ret = 0;
    if(hook) {
        ret = hook(this);
    } else {
        store->set_need_effectuate();
    }
    return ret;
}

ConfigSetting :: ConfigSetting(int index, ConfigItem *p, char *name) : setting_name(name)
{
	parent = p;
	setting_index = index;
}

Flash *get_flash(void) __attribute__((weak));

Flash *get_flash(void)
{
	return new Flash(); // stubbed base
}

const char *en_dis[]    = { "Disabled", "Enabled" };
