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

#define SOCKET_BUFFER_SIZE 65536

class SocketDMA {
	static void dmaThread(void *a);
	static void performCommand(int socket, void *load_buffer, int length, uint16_t cmd, uint32_t len, struct in_addr *client_ip);
	static int  readSocket(int socket, void *buffer, int max_remain);
	static int  writeSocket(int socket, void *buffer, int length);
	static int readSocketToFile(int socket, const char *filename, void *buffer, uint32_t len);
	uint8_t *load_buffer;
public:
	SocketDMA();
	virtual ~SocketDMA();

};

#endif /* NETWORK_SOCKET_DMA_H_ */
