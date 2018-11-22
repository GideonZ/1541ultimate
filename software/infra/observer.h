/*
 * observer.h
 *
 *  Created on: Jul 11, 2015
 *      Author: Gideon
 */

#ifndef INFRA_OBSERVER_H_
#define INFRA_OBSERVER_H_

#include <stdio.h>
#include "FreeRTOS.h"
#include "queue.h"

class ObserverQueue {
	QueueHandle_t queue;
	const char *name;
	int polls;
public:
	ObserverQueue(const char *n) : name(n) {
		queue = xQueueCreate(8, sizeof(void *));
		polls = 0;
	}
	virtual ~ObserverQueue() {
		vQueueDelete(queue);
	}
	bool putEvent(void *el) {
#ifdef OS
		return xQueueSend(queue, &el, 5);
#endif
	}
	void *waitForEvent(uint32_t ticks) {
		polls++;
		void *result = 0;
		xQueueReceive(queue, &result, (TickType_t)ticks);
		return result;
	}

	const char *getName(void) {
	    return name;
	}
};

#endif /* INFRA_OBSERVER_H_ */
