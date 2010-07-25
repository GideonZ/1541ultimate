/*
 * memory.h
 *
 *  Created on: Apr 11, 2010
 *      Author: Gideon
 */

#ifndef MEMORY_H_
#define MEMORY_H_

#define NUM_MALLOCS 4000

class MemManager
{
public:
	void   *mem_locations[NUM_MALLOCS];
	size_t  mem_lengths[NUM_MALLOCS];
	size_t  total_alloc;
	int     last_alloc;

	MemManager();
	~MemManager();
	void *qalloc(size_t size);
	void qfree(void *p);
};


#endif /* MEMORY_H_ */
