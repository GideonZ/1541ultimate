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
#include "return_codes.h"

class SubsysCommand;
class Action;

typedef SubsysResultCode_e (*actionFunction_t)(SubsysCommand *cmd);
typedef SubsysResultCode_e (*directFunction_t)(Action *action, void *context);

class Action
{
	char *actionName;
	bool hidden;
	bool greyed;
	bool persistent;
	const void *object;
public:

    // Action defintion using a function code, dispatched by Subsys.
	// Examples: Flush Printer / Sample tape to TAP (task menu items only)
	Action(const char *name, int ID, int f, int m=0) : subsys(ID), function(f), mode(m), func(0), direct_func(NULL) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
		hidden = false;
		greyed = false;
		object = NULL;
        persistent = false;
	}

    // Action definition using a direct function call, without a Subsys
	// Examples: Create D64 (task!), Run Tape (context!)

	Action(const char *name, actionFunction_t func, int f, int m=0, void *obj=NULL) : subsys(-1), function(f), mode(m), func(func), object(obj), direct_func(NULL) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
        hidden = false;
        greyed = false;
        persistent = false;
	}

    // An Alternative Action definition using a direct function call, without a Subsys
    Action(const char *name, directFunction_t func, int param, void *obj = NULL) : subsys(-1), function(param), mode(param), direct_func(func), func(NULL), object(obj) {
		if(name) {
			actionName = new char[strlen(name)+1];
			strcpy(actionName, name);
		} else {
			actionName = 0;
		}
		hidden = false;
		greyed = false;
		object = NULL;
        persistent = false;
    }

	virtual ~Action() {
		if (actionName) {
			delete actionName;
		}
	}

	virtual const char *getName() {
		return (const char *)actionName;
	}

	const void *getObject(void) {
	    return object;
	}
	void setObject(const void *obj) {
	    object = obj;
	}

	void setHidden(bool h) {
	    hidden = h;
	}
	void setDisabled(bool d) {
	    greyed = d;
	}
	void hide(void) {
	    hidden = true;
	}
	void show(void) {
	    hidden = false;
	}
    void disable(void) {
        greyed = true;
    }
	void enable(void) {
	    greyed = false;
	}
	bool isShown(void) {
	    return !hidden;
	}
	bool isEnabled(void) {
	    return !greyed;
	}
	bool isPersistent(void) {
	    return persistent;
	}
	void setPersistent(void) {
	    persistent = true;
	}

	const actionFunction_t func;
	const directFunction_t direct_func;
    const int   subsys;
	const int   function;
	const int   mode;
};

#endif /* COMPONENTS_ACTION_H_ */
