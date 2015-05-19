#ifndef SMALL_PRINTF_H
#define SMALL_PRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

int printf(const char *fmt, ...);
int sprintf(char *, const char *fmt, ...);

int _my_vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap);

int sscanf(const char *, const char *fmt, ...);
int vsprintf(char *dest, const char *fmt, va_list ap);
int puts(const char *str);


//int  putchar(int a);

#include "itu.h"

//#define printf  small_printf
//#define sprintf small_sprintf
//#define puts    my_small_puts

#ifdef __cplusplus
}
#endif

#endif
