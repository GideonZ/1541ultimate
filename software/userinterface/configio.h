/*
 * configio.h
 *
 *  Created on: Aug 22, 2019
 *      Author: gideon
 */

#ifndef CONFIGIO_H_
#define CONFIGIO_H_

#include "config.h"
#include "menu.h"
#include "filemanager.h"
#include "stream_textlog.h"

class ConfigIO : public ObjectWithMenu
{
    static void S_write_to_file(File *f);
    static void S_write_store_to_file(ConfigStore *s, File *f);
    static bool S_read_store_element(ConfigStore *st, const char *line, int linenr, StreamTextLog *log);
    static ConfigStore *S_find_store(ConfigManager *cm, char *storename);

public:
    ConfigIO();
    virtual ~ConfigIO();

    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
    static int S_save_to_file(SubsysCommand *cmd);
    static int S_save(SubsysCommand *cmd);
    static int S_restore(SubsysCommand *cmd);
    static int S_reset(SubsysCommand *cmd);
    static bool S_read_from_file(File *f, StreamTextLog *log);
};


#endif /* CONFIGIO_H_ */
