/*
 * user_listener.h
 *
 *  Created on: Jul 7 2019
 *      Author: Scott Hutter
 */

#ifndef NETWORK_USER_LISTENER_H_
#define NETWORK_USER_LISTENER_H_

#include "FreeRTOS.h"
#include "task.h"

#define INCOMING_SOCKET_STATE_NOT_LISTENING     0x00
#define INCOMING_SOCKET_STATE_LISTENING         0x01
#define INCOMING_SOCKET_STATE_CONNECTED         0x02
#define INCOMING_SOCKET_STATE_BIND_ERROR        0x03

class UserListener
{
private:
    int actual_socket;
    uint8_t state;
    int port;
public:
	TaskHandle_t listenTaskHandle;

	UserListener();
	void listenTask(void);
    void set_state(uint8_t);
    uint8_t get_state(void);
    int get_socket(void);
    void set_port(int);
};

#endif /* NETWORK_SOCKET_GUI_H_ */
