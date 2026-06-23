#include "FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

    void *__wrap_calloc(size_t nmemb, size_t size) {
        size_t actual = nmemb * size;
        void *mem = get_mem(actual);
        if (mem) {
            memset(mem, 0, actual);
        }
        return mem;
    }

    void *__wrap__calloc_r(struct _reent *r, size_t nmemb, size_t size) {
        (void)r; // We ignore Newlib's reentrancy struct because FreeRTOS handles it
        size_t actual = nmemb * size;
        void *mem = get_mem(actual);
        if (mem) {
            memset(mem, 0, actual);
        }
        return mem;
    }

    // And for realloc...
    void *__wrap_realloc(void *ptr, size_t size)
    {
        // FreeRTOS doesn't have a native realloc,
        // so we manually handle the logic to stay safe.
        if (ptr == NULL)
            return (size ? pvPortMalloc(size) : NULL);

        if (size == 0) {
            vPortFree(ptr);
            return NULL;
        }

        // heap_4: block size (incl. header, with the top "allocated" bit) sits at ptr-4
        size_t hdr = ((size_t *)ptr)[-1] & ~((size_t)1 << 31);
        size_t old_usable = hdr - 8 /* xHeapStructSize */;

        void *nw = pvPortMalloc(size);
        memcpy(nw, ptr, (old_usable < size) ? old_usable : size);
        vPortFree(ptr);
        return nw;
    }

    void *__wrap__realloc_r(struct _reent *r, void* ptr, size_t size)
    {
        (void)r;
        return __wrap_realloc(ptr, size);
    }
}
