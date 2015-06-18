#ifndef STREAM_UART_H
#define STREAM_UART_H

#include "stream.h"

class Stream_UART : public Stream
{
public:
    Stream_UART() { }
    ~Stream_UART() { }

    int read(char *buffer, int length);
    int write(const char *buffer, int length);
    int get_char(void);
    void charout(int c);
};

#endif
