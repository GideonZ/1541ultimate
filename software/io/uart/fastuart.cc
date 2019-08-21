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
#include <stdio.h>

#define FUART_Overflow    0x01
#define FUART_TxFifoEmpty 0x04
#define FUART_TxFifoSpace 0x08
#define FUART_TxFifoFull  0x10
#define FUART_RxFifoFull  0x20
#define FUART_TxReady     0x40
#define FUART_RxDataAv    0x80

void FastUART::Write(const uint8_t *buffer, int length)
{
    for (int i = 0; i < length; i++) {
        while (uart->flags & FUART_TxFifoFull) {
            vTaskDelay(15); // 75 ms = 864 bytes @ 115200
            // TODO: Instead of a task delay, enable the interrupt and wait for the semaphore
        }
        uart->data = *(buffer++);
    }
}

#define sendchar(x) { 	while(uart->flags & FUART_TxFifoFull) { vTaskDelay(2); } uart->data = x; }

void FastUART::SendSlipData(const uint8_t *buffer, int length)
{
    // Escaped Body
    for (int i = 0; i < length; i++) {
        uint8_t data = *(buffer++);
        if (data == 0xC0) {
            sendchar(0xDB);
            sendchar(0xDC);
        } else if (data == 0xDB) {
            sendchar(0xDB);
            sendchar(0xDD);
        } else {
            sendchar(data);
        }
    }
}

void FastUART::SendSlipOpen(void)
{
    sendchar(0xC0);
}

void FastUART::SendSlipClose(void)
{
    sendchar(0xC0);
}

void FastUART::SendSlipPacket(const uint8_t *buffer, int length)
{
    // Start Character
    SendSlipOpen();
    // Body
    SendSlipData(buffer, length);
    // End Character
    SendSlipClose();
}

int FastUART::Read(uint8_t *buffer, int bufferSize)
{
    return ReadImpl(&stdRx, buffer, bufferSize);
}

int FastUART::GetSlipPacket(uint8_t *buffer, int bufferSize, uint32_t timeout)
{
    slipElement_t slipElement;

    if (xQueueReceive(slipQueue, &slipElement, timeout)) {
        if ((slipElement.size <= bufferSize) && (!slipElement.error)) {
            return ReadImpl(&slipRx, buffer, slipElement.size);
        }
        slipRx.rxTail += slipElement.size;
        slipRx.rxTail &= 4095;
        return -1;
    }
    return -2;
}

int FastUART::ReadImpl(rxBuffer_t *b, uint8_t *buffer, int bufferSize)
{
    int ret = 0;
    while ((b->rxTail != b->rxHead) && (ret < bufferSize)) {
        *(buffer++) = b->rxBuffer[b->rxTail++];
        ret++;
        b->rxTail &= 4095;
    }
    return ret;
}

void FastUART::EnableRxIRQ(bool enable)
{
    if (enable) {
        uart->ictrl |= 1;
    } else {
        uart->ictrl &= ~1;
    }
}

void FastUART::FastUartRxInterrupt(void *context)
{
    FastUART *u = (FastUART *) context;
    BaseType_t woken = u->RxInterrupt();
    if (woken == pdTRUE) {
        vTaskSwitchContext(); // TODO: This should not be here, but right now, the UART is the only client on this IRQ number
    }
}

BaseType_t FastUART::RxInterrupt()
{
    slipElement_t slipElement;
    BaseType_t woken = pdFALSE;
    bool giveSem = false;
    bool store;

    while (uart->flags & FUART_RxDataAv) {
        // try allocating space in buffer
        uint8_t data = uart->data;
        uart->get = 1; // possibly drop the char, if our buffer is full.. otherwise we keep getting interrupts
        //printf("[%b]", data);
        store = true;
        if (slipEnabled) {
            switch (data) {
            case 0xC0:
                store = false;
                slipEscape = false;
                if ((slipMode) && (slipLength > 0)) {
                    slipElement.size = slipLength;
                    slipElement.error = slipError;
                    xQueueSendFromISR(slipQueue, &slipElement, &woken);
                    slipError = false;
                    slipMode = false;
                    slipLength = 0;
                } else {
                    slipMode = true;
                    slipLength = 0;
                }
                break;
            case 0xDB:
                if (slipMode) {
                    slipEscape = true;
                    store = false;
                }
                break;
            case 0xDC:
                if (slipEscape) {
                    data = 0xC0;
                    slipLength++;
                    slipEscape = false;
                }
                break;
            case 0xDD:
                if (slipEscape) {
                    data = 0xDB;
                    slipLength++;
                    slipEscape = false;
                }
                break;
            default:
                slipEscape = false;
                if (slipMode) {
                    slipLength++;
                }
                break;
            }
        }

        rxBuffer_t* b;
        if (slipMode) {
            b = &slipRx;
        } else {
            b = &stdRx;
            giveSem = true;
        }

        if (store) {
            int next = (b->rxHead + 1) & 4095;
            if (next != b->rxTail) {
                b->rxBuffer[b->rxHead] = data;
                b->rxHead = next;
            } else if (slipMode) {
                slipError = true;
            }
        }
    }
    if (giveSem) {
        xSemaphoreGiveFromISR(rxSemaphore, &woken);
    }
    return woken;
}

void FastUART :: EnableSlip(bool enabled)
{
    slipEnabled = enabled;
    slipMode = false;
    slipError = false;
}
