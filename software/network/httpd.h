#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "indexed_list.h"

#define ERR_OK 0

class HTTPDaemon : public ConfigurableObject
{
	static void http_listen_task(void *a);
	volatile bool enabled;
public:
	TaskHandle_t listenTaskHandle;

	HTTPDaemon();
	~HTTPDaemon() { }

	void effectuate_settings(void);
	int listen_task(void);
};

#endif
