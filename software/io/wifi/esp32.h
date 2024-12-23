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

#include "menu.h"

extern "C" {
    #include "cmd_buffer.h"
}

#if U64 == 2
#   define ESP_MODE_OFF       3
#   define ESP_MODE_RUN       0
#   define ESP_MODE_BOOT      1
#   define ESP_MODE_RUN_UART  4
#   define ESP_MODE_BOOT_UART 5
#else
#   define ESP_MODE_OFF       0
#   define ESP_MODE_RUN       3
#   define ESP_MODE_BOOT      2
#   define ESP_MODE_RUN_UART  7
#   define ESP_MODE_BOOT_UART 6
#endif

class Esp32Application
{
public:
    Esp32Application() {}
    virtual ~Esp32Application() {}
    virtual void Init(DmaUART *uart, command_buf_context_t *packets) {}
    virtual void Start() {}
    virtual void Terminate() {}
};

class Esp32 //: public ObjectWithMenu
{
//    TaskCategory *taskCategory;
//    void create_task_items(void);
    SemaphoreHandle_t rxSemaphore;
    QueueHandle_t commandQueue;
    command_buf_context_t *packets;
    Esp32Application *registered_app;
    Esp32Application *application;
    bool doClose;
    bool programError;

    void StartApp();
    void Disable();
    void Boot();
    int  Download(const uint8_t *binary, uint32_t address, uint32_t length);
    void PackParams(uint8_t *buffer, int numparams, ...);
    bool Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data, uint8_t *receiveBuffer, int timeout);
    bool UartEcho(void);
    //bool RequestEcho(void);
    //void RxPacket(command_buf_t *);

    static void CommandTaskStart(void *context);
    void CommandThread();
    static SubsysResultCode_e S_mode(SubsysCommand *cmd);
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
    void ReadRxMessage(void);
    int  DetectModule(void *buffer, int buffer_len);
    void EnableRunMode();
};

extern Esp32 esp32;

#endif /* ESP32_H_ */
