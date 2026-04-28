/*
 * socket_gui.h
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_GUI_H_
#define NETWORK_SOCKET_GUI_H_

#include "FreeRTOS.h"
#include "task.h"

class SocketGui
{
public:
	TaskHandle_t listenTaskHandle;
#if U64
	TaskHandle_t monitorListenTaskHandle;
#endif

	SocketGui();
	int listenTask(void);
#if U64
	int listenMonitorTask(void);
#endif
};

#endif /* NETWORK_SOCKET_GUI_H_ */
