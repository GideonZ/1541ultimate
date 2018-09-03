/*
 * fastuart.c
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#include "fastuart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define FUART_Overflow    0x01
#define FUART_TxFifoEmpty 0x04
#define FUART_TxFifoSpace 0x08
#define FUART_TxFifoFull  0x10
#define FUART_RxFifoFull  0x20
#define FUART_TxReady     0x40
#define FUART_RxDataAv    0x80


void FastUART :: Write(const uint8_t *buffer, int length)
{
    for (int i=0; i < length; i++) {
        while(uart->flags & FUART_TxFifoFull) {
            vTaskDelay(15); // 75 ms = 864 bytes @ 115200
            // TODO: Instead of a task delay, enable the interrupt and wait for the semaphore
        }
        uart->data = *(buffer++);
    }
}

int FastUART :: Read(uint8_t *buffer, int bufferSize)
{
    int ret = 0;
    while ((rxTail != rxHead) && (ret < bufferSize)) {
        *(buffer++) = rxBuffer[rxTail++];
        ret++;
        rxTail &= 8191;
    }
    return ret;
}

void FastUART :: EnableRxIRQ(bool enable)
{
    if (enable) {
        uart->ictrl |= 1;
    } else {
        uart->ictrl &= ~1;
    }
}

void FastUART :: FastUartRxInterrupt(void *context)
{
    FastUART *u = (FastUART *)context;
    BaseType_t woken = u->RxInterrupt();
    if (woken == pdTRUE) {
        vTaskSwitchContext(); // TODO: This should not be here, but right now, the UART is the only client on this IRQ number
    }
}

BaseType_t FastUART :: RxInterrupt()
{
    while(uart->flags & FUART_RxDataAv) {
        // try allocating space in buffer
        int next = (rxHead + 1) & 8191;
        if (next != rxTail) {
            rxBuffer[rxHead] = uart->data;
            rxHead = next;
        }
        uart->get = 1; // possibly drop the char, if our buffer is full.. otherwise we keep getting interrupts
    }
    BaseType_t woken;
    xSemaphoreGiveFromISR(rxSemaphore, &woken);
    return woken;
}
