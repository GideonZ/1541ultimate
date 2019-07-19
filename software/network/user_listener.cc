/*
 * user_listener.cc
 *
 *  Created on: July 12, 2019
 *      Author: Scott Hutter
 */

#include "socket.h"
#include "errno.h"
#include "user_listener.h"

static void user_listener_listen_task(void *a)
{
	UserListener *listener = (UserListener *)a;
	listener->listenTask();
    vTaskDelete(NULL);
}

UserListener :: UserListener()
{
    state = INCOMING_SOCKET_STATE_NOT_LISTENING;
}

void UserListener :: listenTask(void)
{
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        puts("ERROR opening socket");
        close_all();
        state = INCOMING_SOCKET_STATE_PORT_IN_USE;
        return;
    }
    int optval = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(listen_socket, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        puts("ERROR on binding");
        close_all();
        state = INCOMING_SOCKET_STATE_BIND_ERROR;
        return;
    }

    int err = listen(listen_socket, 2);
    if(err < 0)
    {
        puts("ERROR on listen");
        close_all();
        state = INCOMING_SOCKET_STATE_PORT_IN_USE;
        return;
    }

    while(1) {
        clilen = sizeof(cli_addr);
        answer_socket = accept(listen_socket, (struct sockaddr *) &cli_addr, &clilen);

        if(state != INCOMING_SOCKET_STATE_LISTENING)
        {
            close_all();
            return;
        }
        else
        {
            if (answer_socket < 0) {
                puts("ERROR on accept");
                shutdown(answer_socket, SHUT_RDWR);
                closesocket(answer_socket);
                state = INCOMING_SOCKET_STATE_NOT_LISTENING;
                return;
            }

            struct timeval tv;
            tv.tv_sec = 20; // bug in lwip; this is just used directly as tick value
            tv.tv_usec = 20;
            setsockopt(answer_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

            state = INCOMING_SOCKET_STATE_CONNECTED;
        }
        
        
    }

}

uint8_t UserListener :: get_state(void)
{
    return state;
}

void UserListener :: set_port(uint16_t newport)
{
    port = newport;
}

void UserListener :: set_state(uint8_t newstate)
{
    state = newstate;

    if(state == INCOMING_SOCKET_STATE_LISTENING)
    {
        xTaskCreate( user_listener_listen_task, "Socket User Listener", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &listenTaskHandle );
    }
}

int UserListener :: get_socket()
{
    if(state == INCOMING_SOCKET_STATE_CONNECTED)
        return answer_socket;
    else
        return 0;
}

int UserListener :: close_all(void)
{
    shutdown(answer_socket, SHUT_RDWR);
    closesocket(answer_socket);

    shutdown(listen_socket, SHUT_RDWR);
    closesocket(listen_socket);
}