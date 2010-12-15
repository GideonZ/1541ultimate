#ifndef SMALL_PRINTF_H
#define SMALL_PRINTF_H

//extern "C" {
	#include <stdarg.h>

    int small_printf(const char *fmt, ...);
    int small_sprintf(char *, const char *fmt, ...);

	void _diag_write_char(char c, void **param);
	void _string_write_char(char c, void **param);
	int _vprintf(void (*putc)(char c, void **param), void **param, const char *fmt, va_list ap);

	#include "itu.h"
//}

#define printf  small_printf
#define sprintf small_sprintf

#endif
