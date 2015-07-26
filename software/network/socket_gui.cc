/*
 * socket_gui.cc
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#include "socket.h"
#include "socket_gui.h"
#include "socket_stream.h"

#include "screen.h"
#include "screen_vt100.h"
#include "keyboard_vt100.h"
#include "host_stream.h"
#include "ui_elements.h"
#include "browsable.h"
#include "browsable_root.h"
#include "tree_browser.h"

SocketGui socket_gui; // global that causes us to exist

static void socket_gui_listen_task(void *a)
{
	SocketGui *gui = (SocketGui *)a;
	gui->listenTask();
	vTaskDelete(gui->listenTaskHandle);
}

SocketGui :: SocketGui()
{
	xTaskCreate( socket_gui_listen_task, "\006Socket Gui Listener", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &listenTaskHandle );
}

// The code below runs once for every socket gui instance

void socket_gui_task(void *a)
{
	SocketStream *str = (SocketStream *)a;

	HostStream *host = new HostStream(str);
//	Screen *scr = host->getScreen();
//	Keyboard *keyb = host->getKeyboard();

	UserInterface *user_interface = new UserInterface();
	user_interface->init(host);

	Browsable *root = new BrowsableRoot();
	TreeBrowser *tree_browser = new TreeBrowser(user_interface, root);
	user_interface->activate_uiobject(tree_browser);

	user_interface->run();

	puts("User Interface on stream returned.");
	str->close();

	delete user_interface;
	puts("user_interface gone");
	delete root;
	puts("root gone");
	delete host;
	puts("host gone");
	delete str;
	puts("str gone");

	vTaskSuspend(NULL);
	puts("You shouldn't ever see this.");
}

int SocketGui :: listenTask(void)
{
	int sockfd, portno;
	socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
       puts("ERROR opening socket");
       return -1;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 12345;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0) {
        puts("ERROR on binding");
        return -2;
    }

    listen(sockfd, 2);

    while(1) {
    	clilen = sizeof(cli_addr);
		int actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (actual_socket < 0) {
			 puts("ERROR on accept");
			 return -3;
		}

		struct timeval tv;
		tv.tv_sec = 500; // bug in lwip; this is just used directly as tick value
		tv.tv_usec = 500;
		setsockopt(actual_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

		SocketStream *stream = new SocketStream(actual_socket);
		xTaskCreate( socket_gui_task, "\003Socket Gui Task", configMINIMAL_STACK_SIZE, stream, tskIDLE_PRIORITY + 1, NULL );
    }
}

