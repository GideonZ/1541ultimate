/*
 * contextable.h
 *
 *  Created on: May 3, 2015
 *      Author: Gideon
 */

#ifndef COMPONENTS_CONTEXTABLE_H_
#define COMPONENTS_CONTEXTABLE_H_

#include "indexed_list.h"
#include "action.h"

class Contextable
{
public:
	Contextable() { }
	virtual ~Contextable() { }

	virtual void fetch_context_items(IndexedList<Action *>&items) { }
};



#endif /* COMPONENTS_CONTEXTABLE_H_ */
