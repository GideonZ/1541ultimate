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

typedef struct _fastuart_t
{
    uint8_t data;
    uint8_t get;
    uint8_t flags;
    uint8_t ictrl;
    uint8_t rate_l;
    uint8_t rate_h;
    uint8_t flowctrl;
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

    rxBuffer_t stdRx;
    rxBuffer_t slipRx;

    bool slipEnabled;
    bool slipMode;
    bool slipError;
    bool slipEscape;
    int slipLength;
    QueueHandle_t slipQueue;

    BaseType_t RxInterrupt();
    int ReadImpl(rxBuffer_t *b, uint8_t *buffer, int bufferSize);
public:
    FastUART(void *registers, void *sem)
    {
        uart = (volatile fastuart_t *) registers;
        rxSemaphore = sem;
        stdRx.rxHead = 0;
        stdRx.rxTail = 0;
        slipRx.rxHead = 0;
        slipRx.rxTail = 0;
        stdRx.rxBuffer = new uint8_t[RX_BUFFER_SIZE];
        slipRx.rxBuffer = new uint8_t[RX_BUFFER_SIZE];

        slipMode = false;
        slipLength = 0;
        slipEscape = false;
        slipError = false;
        slipQueue = xQueueCreate(8, sizeof(slipElement_t));
        slipEnabled = false;

        uint16_t rate = (((uint16_t)uart->rate_h) << 8) | uart->rate_l;

        printf("FastUART initialized. Start rate: %d bps\n", CLOCK_FREQ / (rate + 1));
    }

    void EnableRxIRQ(bool);
    void ClearRxBuffer(void);
    void SetBaudRate(int bps);
    static void FastUartRxInterrupt(void *context);

    // possibly blocking
    void Write(const uint8_t *buffer, int length);

    // never blocking
    int Read(uint8_t *buffer, int bufferSize);

    void EnableSlip(bool enabled);
    int GetSlipPacket(uint8_t *buffer, int bufferSize, uint32_t timeout);
    void SendSlipPacket(const uint8_t *buffer, int length);
    void SendSlipData(const uint8_t *buffer, int length);
    void SendSlipOpen(void);
    void SendSlipClose(void);
};

#endif /* FASTUART_H_ */
