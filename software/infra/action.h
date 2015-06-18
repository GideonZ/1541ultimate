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

class SubsysCommand;

typedef int (*actionFunction_t)(SubsysCommand *cmd);

class Action
{
	char *actionName;
public:
	Action(const char *name, int ID, int f, int m=0) : subsys(ID), function(f), mode(m), func(0) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
	}

	Action(const char *name, actionFunction_t func, int f, int m=0) : subsys(-1), function(f), mode(m), func(func) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
	}

	virtual ~Action() {
		if (actionName) {
			delete actionName;
		}
	}

	virtual const char *getName() {
		return (const char *)actionName;
	}

	const actionFunction_t func;
	const int   subsys;
	const int   function;
	const int   mode;
};


#endif /* COMPONENTS_ACTION_H_ */
