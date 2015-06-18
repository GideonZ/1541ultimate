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
		item_list.append(new Action(buf, 15, i, 0));
	}
	return item_list.get_elements();
}

int TaskTest :: executeCommand(SubsysCommand *cmd)
{
	cmd->print();
	return 1;
}

TaskTest test;
