#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "integer.h"

// base class uses UART as I/O...

class Stream
{
    int vprintf(char *fmt, va_list ap);

    // for non-blocking string input
    int str_index;
    int str_state;
public:
    Stream() {
        str_index = 0;
        str_state = 0;
    }
    
    virtual ~Stream() {}

    virtual int read(BYTE *buffer, int length);    
    virtual int write(BYTE *buffer, int length);
    virtual int getchar(void);
    virtual void charout(int c);
    virtual int format(char *fmt, ...);
    virtual int getstr(char *buffer, int length); // non blocking
    
    int  convert_out(int val, char *buf, int radix, char *digits, int leading_zeros, int width);
    int  convert_in(char *buf, int radix, char *digits);
    void hex(int val, char *buf, int len);
};

#endif
