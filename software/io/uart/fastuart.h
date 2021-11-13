/*
 * fastuart.h
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#ifndef FASTUART_H_
#define FASTUART_H_

#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "queue.h"
extern "C" {
    #include "cmd_buffer.h"
}

typedef struct _fastuart_t
{
    uint8_t data;
    uint8_t flags;
    uint8_t ictrl;
    uint8_t flowctrl;
    uint8_t rate_l;
    uint8_t rate_h;
    uint8_t rx_count;
    uint8_t tx_count;
} fastuart_t;

typedef struct
{
    int size;
    bool error;
} slipElement_t;

typedef struct
{
    int rxHead;
    int rxTail;
    uint8_t *rxBuffer;
} rxBuffer_t;

#define RX_BUFFER_SIZE 16384

class FastUART
{
    volatile fastuart_t *uart;
    void *rxSemaphore;
    bool slipMode;
    rxBuffer_t stdRx; // Linear Buffer

    // slip administration
    command_buf_context_t *packets;
    command_buf_t *current_tx_buf;
    command_buf_t *current_rx_buf;
    int current_tx_pnt;

    BaseType_t RxInterrupt();
    int ReadImpl(rxBuffer_t *b, uint8_t *buffer, int bufferSize);
public:
    FastUART(void *registers, void *sem, command_buf_context_t *pkts)
    {
        uart = (volatile fastuart_t *) registers;
        packets = pkts;
        rxSemaphore = sem;
        stdRx.rxHead = 0;
        stdRx.rxTail = 0;
        stdRx.rxBuffer = new uint8_t[RX_BUFFER_SIZE];
        current_tx_buf = NULL;
        current_rx_buf = NULL;
        current_tx_pnt = 0;
        slipMode = false;
        uint16_t rate = (((uint16_t)uart->rate_h) << 8) | uart->rate_l;

        printf("FastUART initialized. Start rate: %d bps\n", CLOCK_FREQ / (rate + 1));
    }

    void EnableSlip(bool enabled) { slipMode = enabled; }
    void EnableIRQ(bool);
    void ClearRxBuffer(void);
    void SetBaudRate(int bps);
    static void FastUartInterrupt(void *context);

    // never blocking
    int Read(uint8_t *buffer, int bufferSize);

/*    int GetSlipPacket(uint8_t *buffer, int bufferSize, uint32_t timeout);
    void SendSlipData(const uint8_t *buffer, int length);
    void SendSlipOpen(void);
    void SendSlipClose(void);
*/
    BaseType_t SendSlipPacket(const uint8_t *buffer, int length);
    BaseType_t TransmitPacket(command_buf_t *buf);
    BaseType_t ReceivePacket(command_buf_t **buf, TickType_t ticks);
    BaseType_t FreeBuffer(command_buf_t *buf);
    BaseType_t GetBuffer(command_buf_t **buf, TickType_t ticks);
};

#endif /* FASTUART_H_ */
