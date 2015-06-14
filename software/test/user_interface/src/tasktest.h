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

class TaskTest : public ObjectWithMenu {
public:
	TaskTest() {
	}
	virtual ~TaskTest() {
	}

    virtual int fetch_task_items(IndexedList<Action*> &item_list);

    static void runTestTask(void *obj, void *param);
};

#endif /* TEST_USER_INTERFACE_SRC_TASKTEST_H_ */
