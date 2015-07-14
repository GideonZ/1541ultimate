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

	SocketGui();
	int listenTask(void);
};

#endif /* NETWORK_SOCKET_GUI_H_ */
