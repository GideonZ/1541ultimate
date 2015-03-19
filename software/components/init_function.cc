/*
 * init_function.cc
 *
 *  Created on: Mar 12, 2015
 *      Author: Gideon
 */

#include "init_function.h"

IndexedList<InitFunction *> InitFunction :: initFunctions(24, 0);

InitFunction::InitFunction(initFunction_t func, void *obj, void *prm) {
	function = func;
	object = obj;
	param = prm;

	initFunctions.append(this);
}

InitFunction::~InitFunction() {

}

void InitFunction::executeAll() {
	int elements = initFunctions.get_elements();
	for (int i=0; i < elements; i++) {
		InitFunction *func = initFunctions[i];
		func->function(func->object, func->param);
	}
}
