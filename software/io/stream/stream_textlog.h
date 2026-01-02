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
    bool buffer_owned;
    int offset;
    int size;
    bool enabled;
public:
    StreamTextLog(int size) {
    	buffer = new char[size];
        buffer_owned = true;
    	this->size = size - 4;
    	offset = 0;
    	enabled = true;
    }

    StreamTextLog(int size, char *existing) {
    	buffer = existing;
        buffer_owned = false;
    	this->size = size - 4;
    	offset = 0;
    	enabled = true;
    }

    ~StreamTextLog() {
        if (buffer_owned) {
    	    delete buffer;
        }
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

    void raw(const char *data) {
        int len = strlen(data);
        if (offset + len >= size) {
            offset = 0; // clear!
        }
        if (!enabled) {
            return;
        }
        memcpy(buffer + offset, data, len);
        offset += len;
    }

    char *getText(void) {
    	buffer[offset] = 0;
    	return buffer;
    }

    int getLength(void) {
    	return offset;
    }

    int format_ap(const char *fmt, va_list ap) {
        return _my_vprintf(StreamTextLog :: _put, (void **)this, fmt, ap);
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
