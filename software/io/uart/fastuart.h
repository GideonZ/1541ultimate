/*
 * fastuart.h
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#ifndef FASTUART_H_
#define FASTUART_H_

#include <stdint.h>
#include "FreeRTOS.h"

typedef struct _fastuart_t {
    uint8_t data;
    uint8_t get;
    uint8_t flags;
    uint8_t ictrl;
} fastuart_t;

class FastUART {
    volatile fastuart_t *uart;
    void *rxSemaphore;
    uint8_t rxBuffer[8192];
    int rxHead;
    int rxTail;
    BaseType_t RxInterrupt();
public:
    FastUART(void *registers, void *sem)  {
        uart = (volatile fastuart_t *)registers;
        rxSemaphore = sem;
        rxHead = 0;
        rxTail = 0;
    }

    void EnableRxIRQ(bool);
    static void FastUartRxInterrupt(void *context);

    // possibly blocking
    void Write(const uint8_t *buffer, int length);

    // never blocking
    int  Read(uint8_t *buffer, int bufferSize);
};

#endif /* FASTUART_H_ */
