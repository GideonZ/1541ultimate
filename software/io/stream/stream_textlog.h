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
    bool enabled;
public:
    StreamTextLog(int size) {
    	buffer = new char[size];
    	this->size = size - 4;
    	offset = 0;
    	enabled = true;
    }
    ~StreamTextLog() {
    	delete buffer;
    }

    void charout(int c) {
        if (offset >= size) {
            offset = 0; // clear!
        }
        if (!enabled) {
            return;
        }
        if (c == 27) {
    		c = '<';
    	} else if (c == 9) {
    		c = ' ';
    	} else if ((c < 32) && (c != 10) && (c != 13)) {
    		return;
    	}
        buffer[offset++] = (char)c;
    }

    char *getText(void) {
    	buffer[offset] = 0;
    	return buffer;
    }

    int getLength(void) {
    	return offset;
    }

    int format(const char *fmt, ...) {
        va_list ap;
        int ret;

        va_start(ap, fmt);
        ret = _my_vprintf(StreamTextLog :: _put, (void **)this, fmt, ap);
        va_end(ap);

        return (ret);
    }

    void Reset(void) {
    	offset = 0;
    	enabled = true;
    }

    void Stop(void) {
    	enabled = false;
    }
};


#endif /* IO_STREAM_STREAM_TEXTLOG_H_ */
