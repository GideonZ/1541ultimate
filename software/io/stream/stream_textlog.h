/*
 * StreamTextLog.h
 *
 *  Created on: May 26, 2015
 *      Author: Gideon
 */

#ifndef IO_STREAM_STREAM_TEXTLOG_H_
#define IO_STREAM_STREAM_TEXTLOG_H_

#include "stream.h"
#include "small_printf.h"

class StreamTextLog
{
    static void _put(char c, void **param) {
    	((StreamTextLog *)param)->charout((int)c);
    }
    char *buffer;
    int offset;
    int size;
public:
    StreamTextLog(int size) {
    	this->size = size;
    	buffer = new char[size];
    	offset = 0;
    }
    ~StreamTextLog() {
    	delete buffer;
    }

    void charout(int c) {
    	if (offset < (size-1)) {
    		buffer[offset++] = (char)c;
    	}
    }

    char *getText(void) {
    	buffer[offset] = 0;
    	return buffer;
    }

    int format(const char *fmt, ...) {
        va_list ap;
        int ret;

        va_start(ap, fmt);
        ret = _my_vprintf(StreamTextLog :: _put, (void **)this, fmt, ap);
        va_end(ap);

        return (ret);
    }
};


#endif /* IO_STREAM_STREAM_TEXTLOG_H_ */
