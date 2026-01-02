/*
 * fastuart.c
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#include "dma_uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "dump_hex.h"
#include "itu.h"
#include <stdio.h>
#include <string.h>

#define DMAUART_RxInterrupt  0x01
#define DMAUART_TxInterrupt  0x02
#define DMAUART_BufInterrupt 0x04
#define DMAUART_Overflow     0x08
#define DMAUART_CtsDetect    0x10
#define DMAUART_RxValid      0x20
#define DMAUART_TxReady      0x40
#define DMAUART_RxNeedAddr   0x80

#define DMAUART_RxIRQ_EN    0x81
#define DMAUART_TxIRQ_EN    0x82
#define DMAUART_BufReq_EN   0x84
#define DMAUART_RxIRQ_DIS   0x01
#define DMAUART_TxIRQ_DIS   0x02
#define DMAUART_BufReq_DIS  0x04

/*
 * DMA Uart is interrupt driven. The software turns the interrupts on and off as needed.
 * The following cases are considered:
 * Application wants to send a packet. Two cases:
 * 1) Uart is not busy. Application can initiate the transmission right away and places
 *    the packet into a queue and enables the Tx interrupt. When the interrupt occurs,
 *    the queue is read and the packet is freed. Then, when the queue is empty, the
 *    interrupt gets disabled.
 * 2) Uart is busy. Application appends the packet to the packets to be transmitted and
 *    enables the transmit interrupt. As soon as the transmitter is ready with the current
 *    packet, the next packet is transmitted in the interrupt. Note that enabling the
 *    interrupt should be an atomic operation. This is achieved by having set and clear
 *    operations on the IO register, instead of RMW. It is important that the packet is
 *    first placed into the queue and then the IRQ is enabled. If the IRQ occurs before
 *    the transmit routine enables it, and the current packet is already transmitted
 *    as it is already in the queue. The interrupt will simply occur (again) after it
 *    has been transmitted, which will be the same as case 1).
 *
 * When the transmit queue is full, the transmitting thread is halted automatically. If freeing the packet cannot be
 * done inside of the interrupt, it can be attached to a 'to be freed' queue, which will wake up the clean up thread.
 *
 * Receiving should be enabled every time that the application is ready to receive. This can be obtained by getting a
 * free buffer from a queue and writing the address of the to be received packet in the registers, and enabling the
 * interrupt. (The first time, this has to be done by the application...) When the interrupt occurs, it is placed in the
 * queue with received packets. A new packet is attempted to be taken from the free queue to re-trigger the receiver. If
 * the queue with free items is empty, the interrupt cannot solve this and the IRQ does not get enabled. Also, if the
 * receive queue is full, the packet cannot be processed and should be appended to the free queue again, otherwise it
 * gets lost. As said, when the free queue is empty, the IRQ cannot solve it. However, the application can. If the IRQ
 * occurs on two conditions: Either the receiver is not triggered (no buffer address written), and a packet has been
 * received. If the IRQ routine can discern the cause, the only thing the application needs to do when a packet is
 * freed, is enable the RxIRQ. The interrupt then occurs, it will pick up the free packet from the queue and program
 * the address registers.
 *
 */

void DmaUART::EnableIRQ(bool enable)
{
    printf("Uart IRQ %sable!\n", enable ? "en" : "dis");
    if (enable) {
        uart->ictrl = ( DMAUART_RxIRQ_EN | DMAUART_BufReq_EN );
        ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) | (1 << my_irq));
    } else {
        uart->ictrl = ( DMAUART_RxIRQ_DIS | DMAUART_BufReq_DIS );
    }
}

void DmaUART::EnableLoopback(bool enable)
{
    if(enable) {
        uart->flowctrl |= DMAUART_LOOPBACK;
    } else {
        uart->flowctrl &= ~DMAUART_LOOPBACK;
    }
}

void DmaUART::FlowControl(bool enable)
{
    if(enable) {
        uart->flowctrl |= DMAUART_HWFLOWCTRL;
    } else {
        uart->flowctrl &= ~DMAUART_HWFLOWCTRL;
    }
}

void DmaUART::EnableSlip(bool enable)
{
    slipMode = enable;
    if(enable) {
        uart->flowctrl |= DMAUART_SLIPENABLE;
    } else {
        uart->flowctrl &= ~DMAUART_SLIPENABLE;
    }
}

void DmaUART::ModuleCtrl(uint8_t mode)
{
    printf("Setting module control to (%b) %d ", uart->flowctrl, mode);
    uart->flowctrl = (uart->flowctrl & 0x8F) | (mode << 4);
    printf("%b\n", uart->flowctrl);
}

void DmaUART::ClearRxBuffer(void)
{
    // uart->ictrl = DMAUART_RxIRQ_DIS | DMAUART_TxIRQ_DIS |DMAUART_BufReq_DIS; // disable interrupts
    uart->flowctrl = uart->flowctrl | DMAUART_RESET; // reset pulse; will also disable interrupts
    xQueueGenericReset(rx_bufs, pdFALSE);
    uart->flowctrl = uart->flowctrl | DMAUART_RESET; // reset pulse; will also disable interrupts
    cmd_buffer_reset(packets);
}

int DmaUART::Read(uint8_t *buffer, int bufsize)
{
    // wrapper around receive.. non blocking get packet
    command_buf_t *buf;
    if (ReceivePacket(&buf, 0) == pdTRUE) {
        // printf("\n\n\e[0m ** Read got packet %02x with Size = %d\n", buf->bufnr, buf->size);
        int max = buf->size > bufsize ? bufsize : buf->size;
        memcpy(buffer, buf->data, max);
        FreeBuffer(buf);
        return max;
    }
    return 0;
}

void DmaUART::SetBaudRate(int bps)
{
	int twice = (CLOCK_FREQ * 2) / bps;
	uint16_t rate = ((twice + 1) >> 1) - 1;
    uint32_t actual = (CLOCK_FREQ * 2) / (2 * (rate + 1));
	printf("SetBaudRate to %d bps => %d => actual = %d\n", bps, rate, actual);
	uart->rate_h = rate >> 8;
	uart->rate_l = rate & 0xFF;
}

void hex(uint8_t h);

uint8_t DmaUART::DmaUartInterrupt(void *context)
{
    DmaUART *u = (DmaUART *) context;
    uint8_t uart_intr_status = 0;

    BaseType_t HPTaskAwoken = pdFALSE;
    // ioWrite8(UART_DATA, '[');

    while (1) {
        // The `continue statement` may cause the interrupt to loop infinitely
        // we exit the interrupt here
        uart_intr_status = u->uart->status & (DMAUART_RxInterrupt | DMAUART_TxInterrupt | DMAUART_BufInterrupt);
        
        //Exit form while loop
        if (uart_intr_status == 0) {
            break;
        }

        // Let's see if we can enable the receiver.
        if (uart_intr_status & DMAUART_BufInterrupt) {
            command_buf_t *rxb = NULL;
            // UART DMA is not able to receive, because it is not loaded with an address, We need a buffer!
            if (cmd_buffer_get_free_isr(u->packets, &rxb, &HPTaskAwoken) == pdTRUE) {
                // receive here. Writing to these registers should trigger the receive unit
                xQueueSendFromISR(u->rx_bufs, &rxb, &HPTaskAwoken); // this has to be first, before writing rx_addr!
//                ioWrite8(UART_DATA, '~');
//                hex(rxb->bufnr);
                u->uart->rx_addr = rxb->data; // pbufs will be attached in the receive thread
                u->uart->ictrl = DMAUART_RxIRQ_EN;
            } else {
                ioWrite8(UART_DATA, '^');
                u->uart->ictrl = DMAUART_BufReq_DIS;
            }
        }

        if (uart_intr_status & DMAUART_TxInterrupt) {
            // ioWrite8(UART_DATA, 'T');

            if (u->current_tx_buf) { // done, free it
                cmd_buffer_free_isr(u->packets, u->current_tx_buf, &HPTaskAwoken);
                u->uart->ictrl = DMAUART_BufReq_EN;
                u->current_tx_buf = NULL;        
            } else if (cmd_buffer_get_tx_isr(u->packets, &(u->current_tx_buf), &HPTaskAwoken) == pdTRUE) {
                u->uart->tx_addr = u->current_tx_buf->data; // how about pbufs?
                u->uart->length  = (uint16_t)u->current_tx_buf->size;
                u->uart->tx_push = 1;
/*
                ioWrite8(UART_DATA, '>');
                uint16_t ti = getMsTimer();
                hex(ti >> 8);
                hex(ti & 0xFF);
                ioWrite8(UART_DATA, '\n');
*/
            } else { // nothing to send!
                u->uart->ictrl = DMAUART_TxIRQ_DIS;
            }
        } // end of transmit code
        
        // Receive code!
        if (uart_intr_status & DMAUART_RxInterrupt) {
            command_buf_t *rxb = NULL;

            if (xQueueReceiveFromISR(u->rx_bufs, &rxb, &HPTaskAwoken) == pdTRUE) {
                // hex(rxb->bufnr);
                rxb->size = u->uart->length;

                if (u->isr_rx_callback(u->packets, rxb, &HPTaskAwoken) != pdTRUE) {
                    // Packet could not be queued, so we need to drop it, But now it's free again
                    cmd_buffer_free_isr(u->packets, rxb, &HPTaskAwoken);
                    u->uart->ictrl = DMAUART_BufReq_EN;
                }
            } else {
                ioWrite8(UART_DATA, '!');
            }
            u->uart->rx_pop = 1;
        }
    }
    // ioWrite8(UART_DATA, ']');
    return HPTaskAwoken;
}

void DmaUART :: SetReceiveCallback(isr_receive_callback_t cb)
{
    isr_rx_callback = cb;
}

void DmaUART :: ResetReceiveCallback(void)
{
    isr_rx_callback = cmd_buffer_received_isr;
}


// Send routine with copy
BaseType_t DmaUART :: SendSlipPacket(const uint8_t *data, int len)
{
    if (len > CMD_BUF_SIZE) {
        return pdFALSE;
    }
    command_buf_t *buf;
    BaseType_t success = GetBuffer(&buf, 100);
    if (success == pdTRUE) {
        memcpy(buf->data, data, len);
        buf->size = len;
        success = TransmitPacket(buf);
    }
    return success;
}

// Send routine without copy
BaseType_t DmaUART :: TransmitPacket(command_buf_t *buf, uint16_t *ms)
{
    if (cmd_buffer_transmit(packets, buf) == pdTRUE) {
        if (txDebug) {
            printf("Transmit packet %d: (%d bytes)\n", buf->bufnr, buf->size);
            dump_hex_relative(buf->data, (buf->size < 64) ? buf->size : 64);
        }
        // Now enable the interrupt, so that the packet is going to be transmitted
        if (ms) {
            *ms = getMsTimer();
        }
        uart->ictrl = DMAUART_TxIRQ_EN;
        return pdTRUE;
    }

    printf("Transmit packet failed; transmit queue full\n");
    cmd_buffer_free(packets, buf);
    return pdFALSE;
}

BaseType_t DmaUART :: ReceivePacket(command_buf_t **buf, TickType_t ticks)
{
    return cmd_buffer_received(packets, buf, ticks);
}

BaseType_t DmaUART :: FreeBuffer(command_buf_t *buf)
{
    BaseType_t ret = cmd_buffer_free(packets, buf);

    // Enable Rx Buffer request Interrupt
    uart->ictrl = DMAUART_BufReq_EN;

    return ret;
}

BaseType_t DmaUART :: GetBuffer(command_buf_t **buf, TickType_t ticks)
{
    BaseType_t ret = cmd_buffer_get(packets, buf, ticks);
    // Initialize
    if (ret == pdTRUE) {
        (*buf)->size = 0;
        (*buf)->dropped = 0;
    }
    return ret;
}

// void DmaUART :: ReEnableBufferIRQ(void)
// {
//     uart->ictrl = DMAUART_BufReq_EN;
// }
