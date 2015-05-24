#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

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
    virtual int write(char *buffer, int length) { return 0; }
    virtual int get_char(void) { return -1; }
    virtual void charout(int c) { }

    virtual int format(const char *fmt, ...);
    virtual int getstr(char *buffer, int length); // non blocking
};

#endif
