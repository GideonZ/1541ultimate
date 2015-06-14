//============================================================================
// Name        : user_interface.cpp
// Author      : Gideon
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "socket_stream.h"
#include "screen.h"
#include "screen_vt100.h"
#include "keyboard_vt100.h"
#include "host_stream.h"
#include "ui_elements.h"
#include "browsable.h"
#include "browsable_root.h"
#include "tree_browser.h"

UserInterface *user_interface;

int SocketStream :: setup_socket() {

	int sockfd, portno, clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
       perror("ERROR opening socket");
       return -1;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 12345;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        return -2;
    }

    listen(sockfd, 2);

    clilen = sizeof(cli_addr);
    actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (actual_socket < 0) {
         perror("ERROR on accept");
         return -3;
    }

    int status = fcntl(actual_socket, F_SETFL, fcntl(actual_socket, F_GETFL, 0) | O_NONBLOCK);

    if (status == -1){
        perror("calling fcntl");
        return -4;
    }
    return 0;
}

int SocketStream :: get_char()
{
	char buffer[4];
	memset(buffer, 0, 16);
	int n = recv(actual_socket, buffer, 1, 0);
	if (n > 0) {
		return (int)buffer[0];
	}
	if ((n < 0) && (errno != EWOULDBLOCK)) {
		printf("ERROR reading from socket %d. Errno = %d", n, errno);
		return -4;
	}
	return -1;
}

int SocketStream :: write(char *buffer, int out_length)
{
	if (actual_socket < 0) {
		return -6;
	}
	while(out_length > 0) {
		int n = send(actual_socket, buffer, out_length, 0);
		if (n == out_length) {
			return 0; // OK!
		} else if ((n < 0) && (errno != EWOULDBLOCK)) {
			perror("ERROR writing to socket");
			return -5;
		} else {
			out_length -= n;
			buffer += n;
		}
	}
	return 0;
}

void SocketStream :: close()
{
	if(actual_socket > 0) {
		shutdown(actual_socket, 2);
		//close(actual_socket);
	}
}


#include <stdio.h>

extern "C" void main_loop(void *a);

int main()
{
/*	char *char_matrix = new char[2000];
	char *color_matrix = new char[2000];

	Screen *scr = new Screen_MemMappedCharMatrix(char_matrix, color_matrix, 60, 25);
	scr->enableVT100Parsing();
	scr->cursor_visible(1);
	scr->move_cursor(10, 10);
	scr->repeat('a', 10);
	scr->move_cursor(25, 11);
	scr->output("\e[5mThis is in a different color.");

	Window *win = new Window(scr, 5, 5, 50, 15);
	win->clear();
	win->draw_border();
	win->output("My name is Gideon");
	win->move_cursor(100, 100); // outside of the range
	win->output_line("This is a very long line, and I don't know if it will fit inside of the window.");

	FILE *f;
	f = fopen("screen_dump.txt", "wb");
	int i = 0;
	char c;
	for(int y=0;y<scr->get_size_y();y++) {
		for(int x=0;x<scr->get_size_x();x++) {
			c = char_matrix[i++];
			if (c == 0)
				c = 32;
			else if ((c & 0xE0) == 0)
				c |= 0xE0;
			fwrite(&c, 1, 1, f);
		}
		fprintf(f, "\n");
	}
	fclose(f);
	delete win;
	delete scr;
	delete char_matrix;
	delete color_matrix;*/

	SocketStream str;

	str.setup_socket();
	str.write((char *)"Hello!\r\n", 8);
	str.format("This is awesome. It took me %d hours, but then, look!\n", 51);

	Screen *scr = new Screen_VT100(&str);
	scr->set_color(2);
	scr->output("This should be printed in red.\n");

	Keyboard *keyb = new Keyboard_VT100(&str);

/*
	char buffer[32];
	Event e(e_nop, 0, 0);
    int ret;

    strcpy(buffer, "Gideon Z");
	UIObject *ui = new UIStringBox("Enter your name:", buffer, 32);
	ui->init(scr, keyb);
    do {
        ret = ui->poll(0, e);
    } while(!ret);
    ui->deinit();
    delete ui;

    scr->clear();
    scr->set_color(6);

	ui = new UIPopup("Select a button", BUTTON_OK | BUTTON_CANCEL | BUTTON_ALL | BUTTON_YES );
	ui->init(scr, keyb);
    do {
        ret = ui->poll(0, e);
    } while(!ret);
    ui->deinit();
    delete ui;

    scr->clear();
    scr->set_color(5);

	UIStatusBox *st = new UIStatusBox("Wait please...", 2);
	st->init(scr);
	for(int i=0;i<2;i++) {
		sleep(1);
		st->update(0, 1);
	}
    st->deinit();
    delete st;
*/

	user_interface = new UserInterface();
    HostStream *host = new HostStream(&str);
	user_interface->init(host);

//    Browsable *root = new BrowsableTest(100, true, "Root");
    Browsable *root = new BrowsableRoot();
	TreeBrowser *tree_browser = new TreeBrowser(root);
	user_interface->activate_uiobject(tree_browser);
    push_event(e_button_press, NULL, 1);

    main_loop(0);

    delete keyb;

	str.close();
}

