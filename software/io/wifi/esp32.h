/*
 * esp32.h
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */
#ifndef ESP32_H_
#define ESP32_H_

#include "dma_uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern "C" {
    #include "cmd_buffer.h"
}

class Esp32Application
{
public:
    Esp32Application() {}
    virtual ~Esp32Application() {}
    virtual void Init(DmaUART *uart, command_buf_context_t *packets) {}
    virtual void Start() {}
    virtual void Terminate() {}
};

class Esp32
{
    SemaphoreHandle_t rxSemaphore;
    QueueHandle_t commandQueue;
    command_buf_context_t *packets;
    Esp32Application *registered_app;
    Esp32Application *application;
    bool doClose;
    bool programError;

    void Enable(bool);
    void Disable();
    void Boot();
    void RefreshRoot();
    int  Download(const uint8_t *binary, uint32_t address, uint32_t length);
    void PackParams(uint8_t *buffer, int numparams, ...);
    bool Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data, uint8_t *receiveBuffer, int timeout);
    bool UartEcho(void);
    //bool RequestEcho(void);
    //void RxPacket(command_buf_t *);

    static void CommandTaskStart(void *context);
    void CommandThread();
public:
    Esp32();
    void Quit();

    BaseType_t doBootMode();
    BaseType_t doDisable();
    BaseType_t doStart();
    BaseType_t doDownload(uint8_t *binary, uint32_t address, uint32_t length, bool doFree);
    BaseType_t doDownloadWrap(bool start);
    BaseType_t doUartEcho(void);
    void AttachApplication(Esp32Application *app);
    DmaUART *uart;
};

extern Esp32 esp32;

#endif /* ESP32_H_ */
