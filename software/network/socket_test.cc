/*
 * socket_test.cc
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#include "itu.h"
#include "filemanager.h"
#include "socket_test.h"
#include "socket.h"
#include "profiler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usb2.h"

SocketTest socket_test; // global that causes the object to exist
void poll_socket_test(Event &e) {
	socket_test.poll(e);
}

SocketTest::SocketTest() {
	MainLoop :: addPollFunction(poll_socket_test);
	fm = FileManager :: getFileManager();

	xTaskCreate( restartThread, "\007Reload Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
}

SocketTest::~SocketTest() {
	MainLoop :: removePollFunction(poll_socket_test);
}

int  SocketTest::fetch_task_items(IndexedList<Action*> &item_list)
{
	item_list.append(new ObjectMenuItem(this, "Socket Test Server", 0));
	item_list.append(new ObjectMenuItem(this, "Socket Test Client", 1));
	//item_list.append(new ObjectMenuItem(this, "Start RTOS Trace", 2));
	//item_list.append(new ObjectMenuItem(this, "Stop RTOS Trace", 3));
	item_list.append(new ObjectMenuItem(this, "Save RTOS Trace", 4));
	return 3;
}

void SocketTest::poll(Event &e)
{
	File *f;
	uint32_t start_address = 0x1000000;
	uint32_t end_address = 0x1000000;
	uint32_t transferred;

	if ((e.type == e_object_private_cmd) && (e.object == this)) {
		switch(e.param) {
		case 0:
			doTest1();
			break;
		case 1:
			doTest2();
			break;
		case 2:
			PROFILER_START = 1;
			break;
		case 3:
			PROFILER_STOP = 1;
			break;
		case 4:
            end_address = PROFILER_ADDR;
            printf("Logic Analyzer stopped. Address = %p\n", end_address);
            f = fm->fopen(NULL, "rtostrac.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS);
            if(f) {
                printf("Opened file successfully.\n");
                f->write((void *)start_address, end_address - start_address, &transferred);
                printf("written: %d...", transferred);
                f->close();
            } else {
                printf("Couldn't open file..\n");
            }
			break;
		default:
			break;
		}
	}
}

void SocketTest::restartThread(void *a)
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
    	puts("ERROR restartThread opening socket");
    	return;
	}

    printf("Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 12345;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
 	    puts("restartThread ERROR on binding");
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
    	puts("restartThread ERROR on accept");
    	return;
    }

    printf("newsockfd = %8x\n", newsockfd);

    PROFILER_STOP = 1;

    /* If connection is established then start communicating */
    char *mempntr = (char *)0x1C00004; // REU memory
    uint16_t start_time = getMsTimer();

    do {
    	n = recv(newsockfd, mempntr, 32768, 0);
    	mempntr += n;
    } while(n > 0);

    uint16_t stop_time = getMsTimer();
    uint16_t receive_time = stop_time - start_time;

    int received = int(mempntr) - 0x1C00004;
    printf("restartThread Received %d bytes in %d ms\n", received, receive_time);
    *((int *)0x1C00000) = (received >> 2);

    if (n < 0)
	{
	    puts("ERROR reading from socket");
	    return;
    }
    lwip_close(newsockfd);
    lwip_close(sockfd);

    portDISABLE_INTERRUPTS(); // disable interrupts
    usb2.deinit();
    ITU_IRQ_DISABLE = 0xFF;
    ITU_IRQ_CLEAR = 0xFF;
    asm("bralid r15, 8"); // restart!
    asm("nop");
}

void SocketTest::doTest1()
{
    int sockfd, newsockfd, portno;
	unsigned long int clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

	PROFILER_START = 1;

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
    char *mempntr = (char *)0x1800000; // REU memory
    uint16_t start_time = getMsTimer();

    do {
    	n = recv(newsockfd, mempntr, 32768, 0);
    	mempntr += n;
    } while(n > 0);

    uint16_t stop_time = getMsTimer();
    uint16_t receive_time = stop_time - start_time;

    printf("Received %d bytes in %d ms\n", int(mempntr) - 0x1800000, receive_time);

    if (n < 0)
	{
	    puts("ERROR reading from socket");
	    return;
    }

	PROFILER_STOP = 1;
	uint32_t profile_size = PROFILER_ADDR - 0x1000000;

    lwip_close(newsockfd);
    lwip_close(sockfd);

    sendTrace(int(profile_size));
}

void SocketTest::doTest2()
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

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("192.168.2.2");
    serv_addr.sin_port = htons(5001);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        puts("Error connecting.");

    char *mempntr = (char *)0x1800000; // REU memory
    uint16_t start_time = getMsTimer();

    for (int i=0; i < 1000; i++) {
    	n = send(sockfd, mempntr, 2048, 0);
    	mempntr += n;
    } while(n > 0);

    uint16_t stop_time = getMsTimer();
    uint16_t receive_time = stop_time - start_time;

    printf("Sent %d bytes in %d ms\n", int(mempntr) - 0x1000000, receive_time);

    lwip_close(sockfd);
}

void SocketTest::sendTrace(int size)
{
    int sockfd, newsockfd, portno;
	unsigned long int clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    printf("Setting up a socket to send %d bytes (port 5002)\n", size);

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
    portno = 5002;

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
    uint16_t start_time = getMsTimer();

    while(size > 0) {
    	n = (size > 1024) ? 1024 : size;
    	n = lwip_send(newsockfd, mempntr, n, 0);
    	if (n < 0)
    		break;
    	mempntr += n;
    	size -= n;
    }

    uint16_t stop_time = getMsTimer();
    uint16_t receive_time = stop_time - start_time;

    printf("Sent %d bytes in %d ms\n", int(mempntr) - 0x1000000, receive_time);

    if (n < 0)
	{
	    printf("ERROR sending to socket, %d", n);
	    return;
    }

    lwip_close(newsockfd);
    lwip_close(sockfd);
}

