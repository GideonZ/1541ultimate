#ifndef SMALL_PRINTF_H
#define SMALL_PRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

int printf(const char *fmt, ...);
int sprintf(char *, const char *fmt, ...);

void _diag_write_char(char c, void **param);
void _string_write_char(char c, void **param);
int _vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap);
int  puts(const char *str);
//int  putchar(int a);

#include "itu.h"

//#define printf  small_printf
//#define sprintf small_sprintf
//#define puts    my_small_puts

#ifdef __cplusplus
}
#endif

#endif
