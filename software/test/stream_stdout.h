#ifndef STREAM_STDOUT_H
#define STREAM_STDOUT_H

#include "stream.h"
#include <stdio.h>

class Stream_StdOut : public Stream
{
public:
    Stream_StdOut() { }
    ~Stream_StdOut() { }

//    int read(char *buffer, int length);
//    int write(const char *buffer, int length);
//    int get_char(void);
    void charout(int c) {
        putchar(c);
    }
};

#endif
