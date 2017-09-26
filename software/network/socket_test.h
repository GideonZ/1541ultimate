/*
 * socket_test.h
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_TEST_H_
#define NETWORK_SOCKET_TEST_H_

#include "menu.h"
#include "filemanager.h"
#include "subsys.h"

class SocketTest : public ObjectWithMenu {
	static int doTest1(SubsysCommand *cmd);
	static int doTest2(SubsysCommand *cmd);
	static int profiler(SubsysCommand *cmd);
	static int saveTrace(SubsysCommand *cmd);
	static int sendTrace(int size);
	static void restartThread(void *a);
public:
	SocketTest();
	virtual ~SocketTest();

    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
};

#endif /* NETWORK_SOCKET_TEST_H_ */
