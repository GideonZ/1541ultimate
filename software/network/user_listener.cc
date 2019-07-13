/*
 * user_listener.cc
 *
 *  Created on: July 12, 2019
 *      Author: Scott Hutter
 */

#include "socket.h"
#include "user_listener.h"

static void user_listener_listen_task(void *a)
{
	UserListener *listener = (UserListener *)a;
	listener->listenTask();
	vTaskSuspend(NULL);
}

UserListener :: UserListener()
{
    state = INCOMING_SOCKET_STATE_NOT_LISTENING;
}

void UserListener :: listenTask(void)
{
	int sockfd, portno;
	socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    
    state = INCOMING_SOCKET_STATE_NOT_LISTENING;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
       puts("ERROR opening socket");
       return;
    }
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 6400;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0) {
        puts("ERROR on binding");
        return;
    }

    listen(sockfd, 2);
    state = INCOMING_SOCKET_STATE_LISTENING;

    while(1) {
    	clilen = sizeof(cli_addr);
		actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (actual_socket < 0) {
			 puts("ERROR on accept");
             state = INCOMING_SOCKET_STATE_NOT_LISTENING;
			 break;
		}
        
		struct timeval tv;
		tv.tv_sec = 20; // bug in lwip; this is just used directly as tick value
		tv.tv_usec = 20;
		setsockopt(actual_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

        state = INCOMING_SOCKET_STATE_CONNECTED;
    }
}

uint8_t UserListener :: get_state(void)
{
    return state;
}

void UserListener :: set_state(uint8_t newstate)
{
    state = newstate;

    if(state == INCOMING_SOCKET_STATE_LISTENING)
    {
       xTaskCreate( user_listener_listen_task, "Socket User Listener", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &listenTaskHandle );
    } 
    else if(state == INCOMING_SOCKET_STATE_NOT_LISTENING)
    {
        vTaskDelete(listenTaskHandle);
    }
    
}

int UserListener :: get_socket()
{
    if(state == INCOMING_SOCKET_STATE_CONNECTED)
        return actual_socket;
    else
        return 0;
    
}