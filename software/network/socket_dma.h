/*
 * socket_test.h
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_DMA_H_
#define NETWORK_SOCKET_DMA_H_

#include "menu.h"
#include "filemanager.h"
#include "subsys.h"

#define SOCKET_BUFFER_SIZE 200000

class SocketDMA {
	static void dmaThread(void *a);
	static void performCommand(int socket, void *load_buffer, int length, uint16_t cmd, uint16_t len);
	static int  readSocket(int socket, void *buffer, int max_remain);
	static int  writeSocket(int socket, void *buffer, int length);

	uint8_t load_buffer[SOCKET_BUFFER_SIZE];
public:
	SocketDMA();
	virtual ~SocketDMA();

};

#endif /* NETWORK_SOCKET_DMA_H_ */
