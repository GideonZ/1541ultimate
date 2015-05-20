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

ConfigManager config_manager; // global!

/*** CONFIGURATION MANAGER ***/
ConfigManager :: ConfigManager() : stores(16, NULL)
{
	flash = get_flash();
	if(!flash) {
		printf("Error opening Flash device...\n");
		num_pages = 0;
	} else {
    	num_pages = flash->get_number_of_config_pages();
    }
//    root.add_child(this); // make ourselves visible in the browser
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
}

ConfigStore *ConfigManager :: register_store(uint32_t store_id, char *name,
                                t_cfg_definition *defs, ConfigurableObject *ob) 
{
	if(!flash)
		return NULL; // fail
		
    if(store_id == 0) {
        printf("ERROR: Requesting to register a store with ID=0\n");
        return NULL;
    }

    int page_size = flash->get_page_size();
    uint32_t id;
    ConfigStore *s;

    for(int i=0;i<num_pages;i++) {
        flash->read_config_page(i, 4, &id);
        if (store_id == id) {
            s = new ConfigStore(store_id, name, i, page_size, defs, ob); // TODO
            s->read();
            //printf("APPENDING STORE %p %s\n", s, s->get_name());
            stores.append(s);
            return s;
        }
    }
    //printf("Store %8x not found..\n", store_id);
    for(int i=0;i<num_pages;i++) {
        flash->read_config_page(i, 4, &id);
        if (id == 0xFFFFFFFF) {
            //printf("Found empty spot on config page %d, for ID=%8x. Size=%d. Defs=%p. Name=%s\n", i, store_id, page_size, defs, name);
            s = new ConfigStore(store_id, name, i, page_size, defs, ob); // TODO
            s->write();
            stores.append(s);
            return s;
        }
    }
    // no entry in flash found, no place to create one.
    // create a temporary one in memory.
    s = new ConfigStore(store_id, name, -1, page_size, defs, ob); // TODO
    if(s) {
        stores.append(s);
    }
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

ConfigStore *ConfigManager :: open_store(uint32_t id)
{
    ConfigStore *s;
    for(int n = 0; n < stores.get_elements();n++) {
        s = stores[n];
        if(s->id == id)
            return s;
    }
    return NULL;
}

//   ===================
/*** CONFIGURATION STORE ***/
//   ===================
ConfigStore :: ConfigStore(uint32_t store_id, char *name, int page, int page_size,
                           t_cfg_definition *defs, ConfigurableObject *ob) : items(16, NULL)
{
    //printf("Create configstore %8x with size %d..", store_id, page_size);
    if(page_size)
        mem_block = new uint8_t[page_size];
    else
        mem_block = NULL;
        
    block_size = page_size;
    flash_page = page;
    id = store_id;
    obj = ob;
    dirty = false;
    store_name = name;
    
    for(int i=0;i<32;i++) {
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
    if(dirty) {
        write();
    }
        
    ConfigItem *i;
    for(int n = 0; n < items.get_elements();n++) {
    	i = items[n];
		delete i;
    }
    items.clear_list();
    if(mem_block)
    	delete mem_block;
}
        
void ConfigStore :: pack()
{
    int remain = block_size;
	memset(mem_block, 0xFF, block_size);
    uint8_t *b = &mem_block[4];
    ConfigItem *i;
    int len;
    (*(uint32_t *)mem_block) = id;

    //    printf("Packing ConfigStore %s.\n", store_name);
    for(int n = 0; n < items.get_elements();n++) {
    	i = items[n];
    	len = i->pack(b, remain);
        b += len;
        remain -= len;
        if(remain < 1) {
            printf("Configuration item doesn't fit. Dropped.\n");
            break;
        }
    }
    *b = 0xFF;
}

void ConfigStore :: effectuate()
{
    if(obj)
        obj->effectuate_settings();
}
    
void ConfigStore :: write()
{
	printf("Writing config store '%s' to flash, page %d..", store_name, flash_page);

	pack();
	Flash *flash = config_manager.get_flash_access();
	if(flash) {
	    flash->write_config_page(flash_page, mem_block);
	    dirty = false;
	    printf(" done.\n");
	} else {
		printf(" error.\n");
	}
}

void ConfigStore :: unpack()
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

void ConfigStore :: read()
{
	Flash *flash = config_manager.get_flash_access();
	if(flash) {
	    flash->read_config_page(flash_page, block_size, mem_block);
	    unpack();
	}
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

int ConfigStore :: get_value(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if(i)
        return i->value;
    return -1;
}
    
char *ConfigStore :: get_string(uint8_t id)
{
    ConfigItem *i = find_item(id);
    if(i)
        if(i->string)
            return i->string;
        else
            return "__not a string__";
            
    return "__id not found__";
}

void ConfigStore :: set_value(uint8_t id, int value)
{
    ConfigItem *i = find_item(id);
    if(i)
        i->value = value;
}

void ConfigStore :: set_string(uint8_t id, char *s)
{
    ConfigItem *i = find_item(id);
    if(i)
        if(i->string)
            strncpy(i->string, s, i->definition->max);

}

void ConfigStore :: dump(void)
{
    ConfigItem *i;
    for(int n = 0; n < items.get_elements(); n++) {
    	i = items[n];
        printf("ID %02x: ", i->definition->id);
        if(i->definition->type == CFG_TYPE_STRING) {
            printf("%s = '%s'\n", i->definition->item_text, i->string);
        } else if(i->definition->type == CFG_TYPE_ENUM) {
            printf("%s = %s\n", i->definition->item_text, i->definition->items[i->value]);
        } else {
            printf("%s = %d\n", i->definition->item_text, i->value);
        }
    }
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
				dirty = true;
			} else if(i->value > i->definition->max) {
				i->value = i->definition->max;
				dirty = true;
			}
		}
	}
}


/*** CONFIGURATION ITEM ***/
ConfigItem :: ConfigItem(ConfigStore *s, t_cfg_definition *d)
{
    definition = d;
    store = s;
    if(d->type == CFG_TYPE_STRING) {
        string = new char[d->max+1];
        strncpy(string, (char *)d->def, d->max);
        value = 0;
    } else {
        value = d->def;
        string = NULL;
    }
}

ConfigItem :: ~ConfigItem()
{
    if(string)
        delete[] string;
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
//            printf("String %s = %s\n", definition->item_text, string);
            if(2+strlen(string) > len) {
                printf("String doesn't fit.\n");
                return 0;
            }
            *(buffer++) = (uint8_t)strlen(string);
            strcpy((char *)buffer, string);
            return 3+strlen(string);
        default:
            printf("Error: unknown type packing flash configuration.\n");
    }
    return 0;
}

char *ConfigItem :: get_display_string()
{
    static char tmp[44];
    static char buf[32];

    memset(tmp, ' ', 40);
    tmp[40] = '\0';
    
    switch(definition->type) {
        case CFG_TYPE_VALUE:
            sprintf(buf, definition->item_format, value);
            break;
        case CFG_TYPE_ENUM:
            sprintf(buf, definition->item_format, definition->items[value]);
            break;
        case CFG_TYPE_STRING:
            sprintf(buf, definition->item_format, string);
            break;
        default:
            sprintf(buf, "Unknown type.");
    }

    int len = strlen(buf);

    char *src, *dst;
    // left align copy
    src = definition->item_text;
    dst = tmp;
    while(*src) {
        *(dst++) = *(src++);
    }
    // right align copy
    dst = &tmp[39];
    for(int b = len-1; b >= 0; b--)
        *(dst--) = buf[b];
    *(dst--) = '\037'; // color code
    *(dst--) = '\033'; // escape code
    return tmp;
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
    store->dirty = true;
}

ConfigSetting :: ConfigSetting(int index, ConfigItem *p, char *name) : setting_name(name)
{
	parent = p;
	setting_index = index;
}
