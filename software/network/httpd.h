#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "FreeRTOS.h"
#include "task.h"
#include "indexed_list.h"

#define ERR_OK 0

class HTTPDaemon
{
	static void http_listen_task(void *a);
public:
	TaskHandle_t listenTaskHandle;

	HTTPDaemon();
	~HTTPDaemon() { }

	int listen_task(void);
};

class HTTPDaemonThread;

typedef void (HTTPDaemonThread::*func_t)(const char *args);

class HTTPDaemonThread
{
	int socket;
	uint8_t my_ip[4];
	uint8_t your_ip[4];
	uint16_t your_port;

	friend class HTTPDaemon;

	static void run(void *a);
	void send_msg(const char *a, ...);
	void dispatch_command(char *a, int length);
public:
	HTTPDaemonThread(int sock, uint32_t addr, uint16_t port);
	~HTTPDaemonThread() { }

	int handle_connection(void);
};

#endif
