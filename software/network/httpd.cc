#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pattern.h"
#include "network_config.h"
#include "httpd.h"
#include "init_function.h"

extern "C" {
    #include "server.h" // from MicroHTTPServer
    #include "middleware.h" // from MicroHTTPServer
}

HTTPDaemon httpd; // the class that causes us to exist
HTTPServer srv;

HTTPDaemon::HTTPDaemon() : enabled(false), listenTaskHandle(NULL)
{
    new InitFunction("HTTP Daemon", [](void *obj, void *_param) { 
        HTTPDaemon *httpd = (HTTPDaemon *)obj;
        httpd->cfg = networkConfig.cfg;
        httpd->cfg->addObject(httpd);
        httpd->enabled = httpd->cfg->get_value(CFG_NETWORK_HTTP_SERVICE) != 0;
        xTaskCreate(http_listen_task, "HTTP Listener", configMINIMAL_STACK_SIZE, httpd, PRIO_NETSERVICE, &(httpd->listenTaskHandle));
    }, this, NULL, 103);
}

void HTTPDaemon::effectuate_settings(void)
{
    bool was_enabled = enabled;
    enabled = cfg->get_value(CFG_NETWORK_HTTP_SERVICE) != 0;
    if (was_enabled && !enabled && listenTaskHandle && listenTaskHandle != xTaskGetCurrentTaskHandle()) {
        int socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket >= 0) {
            struct sockaddr_in address;
            memset(&address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(MHS_PORT);
            connect(socket, (struct sockaddr *)&address, sizeof(address));
            closesocket(socket);
        }
    }
}

void HTTPDaemon::http_listen_task(void *a)
{
    HTTPDaemon *daemon = (HTTPDaemon *)a;
    printf("<http_listen_task>\n");
    while (1) {
        while (!daemon->enabled) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        /* Running the MicroHTTPServer code */
        HTTPServerInit(&srv, MHS_PORT);
        if (srv.sock < 0) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        printf("Run HTTP Server Loop.\n");
        bool accepting = true;
        while ((daemon->enabled && accepting) || srv.available_connections < MAX_HTTP_CLIENT) {
            if (!daemon->enabled) {
                FD_CLR(srv.sock, &srv._read_sock_pool);
                if (accepting) {
                    shutdown(srv.sock, SHUT_RDWR);
                    accepting = false;
                }
            }
            HTTPServerRun(&srv, Dispatch);
        }
        HTTPServerClose(&srv);
    }
}
