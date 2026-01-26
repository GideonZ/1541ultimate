/*
 * sock_echo.c
 *
 *  Created on: Mar 16, 2015
 *      Author: Gideon
 */

#include "ipv4/lwip/inet.h"
#include "socket.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"

#define fcntl(a,b,c)          lwip_fcntl(a,b,c)

static void echo_run()
{
	int lSocket;
	struct sockaddr_in sLocalAddr;

	lSocket = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (lSocket < 0) {
		puts("lSocket < 0");
		return;
	}

	memset((char *)&sLocalAddr, 0, sizeof(sLocalAddr));
	sLocalAddr.sin_family = AF_INET;
	sLocalAddr.sin_len = sizeof(sLocalAddr);
	sLocalAddr.sin_addr.s_addr = 0L; //htonl(IP_ADDR_ANY);
	sLocalAddr.sin_port = htons(23);

	if (lwip_bind(lSocket, (struct sockaddr *)&sLocalAddr, sizeof(sLocalAddr)) < 0) {
		lwip_close(lSocket);
		puts("bind failed");
		return;
	}

	if ( lwip_listen(lSocket, 20) != 0 ) {
		lwip_close(lSocket);
		puts("listen failed");
		return;
	}

	while (1) {
		int clientfd;
		struct sockaddr_in client_addr;
		int addrlen=sizeof(client_addr);
		char buffer[1024];
		int nbytes;

		clientfd = lwip_accept(lSocket, (struct sockaddr*)&client_addr, (socklen_t *)&addrlen);
		if (clientfd >= 0) {
			puts("Accepted Echo Connection");

/*
		    int flags = fcntl(clientfd, F_GETFL, 0);
		    printf("flags = %d\n", flags);
		    fcntl(clientfd, F_SETFL, flags | O_NONBLOCK);

		    flags = fcntl(clientfd, F_GETFL, 0);
			if ((flags & O_NONBLOCK) == O_NONBLOCK) {
				printf("it's nonblocking");
			}
			else {
				printf("it's blocking.");
			}
*/
			struct timeval tv;
			tv.tv_sec = 500;
			tv.tv_usec = 500;
			setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

			do {
				nbytes = lwip_recv(clientfd, buffer, sizeof(buffer),0);

				if (nbytes > 0)
					lwip_send(clientfd, buffer, nbytes, 0);
				else
					lwip_send(clientfd, "#", 1, 0);
			}  while (nbytes != 0);

			puts("Closing connection.");
			lwip_close(clientfd);
		}
	}
	lwip_close(lSocket);
	puts("Socked closed.");
}

void echo_task( void *a ) {
	puts("Echo task has started.");
	echo_run();
	puts("Echo task has ended.");
	vTaskSuspend(NULL);
}