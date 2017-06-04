/*
 * socket_dma.cc
 *
 *  Created on: June 3, 2017
 *      Author: Gideon
 */

#include "itu.h"
#include "filemanager.h"
#include "socket_dma.h"
#include "socket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dump_hex.h"
#include "c64.h"
#include "c64_subsys.h"

SocketDMA socket_test; // global that causes the object to exist

SocketDMA::SocketDMA() {
	xTaskCreate( dmaThread, "DMA Load Task", configMINIMAL_STACK_SIZE, (void *)load_buffer, tskIDLE_PRIORITY + 1, NULL );
}

SocketDMA::~SocketDMA() {

}

void SocketDMA::dmaThread(void *load_buffer)
{
	int sockfd, newsockfd, portno;
	unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
	{
    	puts("ERROR dmaThread opening socket");
    	return;
	}

    printf("DMA Thread Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 64;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
 	    puts("dmaThread ERROR on binding");
	    return;
    }

    /* Now start listening for the clients, here process will
    * go in sleep mode and will wait for the incoming connection
    */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while(1) {
		/* Accept actual connection from the client */
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (newsockfd < 0)
		{
			puts("dmaThread ERROR on accept");
			return;
		}

		printf("dmaThread newsockfd = %8x\n", newsockfd);


		/* If connection is established then start communicating */
		char *mempntr = (char *)load_buffer;
		int max_remain = 65536;
		int received = 0;

		do {
			int current = (max_remain > 8192) ? 8192 : max_remain;
			n = recv(newsockfd, mempntr, current, 0);
			mempntr += n;
			max_remain -= n;
			received += n;
		} while((n > 0) && (max_remain > 0));

		lwip_close(newsockfd);
		if (n < 0) {
			puts("ERROR reading from socket");
		} else {
			SubsysCommand *c64_command;
			c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD, load_buffer, received);
        	c64_command->execute();
		}
    }

    // this will never happen
    lwip_close(sockfd);
}
