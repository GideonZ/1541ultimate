#include "FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>

void * get_mem(size_t size) 
{
    //printf("New operator for size = %d, returned: \n", size);
    void *ret;
	ret = pvPortMalloc(size);

	if (!ret) {
        printf("** PANIC **: Error allocating %p..\n", __builtin_return_address(0));
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


extern "C" void __cxa_pure_virtual()
{
}

void *__dso_handle = 0;
extern "C" void  __cxa_atexit()
{
}

extern "C" {
    // Standard version
    void* __wrap_malloc(size_t size) {
        return get_mem(size);
    }

    // Newlib reentrant version
    void* __wrap__malloc_r(struct _reent *r, size_t size) {
        (void)r; // We ignore Newlib's reentrancy struct because FreeRTOS handles it
        return get_mem(size);
    }

    // Do the same for free / _free_r
    void __wrap_free(void* ptr) {
        vPortFree(ptr);
    }

    void __wrap__free_r(struct _reent *r, void* ptr) {
        (void)r;
        vPortFree(ptr);
    }

    // And for realloc...
    void *__wrap_realloc(void* ptr, size_t size) {
        // FreeRTOS doesn't have a native realloc,
        // so we manually handle the logic to stay safe.
        vPortFree(ptr);
        if (size == 0) {
            return nullptr;
        }
        
        return pvPortMalloc(size);
    }

    void *__wrap__realloc_r(struct _reent *r, void* ptr, size_t size)
    {
        (void)r;

        // FreeRTOS doesn't have a native realloc,
        // so we manually handle the logic to stay safe.
        vPortFree(ptr);
        if (size == 0) {
            return nullptr;
        }
        
        return pvPortMalloc(size);
    }
}
