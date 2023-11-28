#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "small_printf.h"

class Stream
{
    // for non-blocking string input
    int str_index;
    int str_state;
    static void _put(char c, void **param) {
    	((Stream *)param)->charout((int)c);
    }
public:
    Stream() {
    	str_index = 0;
    	str_state = 0;
    }
    
    virtual ~Stream() {}

    virtual int read(char *buffer, int length) { return 0; }
    virtual int write(const char *buffer, int length) { return 0; }
    virtual int get_char(void) { return -1; }
    virtual void charout(int c) { printf("[SBco%02x]", c); }

    virtual int format(const char *fmt, ...)
    {
        va_list ap;
        int ret = 0;

        va_start(ap, fmt);
        ret = _my_vprintf(Stream :: _put, (void **)this, fmt, ap);
        va_end(ap);

        return (ret);
    }

    virtual int getstr(char *buffer, int length) // non blocking
    {
        if(str_state == 0) {
            char *b = buffer;
            str_index = 0;
            while(*b) {
                charout(*(b++));
                str_index ++;
            }
            str_state = 1;
        }
        
        int c = get_char();
        if(c < 0)
            return -1;
            
        int ret = -1;
        if(c == '\r') {
            ret = str_index;
            str_state = 0; // exit
            charout('\n');
        } else if(c == 8) { // backspace
            if(str_index != 0) {
                str_index--;
                buffer[str_index] = '\0';
                format("\033[D \033[D");
            }
        } else if(str_index < length) {
            buffer[str_index++] = c;
            buffer[str_index] = 0;
            charout(c);
        }        
        return ret;  
    }

    virtual void sync(void) { }
};

#endif
