/*
 * tasktest.h
 *
 *  Created on: May 25, 2015
 *      Author: Gideon
 */

#ifndef TEST_USER_INTERFACE_SRC_TASKTEST_H_
#define TEST_USER_INTERFACE_SRC_TASKTEST_H_

#include "menu.h"
#include "globals.h"
#include "subsys.h"
#include "path.h"

class TaskTest : public SubSystem, ObjectWithMenu {
public:
	TaskTest() : SubSystem(15) {
	}
	virtual ~TaskTest() {
	}

    virtual int fetch_task_items(Path *path, IndexedList<Action*> &item_list);

    const char *identify(void) { return "TaskTest"; };
    int executeCommand(SubsysCommand *cmd);

    static void runTestTask(void *obj, void *param);
};

#endif /* TEST_USER_INTERFACE_SRC_TASKTEST_H_ */
