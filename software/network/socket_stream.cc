//============================================================================
// Name        : user_interface.cpp
// Author      : Gideon
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include "socket_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#if RUNS_ON_PC
	#include <netinet/in.h>
#else
	#define fcntl(a,b,c)          lwip_fcntl(a,b,c)
#endif

#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>

int SocketStream :: setup_socket()
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

    clilen = sizeof(cli_addr);
    actual_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (actual_socket < 0) {
         puts("ERROR on accept");
         return -3;
    }

    int status = fcntl(actual_socket, F_SETFL, fcntl(actual_socket, F_GETFL, 0) | O_NONBLOCK);

    if (status == -1){
        puts("calling fcntl");
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

int SocketStream :: write(const char *buffer, int out_length)
{
	if (actual_socket < 0) {
		return -6;
	}
	while(out_length > 0) {
		int n = send(actual_socket, buffer, out_length, 0);
		if (n == out_length) {
			return 0; // OK!
		} else if ((n < 0) && (errno != EWOULDBLOCK)) {
			puts("ERROR writing to socket");
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
