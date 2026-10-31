// Stand-in for the UART driver: what wifi.h, esp32.h and the WiFi application call. The
// bodies are in wifi_task_stubs.cpp, where they record what happens.
#ifndef WIFI_TESTS_DMA_UART_H
#define WIFI_TESTS_DMA_UART_H
#include "FreeRTOS.h"
extern "C" {
    #include "cmd_buffer.h"
}
class DmaUART
{
public:
    bool txDebug;
    void ModuleCtrl(int mode);
    BaseType_t GetBuffer(command_buf_t **b, TickType_t t);
    void FreeBuffer(command_buf_t *buf);
    void TransmitPacket(command_buf_t *b) {}
    void PrintStatus() {}
    void ResetReceiveCallback() {}
};
#endif
