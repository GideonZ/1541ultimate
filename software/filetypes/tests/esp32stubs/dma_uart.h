// Stand-in for the UART driver, for the host test of Esp32 :: Command and Esp32 :: VerifyFlash
// (esp32_verify_test.cpp). It sends nothing: what the code transmits is recorded, and what it
// receives is a list of frames the case scripted. Found before the stub of io/wifi/tests.
#ifndef ESP32_STUBS_DMA_UART_H
#define ESP32_STUBS_DMA_UART_H
#include <string.h>
#include <vector>
#include "FreeRTOS.h"
extern "C" {
    #include "cmd_buffer.h"
}
class DmaUART
{
public:
    bool txDebug;
    bool no_buffer;                              // GetBuffer fails
    std::vector<std::vector<uint8_t> > sent;     // frames handed to TransmitPacket
    std::vector<std::vector<uint8_t> > replies;  // frames ReceivePacket returns, in order
    size_t next_reply;
    int last_timeout;                            // of the last ReceivePacket
    command_buf_t tx, rx;
    DmaUART() : txDebug(false), no_buffer(false), next_reply(0), last_timeout(-1) { }
    BaseType_t GetBuffer(command_buf_t **b, TickType_t t) {
        if (no_buffer) return pdFALSE;
        *b = &tx;
        return pdTRUE;
    }
    void TransmitPacket(command_buf_t *b) { sent.push_back(std::vector<uint8_t>(b->data, b->data + b->size)); }
    BaseType_t ReceivePacket(command_buf_t **b, TickType_t t) {
        last_timeout = (int)t;
        if (next_reply >= replies.size()) return pdFALSE;   // the module says nothing
        const std::vector<uint8_t> &f = replies[next_reply++];
        memset(rx.data, 0xA5, sizeof(rx.data));
        memcpy(rx.data, f.data(), f.size());
        rx.size = f.size();
        *b = &rx;
        return pdTRUE;
    }
    void FreeBuffer(command_buf_t *) { }
    void ModuleCtrl(int) { }
    void PrintStatus() { }
};
#endif
