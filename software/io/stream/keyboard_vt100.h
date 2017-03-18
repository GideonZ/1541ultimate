/*
 * keyboard_vt100.h
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#ifndef IO_STREAM_KEYBOARD_VT100_H_
#define IO_STREAM_KEYBOARD_VT100_H_

#include "keyboard.h"
#include "stream.h"

class Keyboard_VT100 : public Keyboard
{
	Stream *stream;

	typedef enum {
		e_esc_idle,
		e_esc_escape,
		e_esc_bracket,
		e_esc_o,
	} escape_state_t;

	escape_state_t escape_state;
	int escape_value;

public:
	Keyboard_VT100(Stream *s) {
		stream = s;
		escape_state = e_esc_idle;
		escape_value = 0;
	}

    ~Keyboard_VT100() {

    }

    int  getch(void);
    void wait_free(void);
    void clear_buffer(void);
};

/*
1B 5B 31 31 7E f1  \e[11~
1B 5B 31 32 7E f2  \e[12~
1B 5B 31 33 7E f3  \e[13~
1B 5B 31 34 7E f4  \e[14~
1B 5B 31 35 7E f5  \e[15~
1B 5B 31 37 7E f6  \e[17~
1B 5B 31 38 7E f7  \e[18~
1B 5B 31 39 7E f8  \e[19~
1B 5B 32 30 7E f9  \e[20~
1B 5B 32 31 7E f10 \e[21~
1B 5B 32 33 7E f11 \e[23~
1B 5B 32 34 7E f12 \e[24~
1B 5B 32 7E ins    \e[2~
7F del
1B 5B 31 7E home      \e[1~
1B 5B 35 7E page up   \e[5~
1B 5B 36 7E page down \e[6~
1B 5B 34 7E end       \e[4~
09 tab
1B 5B 44 left  \e[D
1B 5B 43 right \e[C
1B 5B 41 up    \e[A
1B 5B 42 down  \e[B
 *
 */



#endif /* IO_STREAM_KEYBOARD_VT100_H_ */
