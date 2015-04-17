/*
 * socket_test.cc
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#include "socket_test.h"
#include "socket.h"
#include "itu.h"

SocketTest socket_test; // global that causes the object to exist
void poll_socket_test(Event &e) {
	socket_test.poll(e);
}

SocketTest::SocketTest() {
	main_menu_objects.append(this);
	poll_list.append(poll_socket_test);
}

SocketTest::~SocketTest() {
	poll_list.remove(poll_socket_test);
	main_menu_objects.remove(this);
}

int  SocketTest::fetch_task_items(IndexedList<PathObject*> &item_list)
{
	item_list.append(new ObjectMenuItem(this, "Socket Test", 0));
	return 1;
}

void SocketTest::poll(Event &e)
{
	if ((e.type == e_object_private_cmd) && (e.object == this)) {
		switch(e.param) {
		case 0:
			printf("You selected socket test.\n");
			doTest1();
			break;
		default:
			break;
		}
	}
}

void SocketTest::doTest1()
{
    int sockfd, newsockfd, portno;
	unsigned long int clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
	{
    	puts("ERROR opening socket");
    	return;
	}

    printf("Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
 	    puts("ERROR on binding");
	    return;
    }

    /* Now start listening for the clients, here process will
    * go in sleep mode and will wait for the incoming connection
    */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    /* Accept actual connection from the client */
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0)
    {
    	puts("ERROR on accept");
    	return;
    }

    printf("newsockfd = %8x\n", newsockfd);

    /* If connection is established then start communicating */
    char *mempntr = (char *)0x1000000; // REU memory
    WORD start_time = ITU_MS_TIMER;

    do {
    	n = recv(newsockfd, mempntr, 2048, 0);
    	mempntr += n;
    } while(n > 0);

    WORD stop_time = ITU_MS_TIMER;
    WORD receive_time = stop_time - start_time;

    printf("Received %d bytes in %d ms\n", int(mempntr) - 0x1000000, receive_time);

    if (n < 0)
	{
	    puts("ERROR reading from socket");
	    return;
    }

    close(newsockfd);
    close(sockfd);
}
