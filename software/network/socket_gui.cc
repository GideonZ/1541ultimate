/*
 * socket_gui.cc
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "socket_gui.h"
#include "socket_stream.h"

#include "event.h"
#include "screen.h"
#include "screen_vt100.h"
#include "keyboard_vt100.h"
#include "host_stream.h"
#include "ui_elements.h"
#include "browsable.h"
#include "browsable_root.h"
#include "tree_browser.h"

SocketGui socket_gui; // global that causes us to exist

SocketGui :: SocketGui()
{
	xTaskCreate( socket_gui_task, "\006Socket Gui", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
}

void socket_gui_task(void *a)
{
	SocketStream str;
	str.setup_socket();

	Screen *scr = new Screen_VT100(&str);
	Keyboard *keyb = new Keyboard_VT100(&str);

	UserInterface *user_interface = new UserInterface();
    HostStream *host = new HostStream(&str);
	user_interface->init(host);

    Browsable *root = new BrowsableRoot();
	TreeBrowser *tree_browser = new TreeBrowser(user_interface, root);
	user_interface->activate_uiobject(tree_browser);

	while(1) {
		user_interface->handle_event((Event &)c_empty_event);
		vTaskDelay(4);
	}

	str.close();

	user_interface->release_host();
	delete tree_browser;
	delete root;
	delete host;
	delete user_interface;
	delete keyb;
	delete scr;
}


