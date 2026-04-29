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

static const uint32_t socket_listener_stack_size = configMINIMAL_STACK_SIZE * 2;
static const uint32_t socket_session_stack_size = configMINIMAL_STACK_SIZE * 4;

static void socket_gui_listen_task(void *a)
{
	SocketGui *gui = (SocketGui *)a;
	gui->listenTask();
	vTaskDelete(NULL);
}

typedef struct {
    SocketStream *stream;
} SocketGuiSessionContext;

SocketGui :: SocketGui()
{
    printf("Starting Telnet Server\n");
	xTaskCreate( socket_gui_listen_task, "Socket Gui Listener", socket_listener_stack_size, this, PRIO_NETSERVICE, &listenTaskHandle );
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
    SocketGuiSessionContext *context = (SocketGuiSessionContext *)a;
    SocketStream *str = context->stream;
    delete context;
	if (!socket_ensure_authenticated(str)) {
		vTaskDelete(NULL);
	}

    char product[64];
    char title[81];
    getProductVersionString(product, sizeof(product), true);
    sprintf(title, "\eA*** %s *** Remote ***\eO", product);

    HostStream *host = new HostStream(str);
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

	vTaskDelete(NULL);
}

static int socket_gui_accept_loop(int portno, const char *label)
{
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    while (1) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            puts("ERROR opening socket");
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        int enable = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(enable));

        memset((char *) &serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(portno);
        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            puts("ERROR on binding");
            lwip_close(sockfd);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        if (listen(sockfd, 2) < 0) {
            puts("ERROR on listen");
            lwip_close(sockfd);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        printf("%s starting on port %d\n", label, portno);

        while (1) {
	    	clilen = sizeof(cli_addr);
            int actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
            if (actual_socket < 0) {
                 puts("ERROR on accept");
                 vTaskDelay(100 / portTICK_PERIOD_MS);
                 continue;
            }

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200 ms
            setsockopt(actual_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

            SocketStream *stream = new SocketStream(actual_socket);
            SocketGuiSessionContext *context = new SocketGuiSessionContext;
            context->stream = stream;
            BaseType_t res = xTaskCreate(socket_gui_task, "Socket Gui Task",
                                         socket_session_stack_size, context, PRIO_USERIFACE, NULL);
            if (res != pdPASS) {
                puts("Telnet: xTaskCreate failed; dropping connection");
                delete context;
                stream->close();
                delete stream;
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }
    }
}

int SocketGui :: listenTask(void)
{
    while (networkConfig.cfg->get_value(CFG_NETWORK_TELNET_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    return socket_gui_accept_loop(23, "Telnet remote menu service");
}
