#include <stddef.h>
#include <stdint.h>
// Freestanding helpers; no C runtime, constructors, heap or device initialization.
extern "C" void *memset(void *dest, int value, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)dest;
    for (size_t i = 0; i < size; ++i) p[i] = uint8_t(value);
    return dest;
}
extern "C" void *memcpy(void *dest, const void *src, size_t size)
{
    volatile uint8_t *d = (volatile uint8_t *)dest;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < size; ++i) d[i] = s[i];
    return dest;
}
extern "C" uint32_t __mulsi3(uint32_t a, uint32_t b)
{
    uint32_t result = 0;
    while (b) { if (b & 1) result += a; a <<= 1; b >>= 1; }
    return result;
}
