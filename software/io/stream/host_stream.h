/*
 * host_stream.h
 *
 *  Created on: May 20, 2015
 *      Author: Gideon
 */

#ifndef IO_STREAM_HOST_STREAM_H_
#define IO_STREAM_HOST_STREAM_H_

#include "host.h"
#include "stream.h"

class HostStream: public GenericHost {
	Stream *stream;
	Screen *screen;
	Keyboard *keyboard;
public:
	HostStream(Stream *str) {
		screen = 0;
		keyboard = 0;
		stream = str;
	}

	virtual ~HostStream() {
		if (keyboard) {
			delete keyboard;
		}
	}

    bool exists(void) { return true; }
    bool is_accessible(void) { return true; }

    Screen   *getScreen(void);
    void releaseScreen(void);
    Keyboard *getKeyboard(void);
};

#endif /* IO_STREAM_HOST_STREAM_H_ */
