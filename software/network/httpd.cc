#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pattern.h"

#include "httpd.h"

static int num_threads = 0;

#ifdef HTTPD_DEBUG
int dbg_printf(const char *fmt, ...);
#else
#define dbg_printf(f, ...) /* */
#endif

HTTPDaemon httpd; // the class that causes us to exist

const char *dummy_header =
        "HTTP/1.1 200 OK\r\n"
        // "Date: Sat, 24 Jul 2021 19:37:30 GMT\r\n"
        "Server: Ultimate\r\n"
        //"Last-Modified: Mon, 04 May 2020 09:58:20 GMT\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: %d\r\n"
        //"Vary: Accept-Encoding,User-Agent\r\n"
        "Content-Type: %s\r\n"
        "\r\n";

const char *dummy_content =
"<html>\n"
"<head>\n"
"Ultimate 64 HTTPD\n"
"</head>\n"
"<body>\n"
"<br><br>There is nothing to see here. Why are you even trying this?<br><br>"
"Go to the <a href=\"http://ultimate64.com\">webshop</a> if you like.<br>\n"
"<form action=\"/some_action.php\" method=\"post\" enctype=\"multipart/form-data\">\n"
"  <input type=\"file\" id=\"myFile\" name=\"filename\">\n"
"  <input type=\"submit\">\n"
"</form>\n"
"</body>\n"
"</html>\n";

HTTPDaemon::HTTPDaemon()
{
    xTaskCreate(http_listen_task, "HTTP Listener", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &listenTaskHandle);
}

void HTTPDaemon::http_listen_task(void *a)
{
    HTTPDaemon *daemon = (HTTPDaemon *) a;
    int error = daemon->listen_task();
    printf("Going to suspend the HTTPDaemon. Error = %d\n", error);
    vTaskSuspend(NULL);
}

int HTTPDaemon::listen_task()
{
    int sockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        puts("HTTPD: ERROR opening socket");
        return -1;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 80;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
            sizeof(serv_addr)) < 0) {
        puts("HTTPD: ERROR on binding");
        return -2;
    }

    listen(sockfd, 2);

    while (1) {
        clilen = sizeof(cli_addr);
        int actual_socket = accept(sockfd, (struct sockaddr * ) &cli_addr, &clilen);
        if (actual_socket < 0) {
            puts("HTTPD: ERROR on accept");
            // return -3;
        }

        HTTPDaemonThread *thread = new HTTPDaemonThread(actual_socket, cli_addr.sin_addr.s_addr, cli_addr.sin_port);

        xTaskCreate(HTTPDaemonThread::run, "HTTP Task", configMINIMAL_STACK_SIZE, thread, tskIDLE_PRIORITY + 1, NULL);
    }
}

HTTPDaemonThread::HTTPDaemonThread(int socket, uint32_t addr, uint16_t port)
{
    num_threads++;
    this->socket = socket;

    {
        uint8_t *ip = (uint8_t *)&addr;
        your_ip[0] = ip[0]; //addr >> 24;
        your_ip[1] = ip[1]; //(addr >> 16) & 0xFF;
        your_ip[2] = ip[2]; //(addr >> 8) & 0xFF;
        your_ip[3] = ip[3]; //addr & 0xFF;
    }

    your_port = port;

    printf("HTTPDaemonThread(%d): connection from %d.%d.%d.%d:%d\n", num_threads, your_ip[0], your_ip[1], your_ip[2], your_ip[3], port);
}

void HTTPDaemonThread::run(void *a)
{
    HTTPDaemonThread *thread = (HTTPDaemonThread *) a;
    thread->handle_connection();
    lwip_close(thread->socket);
    delete thread;
    num_threads--;
    vTaskDelete(NULL);
}

int HTTPDaemonThread::handle_connection()
{
    char buffer[1024];
    while(1) {
        int n = recv(socket, buffer, 1024, 0);
        if (n > 0) {
            buffer[n] = 0;
            printf("This is what I got:\n%s\n", buffer);

            int len = sprintf(buffer, dummy_header, strlen(dummy_content), "text/html");
            send(socket, buffer, len, 0);

            send(socket, dummy_content, strlen(dummy_content), 0);
            break;
        } else if (n < 0) {
            if (errno == EAGAIN)
                continue;
            dbg_printf("HTTPD: ERROR reading from socket %d. Errno = %d", n, errno);
            return errno;
        } else { // n == 0
            dbg_printf("HTTPD: Socket got closed\n");
            break;
        }
    }
    return ERR_OK;
}
