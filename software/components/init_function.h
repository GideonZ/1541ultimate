/*
 * init_function.h
 *
 *  Created on: Mar 12, 2015
 *      Author: Gideon
 */

#ifndef COMPONENTS_INIT_FUNCTION_H_
#define COMPONENTS_INIT_FUNCTION_H_

typedef void (*initFunction_t)(void *object, void *param);

#include "indexed_list.h"

class InitFunction {
	static IndexedList<InitFunction *> initFunctions;

	initFunction_t function;
	void *object;
	void *param;
public:
	InitFunction(initFunction_t func, void *obj, void *prm);
	virtual ~InitFunction();
	static void executeAll();
};

#endif /* COMPONENTS_INIT_FUNCTION_H_ */
