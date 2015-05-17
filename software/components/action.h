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

typedef void (*actionFunction_t)(void *obj, void *param);

class Action
{
	const char *actionName;
	actionFunction_t callback;
	void *object;
	void *param;
public:

	Action(const char *name, actionFunction_t func, void *obj, void *prm) {
		actionName = name;
		callback = func;
		object = obj;
		param = prm;
	}

	Action(const char *name, actionFunction_t func, void *obj) {
		actionName = name;
		callback = func;
		object = obj;
		param = NULL;
	}

	virtual ~Action() { }

	virtual void execute() {
		callback(object, param);
	}

	virtual const char *getName() {
		return actionName;
	}
};


#endif /* COMPONENTS_ACTION_H_ */
