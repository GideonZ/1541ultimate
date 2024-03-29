/*
 * observer.h
 *
 *  Created on: Jul 11, 2015
 *      Author: Gideon
 */

#ifndef INFRA_OBSERVER_H_
#define INFRA_OBSERVER_H_

#include <stdio.h>

#ifdef OS
#include "FreeRTOS.h"
#include "queue.h"
#endif

class ObserverQueue {
#ifdef OS
    QueueHandle_t queue;
#endif
	const char *name;
	int polls;
public:
	ObserverQueue(const char *n) : name(n) {
#ifdef OS
		queue = xQueueCreate(8, sizeof(void *));
#endif
		polls = 0;
	}
	virtual ~ObserverQueue() {
#ifdef OS
		vQueueDelete(queue);
#endif
	}
	bool putEvent(void *el) {
#ifdef OS
		return xQueueSend(queue, &el, 5);
#endif
		return false;
	}
	void *waitForEvent(uint32_t ticks) {
		polls++;
		void *result = 0;
#ifdef OS
		xQueueReceive(queue, &result, (TickType_t)ticks);
#endif
		return result;
	}

	const char *getName(void) {
	    return name;
	}
};

#endif /* INFRA_OBSERVER_H_ */
