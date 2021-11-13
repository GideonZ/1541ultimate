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
extern "C" {
    #include "cmd_buffer.h"
}

// This class provides an interface to the WiFi module, to manage and program it
// When in operational mode, it dispatches the received packets to the
// socket layer wrapper functions.

class WiFi
{
    FastUART *uart;
    SemaphoreHandle_t rxSemaphore;
    QueueHandle_t commandQueue;
    TaskHandle_t runModeTask;
    command_buf_context_t *packets;

    bool doClose;
    bool programError;

    void Enable();
    void Disable();
    void Boot();
    int  Download(const uint8_t *binary, uint32_t address, uint32_t length);
    void PackParams(uint8_t *buffer, int numparams, ...);
    bool Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data, uint8_t *receiveBuffer, int timeout);
    bool RequestEcho(void);

    static void CommandTaskStart(void *context);
    static void RunModeTaskStart(void *context);
/*
    TaskHandle_t relayTask;
    static void EthernetRelay(void *context);
    void Listen();
*/

    void CommandThread();
    void RunModeThread();
    int actualSocket;
public:
    WiFi();
    void Quit();

    BaseType_t doBootMode();
    BaseType_t doStart();
    BaseType_t doDownload(uint8_t *binary, uint32_t address, uint32_t length, bool doFree);
    BaseType_t doDownloadWrap(bool start);
    BaseType_t doRequestEcho(void);
};


extern WiFi wifi;


#endif /* WIFI_H_ */
