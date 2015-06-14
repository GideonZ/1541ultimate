/*
 * user_interface.h
 *
 *  Created on: May 16, 2015
 *      Author: Gideon
 */

#ifndef SOCKET_STREAM_H_
#define SOCKET_STREAM_H_

#define OUT_BUFFER_SIZE 1024

#include "stream.h"

class SocketStream : public Stream
{
	int  actual_socket;
public:

	SocketStream() {
		actual_socket = -1;
	}
	~SocketStream() { }

	int setup_socket();
	void close();

	// stream interface
	int write(char *buffer, int n);
	int get_char(void);
    void charout(int c)
    {
    	char cc = (char)c;
    	write(&cc, 1);
    }
};

#endif /* SOCKET_STREAM_H_ */
