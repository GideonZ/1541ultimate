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
#include "lwip/sockets.h"
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
		close();
		return -2;
	} else { // n == 0
		printf("Socket got closed\n");
		close();
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

// What to do when the peer's buffer is full. A blocking write waits for room on the
// stack's own semaphore with no timeout at all (lwIP `netconn_apimsg`), which holds this
// task for as long as the peer stays away, so the write is asked not to block and the
// wait for room is bounded here. A screen this task draws is a snapshot rather than a
// stream, so a repaint that still finds no room is dropped whole and the session kept:
// the next repaint carries the state the screen has then. A repaint that is already part
// way out cannot be dropped, because the peer would be left with half an escape
// sequence, so that one is waited for far longer and only then ends the session. The
// waits are short because the task reads its input between screens: a long wait here
// stops it emptying the receive queue, which closes the window the peer needs in order to
// send the keys that would end the burst. A peer that has gone is reaped by the keepalive
// socket_gui.cc enables.
#define SEND_STALLS_TOLERATED 3
#define PART_SENT_STALLS_TOLERATED 150
#define SEND_STALL_WAIT_MS 200

static bool wait_for_room(int socket_fd)
{
	fd_set writeable;
	struct timeval tv;
	FD_ZERO(&writeable);
	FD_SET(socket_fd, &writeable);
	tv.tv_sec = 0;
	tv.tv_usec = SEND_STALL_WAIT_MS * 1000;
	return select(socket_fd + 1, NULL, &writeable, NULL, &tv) > 0;
}

int SocketStream :: transmit(const char *buffer, int out_length)
{
	int stalls = 0;
	bool part_sent = false;
	while(out_length > 0) {
		int n = send(actual_socket, buffer, out_length, MSG_DONTWAIT);
		if (n > 0) {
			out_length -= n;
			buffer += n;
			part_sent = true;
			stalls = 0;
			continue;
		}
		if ((n < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == ENOMEM))) {
			if (++stalls <= (part_sent ? PART_SENT_STALLS_TOLERATED : SEND_STALLS_TOLERATED)) {
				wait_for_room(actual_socket);
				continue;
			}
			if (!part_sent) {
				return 0; // nothing of this screen has gone out, so drop it
			}
		}
		printf("ERROR writing to socket %d. Errno = %d\n", n, errno);
		close();
		return -5;
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
