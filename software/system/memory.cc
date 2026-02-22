#include <stdlib.h>
#include <string.h>
#include "memory.h"
extern "C" {
    #include "dump_hex.h"
    #include "small_printf.h"
}
#define USE_MEM_TRACE 0
#define MEM_VERBOSE 1

MemManager :: MemManager()
{
    printf("*** CREATING MEMORY MANAGER ***\n");
	for(int i=0;i<NUM_MALLOCS;i++) {
		mem_locations[i] = 0;
		mem_lengths[i] = 0;
    	mem_freed[i] = -2;
	}
	total_alloc = 0;
	last_alloc = 0;
    free_count = 0;
    enabled = false;
    dump();
}

MemManager :: ~MemManager()
{
	printf("\nTotal leak: %d bytes.", total_alloc);
	for(int i=0;i<NUM_MALLOCS;i++) {
		if(mem_lengths[i]) {
    		printf("NF %4d: Size: %7d  Loc: %p. Total = %d\n", i, mem_lengths[i], mem_locations[i], total_alloc);
    		dump_hex(mem_locations[i], mem_lengths[i]);
    		total_alloc -= mem_lengths[i];
		}
	}
}

void MemManager :: dump()
{
    printf("The memory manager is currently %s\n", enabled?"enabled":"disabled");
	printf("Total allocated: %d bytes.", total_alloc);
    for(int i=0;i<last_alloc;i++) {
		printf("%4d: Size: %7d  Loc: %p. Freed: %4d\n", i, mem_lengths[i], mem_locations[i], mem_freed[i]);
    }
}
    
void *MemManager :: qalloc(size_t size)
{
    void *p = malloc(size);
    if(!p) {
    	printf("alloc failed.\n");
    	while(1)
    		;
    }
    memset(p, 0xCC, size);

/*
    for(int i=0;i<NUM_MALLOCS;i++) {
    	if(!mem_lengths[i]) {
    		mem_lengths[i] = size;
    		mem_locations[i] = p;
    		total_alloc += size;
#if MEM_VERBOSE > 0
    		printf("++ %4d: Size: %7d  Loc: %p. Total = %d\n", i, size, p, total_alloc);
#endif
    		return p;
    	}
    }
    printf("No space in the alloc list..\n");
    while(1)
    	;
*/
    if(last_alloc < NUM_MALLOCS) {
    	mem_lengths[last_alloc] = size;
    	mem_locations[last_alloc] = p;
    	mem_freed[last_alloc] = -1;
    	total_alloc += size;
#if MEM_VERBOSE > 0
		printf("++ %4d: Size: %7d  Loc: %p. Total = %d\n", last_alloc, size, p, total_alloc);
#endif
		last_alloc ++;
		return p;
    }

    printf("No space in the alloc list..\n");
	while(1);
}

void MemManager :: qfree(void *p)
{
	for(int i=0;i<NUM_MALLOCS;i++) {
		if((mem_locations[i] == p)&&(mem_freed[i] < 0)) {
			total_alloc -= mem_lengths[i];
#if MEM_VERBOSE > 0
    		printf("-- %4d: Size: %7d  Loc: %p. Total = %d\n", i, mem_lengths[i], p, total_alloc);
#endif
    		memset(p, i, mem_lengths[i]);
			mem_freed[i] = free_count++;
			free(p);
			return;
		}
	}
	printf("Trying to free a location that is not in the alloc list: %p\n", p);
//	while(1)
//		;
}

#if USE_MEM_TRACE > 0
MemManager mem_manager;
#endif

extern char _heap[];

void * get_mem(size_t size) 
{
    //printf("New operator for size = %d, returned: \n", size);
    void *ret;
#if USE_MEM_TRACE == 1
    if(mem_manager.enabled) {
        ret = mem_manager.qalloc(size);
    } else {
        ret = malloc(size);
    }
#else
	ret = malloc(size);
#endif
    if (!ret) {
        printf("** PANIC **: Error allocating memory..\n");
        while(1)
            ;
    }
    // printf("%p\n", ret);
    return ret;
}

void * operator new(size_t size)
{
    return get_mem(size);
}

void operator delete(void *p)
{
#if USE_MEM_TRACE == 1
    if(mem_manager.enabled) {
    	mem_manager.qfree(p);
    } else {
    	free(p);
    }
#else
    //printf("Freeing %p\n", p);
    free(p);
#endif
}

void operator delete(void *p, unsigned int something)
{
#if USE_MEM_TRACE == 1
    if(mem_manager.enabled) {
        mem_manager.qfree(p);
    } else {
        free(p);
    }
#else
    //printf("Freeing %p\n", p);
    free(p);
#endif
}

void * operator new[](size_t size)
{
    return get_mem(size);
}

void operator delete[](void *p)
{
#if USE_MEM_TRACE == 1
    if(mem_manager.enabled) {
    	mem_manager.qfree(p);
    } else {
    	free(p);
    }
#else
	free(p);
#endif
}

extern "C" void __cxa_pure_virtual()
{
}

extern "C" void  __cxa_atexit()
{
}
