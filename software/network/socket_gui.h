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
#include <atomic>
#include "config.h"

class SocketGui : public ConfigurableObject
{
	std::atomic<bool> enabled;
public:
	TaskHandle_t listenTaskHandle;

	SocketGui();
	void effectuate_settings(void);
	int listenTask(void);
};

#endif /* NETWORK_SOCKET_GUI_H_ */
