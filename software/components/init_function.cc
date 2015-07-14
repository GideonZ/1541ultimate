/*
 * init_function.cc
 *
 *  Created on: Mar 12, 2015
 *      Author: Gideon
 */

#include "init_function.h"

static IndexedList<InitFunction *> *getInitFunctionList(void) {
	static IndexedList<InitFunction *> initFunctions(24, 0);
	return &initFunctions;
}

InitFunction::InitFunction(initFunction_t func, void *obj, void *prm) {
	function = func;
	object = obj;
	param = prm;

	getInitFunctionList()->append(this);
}

InitFunction::~InitFunction() {

}

void InitFunction::executeAll() {
	int elements = getInitFunctionList()->get_elements();
	for (int i=0; i < elements; i++) {
		InitFunction *func = (*getInitFunctionList())[i];
		func->function(func->object, func->param);
	}
}
