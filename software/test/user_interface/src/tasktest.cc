/*
 * tasktest.cc
 *
 *  Created on: May 25, 2015
 *      Author: Gideon
 */

#include "tasktest.h"
#include <stdio.h>

int TaskTest :: fetch_task_items(IndexedList<Action*> &item_list)
{
	char buf[16];
	for(int i=0;i<10;i++) {
		sprintf(buf, "Test action %d", i);
		item_list.append(new Action(buf, TaskTest :: runTestTask, this, (void *)i));
	}
	return item_list.get_elements();
}

void TaskTest :: runTestTask(void *obj, void *param)
{
	printf("You chose TaskTest %d\n", (int)param);
}

TaskTest test;
