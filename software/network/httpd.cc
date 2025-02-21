#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pattern.h"
#include "network_config.h"

#include "httpd.h"

extern "C" {
    #include "server.h" // from MicroHTTPServer
    #include "middleware.h" // from MicroHTTPServer
}

HTTPDaemon httpd; // the class that causes us to exist
HTTPServer srv;

HTTPDaemon::HTTPDaemon()
{
    xTaskCreate(http_listen_task, "HTTP Listener", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &listenTaskHandle);
}

void HTTPDaemon::http_listen_task(void *a)
{
    while (networkConfig.cfg->get_value(CFG_NETWORK_HTTP_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

/* Running the MicroHTTPServer code */
    printf("Waiting to start HTTP Server.\n");
    vTaskDelay(2000); /// wait for 10 sec

    printf("Init HTTP Server.\n");
	HTTPServerInit(&srv, MHS_PORT);
	/* Run the HTTP server forever. */
	/* Run the dispatch callback if there is a new request */
    printf("Run HTTP Server Loop.\n");
	HTTPServerRunLoop(&srv, Dispatch);
	HTTPServerClose(&srv);
    vTaskSuspend(NULL);
}
