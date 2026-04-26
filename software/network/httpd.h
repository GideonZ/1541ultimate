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

#endif
