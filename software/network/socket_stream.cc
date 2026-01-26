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
#include <errno.h>
#include <unistd.h>

int SocketStream :: get_char()
{
	purge();

	char buffer[4];
	memset(buffer, 0, 4);
	int n = recv(actual_socket, buffer, 1, 0);
	if (n > 0) {
	    if (buffer[0] == -1) {
	        skip = 2;
	        return -1;
	    }
	    if (skip > 0) {
	        skip --;
	        return -1;
	    }
	    return (int)buffer[0];
	} else if (n < 0) {
		if (errno == EAGAIN)
			return -1;
		printf("ERROR reading from socket %d. Errno = %d", n, errno);
		return -2;
	} else { // n == 0
		printf("Socket got closed\n");
		return -2;
	}
	return -1;
}

int SocketStream :: write(const char *buffer, int out_length)
{
	if (actual_socket < 0) {
		return -6;
	}
	// first see if we can buffer the data locally
	if (buf_remaining >= out_length) {
		memcpy(buf_pos, buffer, out_length);
		buf_pos += out_length;
		buf_remaining -= out_length;
		return 0;
	}

	int ret = purge();
	if (ret)
		return ret;

	if (buf_remaining >= out_length) {
		memcpy(buf_pos, buffer, out_length);
		buf_pos += out_length;
		buf_remaining -= out_length;
		return 0;
	}

	// too large to store in buffer
	return transmit(buffer, out_length);
}

int SocketStream :: purge() {
	int len = buf_size - buf_remaining;
	if (!len)
		return 0;

	int ret = transmit(buf_start, len);
	buf_remaining = buf_size;
	buf_pos = buf_start;
	return ret;
}

int SocketStream :: transmit(const char *buffer, int out_length)
{
	while(out_length > 0) {
		int n = send(actual_socket, buffer, out_length, 0);
		if (n == out_length) {
			return 0; // OK!
		} else if (n < 0) {
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
	if(actual_socket >= 0) {
		shutdown(actual_socket, 2);
		lwip_close(actual_socket);
		actual_socket = -1;
	}
}