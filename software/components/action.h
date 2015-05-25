/*
 * action.h
 *
 *  Created on: May 3, 2015
 *      Author: Gideon
 */

#ifndef COMPONENTS_ACTION_H_
#define COMPONENTS_ACTION_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef void (*actionFunction_t)(void *obj, void *param);

class Action
{
	char *actionName;
	actionFunction_t callback;
	void *object;
	void *param;
public:

	Action(const char *name, actionFunction_t func, void *obj, void *prm) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
		callback = func;
		object = obj;
		param = prm;
	}

	Action(const char *name, actionFunction_t func, void *obj) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
		callback = func;
		object = obj;
		param = NULL;
	}

	virtual ~Action() {
		if (actionName) {
			delete actionName;
		}
	}

	virtual void execute() {
		callback(object, param);
	}

	virtual const char *getName() {
		return (const char *)actionName;
	}
};


#endif /* COMPONENTS_ACTION_H_ */
