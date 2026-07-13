#include "FreeRTOS.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Keep allocation sizes in our own header. heap_4's private header changes
 * size with the target alignment and must not be inspected here. */
static const size_t malloc_header_size =
    (sizeof(size_t) + portBYTE_ALIGNMENT - 1) & ~portBYTE_ALIGNMENT_MASK;

static void *malloc_allocate(size_t size)
{
    if (!size || size > (size_t)-1 - malloc_header_size)
        return NULL;

    uint8_t *mem = (uint8_t *)pvPortMalloc(size + malloc_header_size);
    if (!mem)
        return NULL;

    *((size_t *)mem) = size;
    return mem + malloc_header_size;
}

static void malloc_release(void *ptr)
{
    if (ptr)
        vPortFree((uint8_t *)ptr - malloc_header_size);
}

void * get_mem(size_t size) 
{
    //printf("New operator for size = %d, returned: \n", size);
    void *ret;
	ret = pvPortMalloc(size ? size : 1);

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
        return malloc_allocate(size);
    }

    // Newlib reentrant version
    void* __wrap__malloc_r(struct _reent *r, size_t size) {
        (void)r; // We ignore Newlib's reentrancy struct because FreeRTOS handles it
        return malloc_allocate(size);
    }

    // Do the same for free / _free_r
    void __wrap_free(void* ptr) {
        malloc_release(ptr);
    }

    void __wrap__free_r(struct _reent *r, void* ptr) {
        (void)r;
        malloc_release(ptr);
    }

    void *__wrap_calloc(size_t nmemb, size_t size) {
        if (nmemb && size > (size_t)-1 / nmemb)
            return NULL;
        size_t actual = nmemb * size;
        void *mem = malloc_allocate(actual);
        if (mem) {
            memset(mem, 0, actual);
        }
        return mem;
    }

    void *__wrap__calloc_r(struct _reent *r, size_t nmemb, size_t size) {
        (void)r; // We ignore Newlib's reentrancy struct because FreeRTOS handles it
        if (nmemb && size > (size_t)-1 / nmemb)
            return NULL;
        size_t actual = nmemb * size;
        void *mem = malloc_allocate(actual);
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
            return malloc_allocate(size);

        if (size == 0) {
            malloc_release(ptr);
            return NULL;
        }

        size_t old_size = *((size_t *)((uint8_t *)ptr - malloc_header_size));

        void *nw = malloc_allocate(size);
        if (!nw)
            return NULL;

        memcpy(nw, ptr, (old_size < size) ? old_size : size);
        malloc_release(ptr);
        return nw;
    }

    void *__wrap__realloc_r(struct _reent *r, void* ptr, size_t size)
    {
        (void)r;
        return __wrap_realloc(ptr, size);
    }
}
