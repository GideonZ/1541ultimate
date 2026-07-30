/*
 * socket_gui.cc
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#include <sys/socket.h>
#include "lwip/sockets.h" // SO_KEEPALIVE / IPPROTO_TCP / TCP_KEEPIDLE|INTVL|CNT
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
	vTaskDelete(NULL);
}

// Cap on concurrent telnet sessions: bounds task/socket/netconn use so abandoned
// sessions cannot drain the shared lwIP netconn pool (which would also take
// REST/FTP down).
#define TELNET_MAX_SESSIONS 4
static int telnet_sessions = 0;

static void socket_gui_set_timeouts(int socket_fd)
{
    // Short receive timeout drives the responsive input-poll loop (get_char
    // returns -1 on no data). A longer send timeout tolerates a momentarily slow
    // terminal / large screen redraw while still bounding a truly stuck send
    // (which, with SO_SNDTIMEO now enabled in lwipopts, would otherwise block the
    // task indefinitely on a non-reading peer).
    struct timeval recv_tv;
    recv_tv.tv_sec = 0;
    recv_tv.tv_usec = 200000; // 200 ms
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_tv, sizeof(struct timeval));

    struct timeval send_tv;
    send_tv.tv_sec = 5;
    send_tv.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&send_tv, sizeof(struct timeval));

    // Reap half-open telnet sessions: a vanished peer (WiFi drop, powered-off
    // phone, AP roam) sends no FIN/RST, so run_remote() would poll recv()
    // forever and leak its TELNET_MAX_SESSIONS slot. Keepalive errors the pcb
    // once the peer stops ACKing, so recv() fails and the slot frees (~35s:
    // 20s idle + 3*5s). Needs LWIP_TCP_KEEPALIVE=1 in lwipopts.h.
    int keepalive_on = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&keepalive_on, sizeof(keepalive_on));
    int keepalive_idle = 20;   // idle seconds before first probe
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, (char *)&keepalive_idle, sizeof(keepalive_idle));
    int keepalive_intvl = 5;   // seconds between probes
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, (char *)&keepalive_intvl, sizeof(keepalive_intvl));
    int keepalive_cnt = 3;     // unacked probes -> peer dead
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, (char *)&keepalive_cnt, sizeof(keepalive_cnt));
}

SocketGui :: SocketGui()
{
    printf("Starting Telnet Server\n");
	xTaskCreate( socket_gui_listen_task, "Socket Gui Listener", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &listenTaskHandle );
}

static bool socket_ensure_authenticated(SocketStream *str) {
    char buf[32 + 1];
    char pos;
    int failure_sleep = 1;
    bool disconnect = false;
    bool authenticated = false;
    const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    if (!*password)
        return true;  // Empty password configured, all good

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
        return false;
    }
    return true;
}

// The code below runs once for every socket gui instance

void socket_gui_task(void *a)
{
	SocketStream *str = (SocketStream *)a;
	if (!socket_ensure_authenticated(str)) {
		portENTER_CRITICAL();
		telnet_sessions--;
		portEXIT_CRITICAL();
		vTaskDelete(NULL);
	}

	char product[64];
	char title[81];
	getProductVersionString(product, sizeof(product), true);
	snprintf(title, sizeof(title), "\eA*** %s *** Remote ***\eO", product);

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

	portENTER_CRITICAL();
	telnet_sessions--;
	portEXIT_CRITICAL();
	vTaskDelete(NULL);
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
    // Retry binding rather than exiting permanently: a transient failure would
    // otherwise kill the telnet listener until reboot.
    while (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        puts("Telnet: ERROR on binding; retrying in 2s");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    listen(sockfd, 2);

    while(1) {
    	clilen = sizeof(cli_addr);
		int actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (actual_socket < 0) {
			 puts("ERROR on accept");
			 vTaskDelay(100 / portTICK_PERIOD_MS);
			 continue;
		}

        socket_gui_set_timeouts(actual_socket);

		// Cap concurrent telnet sessions: refuse politely rather than let
		// abandoned connections accumulate and drain the shared netconn pool.
		if (telnet_sessions >= TELNET_MAX_SESSIONS) {
			const char *busy = "Too many connections, try again later.\r\n";
			send(actual_socket, busy, strlen(busy), 0);
			shutdown(actual_socket, 2);
			lwip_close(actual_socket);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			continue;
		}

		SocketStream *stream = new SocketStream(actual_socket);
		// telnet_sessions is decremented from each session task on exit; guard the
		// read-modify-write so concurrent decrements cannot lose an update and drift
		// the count (which would eventually make the cap refuse every connection).
		portENTER_CRITICAL();
		telnet_sessions++;
		portEXIT_CRITICAL();
		BaseType_t res = xTaskCreate( socket_gui_task, "Socket Gui Task", configMINIMAL_STACK_SIZE, stream, PRIO_USERIFACE, NULL );
		if (res != pdPASS) {
			puts("Telnet: xTaskCreate failed; dropping connection");
			portENTER_CRITICAL();
			telnet_sessions--;
			portEXIT_CRITICAL();
			stream->close();
			delete stream;
			vTaskDelay(100 / portTICK_PERIOD_MS);
			continue;
		}
    }
}
