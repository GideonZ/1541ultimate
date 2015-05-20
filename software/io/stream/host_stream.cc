/*
 * host_stream.cc
 *
 *  Created on: May 20, 2015
 *      Author: Gideon
 */

#include "host_stream.h"
#include "screen_vt100.h"
#include "keyboard_vt100.h"

Screen   *HostStream :: getScreen(void)
{
	if (!screen) {
		screen = new Screen_VT100(stream);
	}
	return screen;
}

void HostStream :: releaseScreen(void)
{
	delete screen;
	screen = 0;
}

Keyboard *HostStream :: getKeyboard(void)
{
	if (!keyboard) {
		keyboard = new Keyboard_VT100(stream);
	}
	return keyboard;
}
