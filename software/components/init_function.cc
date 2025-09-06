/*
 * init_function.cc
 *
 *  Created on: Mar 12, 2015
 *      Author: Gideon
 */

#include "init_function.h"
#include <stdio.h>
static IndexedList<InitFunction *> *getInitFunctionList(void) {
	static IndexedList<InitFunction *> initFunctions(24, 0);
	return &initFunctions;
}

InitFunction::InitFunction(const char *name, initFunction_t func, void *obj, void *prm, int ord) {
	this->name = name;
    function = func;
	object = obj;
	param = prm;
	ordering = ord;

	getInitFunctionList()->append(this);
}

InitFunction::~InitFunction() {

}

void InitFunction::executeAll() {
    getInitFunctionList()->sort(InitFunction::compare);
    int elements = getInitFunctionList()->get_elements();
	for (int i=0; i < elements; i++) {
	    InitFunction *func = (*getInitFunctionList())[i];
        printf("----> Initializing %s (%d)...\n", func->name, func->ordering);
		func->function(func->object, func->param);
	}
	printf("---> All Init functions called.\n");
}

int InitFunction::compare(IndexedList<InitFunction *> *list, int a, int b)
{
    InitFunction *obj_a = (*list)[a];
    InitFunction *obj_b = (*list)[b];

    if(!obj_b)
        return 1;
    if(!obj_a)
        return -1;

    return obj_a->ordering - obj_b->ordering;
}

extern "C" {
	void execute_init_functions(void) {
		InitFunction::executeAll();
	}
}
