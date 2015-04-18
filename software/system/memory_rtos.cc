#include <stdlib.h>
#include <string.h>
extern "C" {
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "FreeRTOS.h"


void * get_mem(size_t size) 
{
    //printf("New operator for size = %d, returned: \n", size);
    void *ret;
	ret = pvPortMalloc(size);

	if (!ret) {
        printf("** PANIC **: Error allocating memory..\n");
        while(1)
            ;
    }
    return ret;
}

void * operator new(size_t size)
{
    return get_mem(size);
}

void operator delete(void *p)
{
	vPortFree(p);
}

void * operator new[](size_t size)
{
    return get_mem(size);
}

void operator delete[](void *p)
{
	vPortFree(p);
}
