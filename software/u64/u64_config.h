/*
 * u64_config.h
 *
 *  Created on: Oct 6, 2017
 *      Author: Gideon
 */

#ifndef U64_CONFIG_H_
#define U64_CONFIG_H_

#include "config.h"
#include "filemanager.h"
#include "subsys.h"

class U64Config : public ConfigurableObject, ObjectWithMenu, SubSystem
{
	FileManager *fm;
public:
    U64Config();
    ~U64Config() {}

    void effectuate_settings();
    int fetch_task_items(Path *p, IndexedList<Action*> &item_list);
    int executeCommand(SubsysCommand *cmd);

    static void setPllOffset(ConfigItem *it);
    static void setScanMode(ConfigItem *it);

};

extern U64Config u64_configurator;

#endif /* U64_CONFIG_H_ */
