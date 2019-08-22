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

class ConfigIO : public ObjectWithMenu
{
    static void S_write_to_file(File *f);
    static void S_write_store_to_file(ConfigStore *s, File *f);

public:
    ConfigIO();
    virtual ~ConfigIO();

    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
    static int S_save_to_file(SubsysCommand *cmd);
};


#endif /* CONFIGIO_H_ */
