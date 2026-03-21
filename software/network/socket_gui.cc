/*
 * socket_gui.cc
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#include <sys/socket.h>
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
#include "versions.h"
#include "home_directory.h"
#include "init_function.h"
#include "network_config.h"
#include "product.h"

SocketGui *socket_gui = NULL;
InitFunction init_socket_gui("Telnet Server", [](void *_obj, void *_param) { socket_gui = new SocketGui(); }, NULL, NULL, 100); // global that causes us to exist

static void socket_gui_listen_task(void *a)
{
	SocketGui *gui = (SocketGui *)a;
	gui->listenTask();
	vTaskSuspend(NULL);
//	vTaskDelete(gui->listenTaskHandle);
}

SocketGui :: SocketGui()
{
    printf("Starting Telnet Server\n");
	xTaskCreate( socket_gui_listen_task, "Socket Gui Listener", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &listenTaskHandle );
}

static void socket_ensure_authenticated(SocketStream *str) {
    char buf[32 + 1];
    char pos;
    int failure_sleep = 1;
    bool disconnect = false;
    bool authenticated = false;
    const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    if (!*password)
        return;  // Empty password configured, all good

    int attempts = 5;
    int attempt_delay = 250;
    while (!disconnect && !authenticated && attempts > 0) {
        str->write("Password: ", 10);
        str->write("\xff\xfb\x01", 3);  // Telnet: IAC WILL ECHO (Disable client local echo)
        pos = 0;
        while (true) {
            int ch = str->get_char();
            if (ch == -1) {  // No data yet
                continue;
            }
            else if (ch == -2) {  // Socket was closed
                disconnect = true;
                break;
            }
            else if (ch == '\xff') {  // Telnet IAC: ignore commands (discard the next two bytes)
                int ignore = 2;
                while (!disconnect && ignore > 0) {
                    int cmd = str->get_char();
                    if (cmd == -2) {
                        disconnect = true;
                    }
                    else if (cmd >= 0) {
                        --ignore;
                    }
                }
                if (disconnect)
                    break;
            }
            else if (ch == '\r') {
                continue;  // Ignore CR and wait for LF
            }
            else if (ch == '\0' || ch == '\n') {  // End of line (or NULL byte)
                buf[pos] = 0;
                str->write("\r\n", 2);
                str->sync();
                password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
                if (!*password || strcmp(buf, password) == 0) {
                    printf("Telnet connection logged in\n");
                    authenticated = true;
                }
                else {
                    // Failed login, throttle and flush input
                    printf("Telnet connection invalid password\n");
                    vTaskDelay(attempt_delay / portTICK_PERIOD_MS);
                    str->write("\r\nIncorrect password!\r\n", 2 + 19 + 2);
                    --attempts;
                    attempt_delay <<= 1;  // Exponential delay, 0.25 -> 4s delay (5 attempts)
                    while (str->get_char() >= 0)
                        ;
                }
                break;
            }
            else if (ch == '\b' || ch == '\x7f' || ch == '\x7e') {  // Backspace in some form
                if (pos > 0) {
                    str->write("\b \b", 3);
                    --pos;
                }
            }
            else {
                if (pos < sizeof(buf) - 1) {   // Buffer
                    buf[pos++] = (ch & 0xff);
                    str->write("*", 1);
                }
            }
        }
        str->write("\xff\xfc\x01", 3);  // Telnet: IAC WONT ECHO (Enable client local echo)
        str->write("\r\n", 2);
    }
    if (disconnect || !authenticated) {
        printf("Telnet connection closed before successful authentication\n");
        str->close();
        delete(str);
        vTaskSuspend(NULL);
        puts("You shouldn't ever see this.");
        while (1)
            ;
    }
}

// The code below runs once for every socket gui instance

void socket_gui_task(void *a)
{
	SocketStream *str = (SocketStream *)a;
	socket_ensure_authenticated(str);

	char product[64];
	char title[81];
	getProductVersionString(product, sizeof(product), true);
	sprintf(title, "\eA*** %s *** Remote ***\eO", product);

	HostStream *host = new HostStream(str);
//	Screen *scr = host->getScreen();
//	Keyboard *keyb = host->getKeyboard();

	UserInterface *user_interface = new UserInterface(title, false);
	user_interface->init(host);

	Browsable *root = new BrowsableRoot();
	TreeBrowser *tree_browser = new TreeBrowser(user_interface, root);
	user_interface->activate_uiobject(tree_browser);

    if(user_interface->cfg->get_value(CFG_USERIF_START_HOME)) {
        new HomeDirectory(user_interface, tree_browser);
        // will clean itself up
    }

	user_interface->run_remote();

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
    while (networkConfig.cfg->get_value(CFG_NETWORK_TELNET_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    puts("Telnet server starting");

	int sockfd, portno;
	socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
       puts("ERROR opening socket");
       return -1;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 23;
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
		tv.tv_sec = 0;
		tv.tv_usec = 200000; // 200 ms
		setsockopt(actual_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

		SocketStream *stream = new SocketStream(actual_socket);
		xTaskCreate( socket_gui_task, "Socket Gui Task", configMINIMAL_STACK_SIZE, stream, PRIO_USERIFACE, NULL );
    }
}
