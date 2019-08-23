/*
 * configio.cc
 *
 *  Created on: Aug 22, 2019
 *      Author: gideon
 */

#include "configio.h"
#include "userinterface.h"
#include "subsys.h"
#include "userinterface.h"

ConfigIO::ConfigIO()
{

}

ConfigIO::~ConfigIO()
{
    // TODO Auto-generated destructor stub
}

int  ConfigIO :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
    item_list.append(new Action("Save Config", ConfigIO :: S_save, 0, 0));
    item_list.append(new Action("Reset to Factory Defaults", ConfigIO :: S_reset, 0, 0));
    item_list.append(new Action("Save Config to File", ConfigIO :: S_save_to_file, 0, 0));

    return 3;
}

int ConfigIO :: S_save_to_file(SubsysCommand *cmd)
{
    char buffer[64];
    buffer[0] = 0;

    FileManager *fm = FileManager::getFileManager();
    File *f;

    int res = cmd->user_interface->string_box("Give filename..", buffer, 22);
    set_extension(buffer, ".cfg", 32);
    if (res > 0) {
        FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_ALWAYS, &f);
        if (fres == FR_OK) {
            S_write_to_file(f);
            fm->fclose(f);
        } else {
            sprintf(buffer, "Error: %s", FileSystem::get_error_string(fres));
            cmd->user_interface->popup(buffer, BUTTON_OK);
        }
    }
    return 0;
}

void ConfigIO :: S_write_to_file(File *f)
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        S_write_store_to_file(s, f);
    }
}

int ConfigIO :: S_save(SubsysCommand *cmd)
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        if (s->dirty) {
            s->write();
            s->dirty = false;
        }
    }
    cmd->user_interface->popup("Configuration saved.", BUTTON_OK);
    return 0;
}

int ConfigIO :: S_reset(SubsysCommand *cmd)
{
    int res = cmd->user_interface->popup("Are you sure to clear settings?", BUTTON_YES | BUTTON_NO);
    if (res != BUTTON_YES) {
        return 0;
    }
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        s->reset();
        s->write();
        s->effectuate();
    }
    return 0;
}

void ConfigIO :: S_write_store_to_file(ConfigStore *st, File *f)
{
    ConfigItem *i;
    char buffer[64]; // should be enough, as it should fit on 40 col screen
    int len = 0;
    uint32_t tr;

    len = sprintf(buffer, "[%s]\n", st->store_name.c_str() );
    if (len) {
        f->write(buffer, len, &tr);
    }
    for(int n = 0; n < st->items.get_elements(); n++) {
        i = st->items[n];
        len = 0;
        if(i->definition->type == CFG_TYPE_STRING) {
            len = sprintf(buffer, "%s=%s\n", i->definition->item_text, i->string);
        } else if(i->definition->type == CFG_TYPE_ENUM) {
            len = sprintf(buffer, "%s=%s\n", i->definition->item_text, i->definition->items[i->value]);
        } else {
            len = sprintf(buffer, "%s=%d\n", i->definition->item_text, i->value);
        }
        if (len) {
            f->write(buffer, len, &tr);
        }
    }
    len = sprintf(buffer, "\n");
    f->write(buffer, len, &tr);
}

bool ConfigIO :: S_read_from_file(File *f)
{
    char c;
    char line[128];
    uint32_t tr;
    bool allOK = true;
    ConfigStore *store;
    ConfigManager *cm = ConfigManager :: getConfigManager();

    while(1) {
        for(int i=0;i<128;) {
            FRESULT fres = f->read(&c, 1, &tr);
            if ((fres == FR_OK) && tr) {
                if (c == 0x0D) {
                    continue;
                }
                if (c == 0x0A) {
                    line[i] = 0;
                    break;
                }
                line[i++] = c;
            } else {
                return allOK;
            }
        }
        if ((line[0] == '#') || (line[0] == ';')) {
            continue;
        }
        if (line[0] == '[') {
            for(int i=1;i<128;i++) {
                if (line[i] == ']') {
                    line[i] = 0;
                    store = S_find_store(cm, line + 1);
                    break;
                }
            }
            continue;
        }
        // trim line? well for now let's assume correct spacing
        if (strlen(line) > 0) {
            if (store) {
                allOK &= S_read_store_element(store, line);
            } else {
                printf("No store selected for line '%s'\n", line);
                allOK = false;
            }
        }
    }
    return allOK;
}

ConfigStore *ConfigIO :: S_find_store(ConfigManager *cm, char *storename)
{
    for(int i=0; i < cm->stores.get_elements(); i++) {
        ConfigStore *st = cm->stores[i];
        if (strcasecmp(st->store_name.c_str(), storename) == 0) {
            return st;
        }
    }
    printf("Store name '%s' not found!\n", storename);
    return NULL;
}

bool ConfigIO :: S_read_store_element(ConfigStore *st, const char *line)
{
    char itemname[40];
    const char *valuestr = 0;
    // first look for the first occurrence of '='
    int len = strlen(line);

    for(int i=0; (i < len) && (i < 40); i++) {
        if (line[i] == '=') {
            itemname[i] = 0;
            valuestr = line + i + 1;
            break;
        }
        itemname[i] = line[i];
    }
    if (!valuestr) {
        return false;
    }
    if (strlen(itemname) == 0) {
        return false;
    }
    // now look for the store element with the itemname
    ConfigItem *item = 0;
    for(int n = 0; n < st->items.get_elements(); n++) {
        item = st->items[n];
        if (strcasecmp(itemname, item->definition->item_text) == 0) {
            break;
        }
    }
    if (!item) {
        printf("Item '%s' not found in this store [%s].\n", itemname, st->store_name.c_str());
        return false;
    }

    // now we know what item should be configured, and how to 'read' the string
    bool found = false;
    if (item->definition->type == CFG_TYPE_VALUE) {
        sscanf(valuestr, "%d", &item->value);
        st->dirty = true;
    } else if (item->definition->type == CFG_TYPE_STRING) {
        strncpy(item->string, valuestr, item->definition->max);
        st->dirty = true;
    } else if (item->definition->type == CFG_TYPE_ENUM) {
        // this is the most nasty one. Let's just iterate over the possibilities and compare the resulting strings
        for(int n = item->definition->min; n <= item->definition->max; n++) {
            if (strcasecmp(valuestr, item->definition->items[n]) == 0) {
                item->value = n;
                st->dirty = true;
                found = true;
                break;
            }
        }
        if (!found) {
            printf("Value '%s' is not a valid choice for item %s\n", valuestr, item->definition->item_text);
            return false;
        }
    }
    return true;
}

ConfigIO config_io;
