/*
 * user_interface.h
 *
 *  Created on: May 16, 2015
 *      Author: Gideon
 */

#ifndef SOCKET_STREAM_H_
#define SOCKET_STREAM_H_

#define OUT_BUFFER_SIZE 2048

#include "stream.h"

class SocketStream : public Stream
{
	int  actual_socket;
	int  purge();
	int  transmit(const char *buffer, int n);

	int buf_size;
	int buf_remaining;
	char *buf_pos;
	char *buf_start;
public:
	SocketStream(int sock) {
		actual_socket = sock;
		buf_size = OUT_BUFFER_SIZE;
		buf_start = new char[buf_size];
		buf_pos = buf_start;
		buf_remaining = buf_size;
	}
	~SocketStream() {
		delete[] buf_start;
	}

	void close();

	// stream interface
	int write(const char *buffer, int n);
	int get_char(void);
    void charout(int c)
    {
    	char cc = (char)c;
    	write(&cc, 1);
    }

    void sync(void)
    {
    	purge();
    }
};

#endif /* SOCKET_STREAM_H_ */
