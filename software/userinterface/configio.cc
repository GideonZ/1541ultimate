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
#include "c64.h"

ConfigIO::ConfigIO()
{

}

ConfigIO::~ConfigIO()
{
    // TODO Auto-generated destructor stub
}

void ConfigIO :: create_task_items(void)
{
    myActions.savecfg   = new Action("Save to Flash", ConfigIO :: S_save, 0, 0);
    myActions.savefile  = new Action("Save to File", ConfigIO :: S_save_to_file, 0, 0);
    myActions.loadcfg   = new Action("Reset from Flash", ConfigIO :: S_restore, 0, 0);
    myActions.factory   = new Action("Reset to Defaults", ConfigIO :: S_reset, 0, 0);
    myActions.clr_flash = new Action("Clear Flash Config", ConfigIO :: S_clear, 0, 0);
    myActions.clear_dbg = new Action("Clear Debug Log", ConfigIO :: S_reset_log, 0, 0);
    myActions.save_dbg  = new Action("Save Debug Log", ConfigIO :: S_save_log, 0, 0);

    TaskCategory *cfg = TasksCollection :: getCategory("Configuration", SORT_ORDER_CONFIG);
    cfg->append(myActions.savecfg);
    cfg->append(myActions.savefile);
    cfg->append(myActions.loadcfg);
    cfg->append(myActions.factory);
    cfg->append(myActions.clr_flash);

    TaskCategory *dev = TasksCollection :: getCategory("Developer", SORT_ORDER_DEVELOPER);
    dev->append(myActions.clear_dbg);
    dev->append(myActions.save_dbg);
}

void ConfigIO :: update_task_items(bool writablePath)
{
    if (writablePath) {
        myActions.savefile ->enable();
        myActions.save_dbg ->enable();
    } else {
        myActions.savefile ->disable();
        myActions.save_dbg ->disable();
    }
}

SubsysResultCode_e ConfigIO :: S_reset_log(SubsysCommand *cmd)
{
    extern StreamTextLog textLog; // the global log
    textLog.Reset();
    return SSRET_OK;
}

SubsysResultCode_e ConfigIO :: S_save_log(SubsysCommand *cmd)
{
    extern StreamTextLog textLog; // the global log
    int len = textLog.getLength();

    FileManager *fm = FileManager::getFileManager();
    File *f;

    char buffer[64];
    buffer[0] = 0;
    int res = cmd->user_interface->string_box("Give filename..", buffer, 22);
    if ((res > 0) && (*buffer)) {
        set_extension(buffer, ".log", 32);
        FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_ALWAYS, &f);
        if (fres == FR_OK) {
            uint32_t transferred = 0;
            fres = f->write(textLog.getText(), len, &transferred);
            fm->fclose(f);
            return (fres == FR_OK) ? SSRET_OK : SSRET_DISK_ERROR;
        } else {
            sprintf(buffer, "Error: %s", FileSystem::get_error_string(fres));
            cmd->user_interface->popup(buffer, BUTTON_OK);
            return SSRET_CANNOT_OPEN_FILE;
        }
    } else {
        return SSRET_ABORTED_BY_USER;
    }
}

SubsysResultCode_e ConfigIO :: S_save_to_file(SubsysCommand *cmd)
{
    char buffer[64];
    buffer[0] = 0;

    FileManager *fm = FileManager::getFileManager();
    File *f;

    int res = cmd->user_interface->string_box("Give filename..", buffer, 22);
    if ((res > 0) && (*buffer)) {
        set_extension(buffer, ".cfg", 32);
        FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_ALWAYS, &f);
        if (fres == FR_OK) {
            S_write_to_file(f); // FIXME? Cannot fail?
            fm->fclose(f);
        } else {
            sprintf(buffer, "Error: %s", FileSystem::get_error_string(fres));
            cmd->user_interface->popup(buffer, BUTTON_OK);
            return SSRET_CANNOT_OPEN_FILE;
        }
    } else {
        return SSRET_ABORTED_BY_USER;
    }
    return SSRET_OK;
}

void ConfigIO :: S_write_to_file(File *f)
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        if (s->save_to_cfg_file()) {
            S_write_store_to_file(s, f);
        }
    }
}

SubsysResultCode_e ConfigIO :: S_save(SubsysCommand *cmd)
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        if (s->is_flash_stale()) {
            s->write();
        }
    }
    cmd->user_interface->popup("Configuration saved.", BUTTON_OK);
    return SSRET_OK;
}

SubsysResultCode_e ConfigIO :: S_reset(SubsysCommand *cmd)
{
    int res = cmd->user_interface->popup("Are you sure to clear settings?", BUTTON_YES | BUTTON_NO);
    if (res != BUTTON_YES) {
        return SSRET_ABORTED_BY_USER;
    }
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        s->reset();
        s->write();
        s->effectuate();
    }
    return SSRET_OK;
}

SubsysResultCode_e ConfigIO :: S_clear(SubsysCommand *cmd)
{
    Flash *fl = get_flash();
    if (!fl) {
        return SSRET_INTERNAL_ERROR;
    }
    int res = cmd->user_interface->popup("Sure to clear config flash?", BUTTON_YES | BUTTON_NO);
    if (res != BUTTON_YES) {
        return SSRET_ABORTED_BY_USER;
    }
    int num = fl->get_number_of_config_pages();
    for (int i=0; i < num; i++) {
        fl->clear_config_page(i);
    }
    res = cmd->user_interface->popup("Cold Boot Required!", BUTTON_OK | BUTTON_CANCEL);
    if (res == BUTTON_OK) {
        SubsysCommand *off = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, MENU_C64_POWEROFF, 0, NULL, 0);
        off->execute();
    }
    return SSRET_OK;
}

SubsysResultCode_e ConfigIO :: S_restore(SubsysCommand *cmd)
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    for(int n = 0; n < cm->stores.get_elements();n++) {
        s = cm->stores[n];
        s->read(false);
        if (!cm->safeMode) {
            s->effectuate();
        }
    }
    return SSRET_OK;
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
        if ((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRFUNC) || (i->definition->type == CFG_TYPE_STRPASS)) {
            len = sprintf(buffer, "%s=%s\n", i->definition->item_text, i->string);
        } else if(i->definition->type == CFG_TYPE_ENUM) {
            len = sprintf(buffer, "%s=%s\n", i->definition->item_text, i->definition->items[i->getValue()]);
        } else if(i->definition->type == CFG_TYPE_VALUE) {
            len = sprintf(buffer, "%s=%d\n", i->definition->item_text, i->getValue());
        }
        if (len) {
            f->write(buffer, len, &tr);
        }
    }
    len = sprintf(buffer, "\n");
    f->write(buffer, len, &tr);
}

bool ConfigIO :: S_read_from_file(File *f, StreamTextLog *log, IndexedList<ConfigStore *> &loaded_stores)
{
    char c;
    char line[128];
    uint32_t tr;
    bool allOK = true;
    ConfigStore *store = NULL;
    bool in_unknown_store = false;
    ConfigManager *cm = ConfigManager :: getConfigManager();
    int linenr = 0;

    while(1) {
        for(int i=0;i<128;) {
            FRESULT fres = f->read(&c, 1, &tr);
            if ((fres == FR_OK) && tr) {
                if (c == 0x0D) {
                    continue;
                }
                if (c == 0x0A) {
                    line[i] = 0;
                    linenr++;
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
            store = NULL;
            in_unknown_store = false;
            for(int i=1;i<128;i++) {
                if (line[i] == ']') {
                    line[i] = 0;
                    store = cm->find_store(line + 1);
                    if (!store) {
                        // A .cfg saved on a machine with hardware this one does
                        // not have names stores that are missing here. Warn and
                        // skip the section rather than failing the whole file.
                        in_unknown_store = true;
                        printf("Config: no store named '%s' on this machine; its settings are ignored.\n", line + 1);
                        log->format("Line %d: Store name '%s' not found; section ignored.\n", linenr, line + 1);
                    }
                    break;
                }
            }
            continue;
        }
        // trim line? well for now let's assume correct spacing
        if (strlen(line) > 0) {
            if (store) {
                t_cfg_line_result result = S_read_store_element(store, line, linenr, log);
                if (result == CFG_LINE_MALFORMED) {
                    allOK = false;
                }
                // Only a store that actually took a value is worth effectuating.
                if (result == CFG_LINE_APPLIED) {
                    bool found = false;
                    for (int n = 0; n < loaded_stores.get_elements(); n++) {
                        if (loaded_stores[n] == store) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        loaded_stores.append(store);
                    }
                }
            } else if (in_unknown_store) {
                // Already warned about the section; name the item too, so the
                // log says exactly what was dropped.
                printf("Config: '%s' ignored, its store is not on this machine.\n", line);
            } else {
                log->format("Line %d: Not inside valid store.\n", linenr);
                allOK = false;
            }
        }
    }
    return allOK;
}

// A .cfg written by this firmware never puts spaces around '=', so a file that
// has been round-tripped is unaffected by any of this. A hand-edited one is
// another matter, and enum labels make it worse: several are padded so the menu
// can right align them ("  30", " ~45"), which means writing the obvious thing
// by hand produces a value that used to be rejected outright.
static const char *skip_leading_space(const char *s)
{
    while ((*s == ' ') || (*s == '\t')) {
        s++;
    }
    return s;
}

static int length_without_trailing_space(const char *s)
{
    int n = (int)strlen(s);
    while ((n > 0) && ((s[n - 1] == ' ') || (s[n - 1] == '\t'))) {
        n--;
    }
    return n;
}

// Compares what is between the spaces, so " ~45" and "~45" are the same choice
// while "12 kHz" keeps the space that belongs to it.
static bool equal_ignoring_outer_space(const char *a, const char *b)
{
    a = skip_leading_space(a);
    b = skip_leading_space(b);
    int la = length_without_trailing_space(a);
    int lb = length_without_trailing_space(b);
    return (la == lb) && (strncasecmp(a, b, la) == 0);
}

t_cfg_line_result ConfigIO :: S_read_store_element(ConfigStore *st, const char *line, int linenr, StreamTextLog *log)
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
    // No config item is defined with padding around its name, so trimming the
    // name can only help: it makes "Item = value" find the same item as
    // "Item=value".
    itemname[length_without_trailing_space(itemname)] = 0;
    const char *name = skip_leading_space(itemname);
    if (strlen(name) == 0) {
        log->format("Line %d: No item name.\n", linenr);
        return CFG_LINE_MALFORMED;
    }
    if (!valuestr) {
        log->format("Line %d: No value given for item '%s'.\n", linenr, name);
        return CFG_LINE_MALFORMED;
    }
    // now look for the store element with the itemname
    ConfigItem *item = st->find_item(name);
    if (!item) {
        // Not an error: a .cfg from a machine with other hardware, or from
        // another firmware version, legitimately names items this one lacks.
        // printf goes to the syslog when one is configured.
        printf("Config: [%s] has no item '%s' (value '%s'); ignored.\n",
               st->store_name.c_str(), name, valuestr);
        log->format("Line %d: Item '%s' not found in this store [%s]; ignored.\n", linenr, name, st->store_name.c_str());
        return CFG_LINE_UNKNOWN;
    }

    // now we know what item should be configured, and how to 'read' the string
    bool found = false;
    int value;
    if (item->definition->type == CFG_TYPE_VALUE) {
        sscanf(valuestr, "%d", &value);
        if (value != item->value) {
            item->value = value;
            st->staleEffect = true;
            st->staleFlash = true;
        }
    } else if ((item->definition->type == CFG_TYPE_STRING) || (item->definition->type == CFG_TYPE_STRFUNC) || (item->definition->type == CFG_TYPE_STRPASS)) {
        // Taken exactly as written, unlike an enum choice: a space in a string
        // value can be deliberate, and a password is a string. Trimming here
        // would silently change data rather than accept a different spelling of
        // the same choice.
        if (strncmp(item->string, valuestr, item->definition->max) != 0) {
            strncpy(item->string, valuestr, item->definition->max);
            st->staleEffect = true;
            st->staleFlash = true;
        }
    } else if (item->definition->type == CFG_TYPE_ENUM) {
        // this is the most nasty one. Let's just iterate over the possibilities and compare the resulting strings
        for(int n = item->definition->min; n <= item->definition->max; n++) {
            if (equal_ignoring_outer_space(valuestr, item->definition->items[n])) {
                if (n != item->value) {
                    item->value = n;
                    st->staleEffect = true;
                    st->staleFlash = true;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            log->format("Line %d: Value '%s' is not a valid choice for item %s\n", linenr, valuestr, item->definition->item_text);
            return CFG_LINE_MALFORMED;
        }
    }
    return CFG_LINE_APPLIED;
}

ConfigIO config_io;
