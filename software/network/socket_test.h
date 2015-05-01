/*
 * socket_test.h
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_TEST_H_
#define NETWORK_SOCKET_TEST_H_

#include "poll.h"
#include "menu.h"

class SocketTest: public ObjectWithMenu {
	void doTest1();
	void doTest2();
	void sendTrace(int size);
	static void restartThread(void *a);
public:
	SocketTest();
	virtual ~SocketTest();

    int  fetch_task_items(IndexedList<CachedTreeNode*> &item_list);
    void poll(Event &e);
};

#endif /* NETWORK_SOCKET_TEST_H_ */
