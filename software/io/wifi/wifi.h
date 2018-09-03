/*
 * wifi.h
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#ifndef WIFI_H_
#define WIFI_H_

#include "fastuart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// This class provides an interface to the WiFi module
// Initially, a simple Uart based interface that links to a TCP socket
// Later, this class can provide the bridge between the 6551 emulation and the WiFi module
// or even use the SPI interface for faster transfers

class WiFi
{
    FastUART *uart;
    SemaphoreHandle_t rxSemaphore;
    TaskHandle_t relayTask;
    bool doClose;

    static void TaskStart(void *context);
    static void EthernetRelay(void *context);
    void Thread();
    int actualSocket;
public:
    WiFi();

    void Enable();
    void Disable();
    void Boot();
};






#endif /* WIFI_H_ */
