/*
 * dma_uart.h
 *
 *  Created on: Sep 3, 2023
 *      Author: gideon
 */

#ifndef DMAUART_H_
#define DMAUART_H_

#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "itu.h"

extern "C" {
    #include "cmd_buffer.h"
}

typedef struct _dma_uart_t
{
    uint8_t rate_l;
    uint8_t rate_h;
    union {
        uint8_t ictrl;
        uint8_t status;
    };
    uint8_t flowctrl;
    uint8_t *tx_addr;
    uint16_t length;
    uint8_t tx_push;
    uint8_t rx_pop;
    uint8_t *rx_addr;
} dma_uart_t;

#define DMAUART_HWFLOWCTRL  0x01
#define DMAUART_LOOPBACK    0x02
#define DMAUART_SLIPENABLE  0x04
#define DMAUART_MOD_BOOT    0x10
#define DMAUART_MOD_ENABLE  0x20
#define DMAUART_RESET       0x80

#define DMAUART_CLR_OVERFLOW 0x08 // clear overflow status

typedef BaseType_t (*isr_receive_callback_t)(command_buf_context_t *context, command_buf_t *b, BaseType_t *w);

class DmaUART
{
    volatile dma_uart_t *uart;
    int my_irq;
    void *rxSemaphore;
    bool slipMode;
    isr_receive_callback_t isr_rx_callback;
    // slip administration
    command_buf_context_t *packets;
    command_buf_t *current_tx_buf;

    BaseType_t RxInterrupt();
    QueueHandle_t rx_bufs;
public:
    static uint8_t DmaUartInterrupt(void *context);
    bool txDebug;

    DmaUART(void *registers, int irq, void *sem, command_buf_context_t *pkts)
    {
        uart = (volatile dma_uart_t *) registers;
        my_irq = irq;
        packets = pkts;
        rxSemaphore = sem;
        slipMode = false;
        txDebug = false;
        current_tx_buf = NULL;
        uint16_t rate = (((uint16_t)uart->rate_h) << 8) | uart->rate_l;
        uart->flowctrl = DMAUART_RESET;
        // printf("DmaUART initialized. Start rate: %d bps\n", CLOCK_FREQ / (rate + 1));
        uart->flowctrl = DMAUART_RESET;
        rx_bufs = xQueueCreate(16, sizeof(command_buf_t *));
        ResetReceiveCallback();
        install_high_irq(irq, DmaUART :: DmaUartInterrupt, this);
    }

    void SoftReset(void) {
        uart->flowctrl = DMAUART_RESET;
        uart->status = DMAUART_CLR_OVERFLOW;
    }
    void ModuleCtrl(uint8_t mode);
    void EnableSlip(bool enabled);
    void EnableLoopback(bool enable);
    void FlowControl(bool enable);
    void EnableIRQ(bool);
    void ClearRxBuffer(void);
    void SetBaudRate(int bps);
    void SetReceiveCallback(isr_receive_callback_t cb);
    void ResetReceiveCallback(void);

    void PrintStatus(void) {
        printf("UART status: %b\n", uart->status);
    }

//    void ReEnableBufferIRQ(void);

    // never blocking
    int Read(uint8_t *buffer, int bufferSize);

    int GetRxCount();
    BaseType_t SendSlipPacket(const uint8_t *buffer, int length);
    BaseType_t TransmitPacket(command_buf_t *buf, uint16_t *ms = NULL);
    BaseType_t ReceivePacket(command_buf_t **buf, TickType_t ticks);
    BaseType_t FreeBuffer(command_buf_t *buf);
    BaseType_t GetBuffer(command_buf_t **buf, TickType_t ticks);
};

#endif /* DMAUART_H_ */
