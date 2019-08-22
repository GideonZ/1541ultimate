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
    item_list.append(new Action("Save Config to File", ConfigIO :: S_save_to_file, 0, 0));
    return 1;
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

