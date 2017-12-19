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
#include "dump_hex.h"
#include "usb_base.h"
#include "userinterface.h"

SocketTest socket_test; // global that causes the object to exist

SocketTest::SocketTest() {
//	xTaskCreate( restartThread, "Debug Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
}

SocketTest::~SocketTest() {

}

int  SocketTest::fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
//	item_list.append(new Action("Socket Test Server", SocketTest :: doTest1, 0, 0));
//	item_list.append(new Action("Socket Test Client", SocketTest :: doTest2, 1, 0));
	item_list.append(new Action("Start BUS Trace", SocketTest :: profiler, 2, 1));
	item_list.append(new Action("Stop BUS Trace", SocketTest :: profiler, 2, 0));
	item_list.append(new Action("Save BUS Trace", SocketTest :: saveTrace, 3, 0));
	return 3;
}

int SocketTest :: profiler(SubsysCommand *cmd) {
	if (cmd->mode) {
		PROFILER_START = 1;
	} else {
		PROFILER_STOP = 1;
	}
	return 0;
}

int SocketTest :: saveTrace(SubsysCommand *cmd)
{
	File *f;
	uint32_t start_address = 0x2000000;
	uint32_t end_address = 0x2000000;
	uint32_t transferred;

	FileManager *fm = FileManager :: getFileManager();

	bool progress = true;
	if (!cmd->user_interface)
		progress = false;

	end_address = PROFILER_ADDR;
	printf("Logic Analyzer stopped. Address = %p\n", end_address);

    static char buffer[32] = {0};
    strcpy(buffer, "bustrace.bin");
    int res = cmd->user_interface->string_box("Give name for trace file..", buffer, 24);
	if(res <= 0) {
		return 0;
	}

	FRESULT fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
	if(fres == FR_OK) {
		printf("Opened file successfully.\n");
		if(progress) {
			cmd->user_interface->show_progress("Saving Bus Trace..", 32);
		}
		while(start_address < end_address) {
			uint32_t now = end_address - start_address;
			if (now > 1024*1024)
				now = 1024*1024;
			f->write((void *)start_address, now, &transferred);
			if(progress) {
				cmd->user_interface->update_progress(NULL, 1);
			}
			start_address += now;
		}
		if(progress) {
			cmd->user_interface->hide_progress();
		}
		printf("written: %d...", transferred);
		fm->fclose(f);
	} else {
		printf("Couldn't open file..\n");
		return -1;
	}
	return 0;
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
    ioWrite8(ITU_IRQ_TIMER_EN, 0);
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    ioWrite8(ITU_IRQ_DISABLE, 0xFF);
    ioWrite8(ITU_IRQ_CLEAR, 0xFF);

//    asm("bralid r15, 8"); // restart!
    asm("nop");
}

int SocketTest::doTest1(SubsysCommand *cmd)
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
    	return -1;
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
	    return -2;
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
    	return -3;
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
	    return -4;
    }

	PROFILER_STOP = 1;
	uint32_t profile_size = PROFILER_ADDR - 0x1000000;

    lwip_close(newsockfd);
    lwip_close(sockfd);

    sendTrace(int(profile_size));
}

int SocketTest::doTest2(SubsysCommand *cmd)
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
    	return -1;
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

int SocketTest::sendTrace(int size)
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
    	return -1;
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
	    return -2;
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
    	return -3;
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
	    return -4;
    }

    lwip_close(newsockfd);
    lwip_close(sockfd);
}
