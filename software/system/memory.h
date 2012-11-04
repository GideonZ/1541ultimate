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
	int     mem_freed[NUM_MALLOCS];
	size_t  total_alloc;
	int     last_alloc;
    int     free_count;
    bool    enabled;
    
	MemManager();
	~MemManager();
	void *qalloc(size_t size);
	void qfree(void *p);
    void dump();
    void enable()  { enabled = true; }
    void disable() { enabled = false; }
    void toggle()  { enabled = !enabled; }
};

extern MemManager mem_manager;

#endif /* MEMORY_H_ */
