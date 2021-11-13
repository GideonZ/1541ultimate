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
#include "dump_hex.h"
#include <stdio.h>
#include <string.h>

#define FUART_Overflow    0x01
#define FUART_ClearToSend 0x02
#define FUART_TxFifoSpace 0x04 // Almost empty (few bytes in the fifo)
#define FUART_TxFifoFull  0x08
#define FUART_RxInterrupt 0x10
#define FUART_RxFifoFull  0x20
#define FUART_RxTimeout   0x40
#define FUART_RxDataAv    0x80

#define FUART_RxIRQ_EN    0x01
#define FUART_TxIRQ_EN    0x02
#define FUART_TxFIFO_SIZE 255

int FastUART::Read(uint8_t *buffer, int bufferSize)
{
    return ReadImpl(&stdRx, buffer, bufferSize);
}

int FastUART::ReadImpl(rxBuffer_t *b, uint8_t *buffer, int bufferSize)
{
    int ret = 0;
    while ((b->rxTail != b->rxHead) && (ret < bufferSize)) {
        *(buffer++) = b->rxBuffer[b->rxTail++];
        ret++;
        b->rxTail &= (RX_BUFFER_SIZE-1);
    }
    return ret;
}

void FastUART::EnableIRQ(bool enable)
{
    if (enable) {
        uart->ictrl |= FUART_RxIRQ_EN;
    } else {
        uart->ictrl = 0;
    }
}

void FastUART::ClearRxBuffer(void)
{
    stdRx.rxHead = 0;
    stdRx.rxTail = 0;
}

void FastUART::SetBaudRate(int bps)
{
	int twice = (CLOCK_FREQ * 2) / bps;
	uint16_t rate = ((twice + 1) >> 1) - 1;
	printf("SetBaudRate to %d bps => %d\n", bps, rate);
	uart->rate_h = rate >> 8;
	uart->rate_l = rate & 0xFF;
}

/*
void FastUART::FastUartRxInterrupt(void *context)
{
    FastUART *u = (FastUART *) context;
    BaseType_t woken = u->RxInterrupt();
    if (woken == pdTRUE) {
        vTaskSwitchContext(); // TODO: This should not be here, but right now, the UART is the only client on this IRQ number
    }
}
*/

/*
BaseType_t FastUART::RxInterrupt()
{
    slipElement_t slipElement;
    BaseType_t woken = pdFALSE;
    bool giveSem = false;
    bool store;

    if (uart->flags & FUART_RxInterrupt) {
        int rx_bytes = (int)uart->rx_count;
        while(rx_bytes > 0) {
            // try allocating space in buffer
            uint8_t data = uart->data; // possibly drop the char, if our buffer is full.. otherwise we keep getting interrupts
            rx_bytes--;

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
                int next = (b->rxHead + 1) & (RX_BUFFER_SIZE-1);
                if (next != b->rxTail) {
                    b->rxBuffer[b->rxHead] = data;
                    b->rxHead = next;
                } else if (slipMode) {
                    //slipError = true;
                }
            }
        }
    }
    if (giveSem) {
        xSemaphoreGiveFromISR(rxSemaphore, &woken);
    }
    return woken;
}
*/

void FastUART::FastUartInterrupt(void *context)
{
    FastUART *u = (FastUART *) context;
    uint8_t uart_intr_status = 0;
    command_buf_t *buf;
    uint8_t store, data;
    int rx_fifo_len;

    BaseType_t HPTaskAwoken = pdFALSE;

    while (1) {
        // The `continue statement` may cause the interrupt to loop infinitely
        // we exit the interrupt here
        uart_intr_status = u->uart->flags & (FUART_TxFifoSpace | FUART_RxInterrupt);
        //Exit form while loop
        if (uart_intr_status == 0) {
            break;
        }
        if (uart_intr_status & FUART_TxFifoSpace) {
            // turn off tx interrupt temporarily (no spinlock needed, as interrupts are not interrupted, and there is just one CPU core)
            u->uart->ictrl &= ~FUART_TxIRQ_EN;

            if (! u->current_tx_buf) { // no buffer available; let's try to get one
                if (cmd_buffer_get_tx_isr(u->packets, &(u->current_tx_buf), &HPTaskAwoken) == pdTRUE) {
                    u->current_tx_pnt = -1; // first transmit 0xC0
                }
            }
            if (u->current_tx_buf) { // buffer available!
                // We KNOW that there is more than one char available, otherwise the interrupt would not have triggered,
                // so we can safely send one char before asking how much space is available in the FIFO.
                if (u->current_tx_pnt < 0) {
                    u->uart->data = 0xC0;
                    u->current_tx_pnt = 0;
                }
                // check how much space there is in the TXFIFO, and copy at most this number of bytes into it
                // from the current buffer, encode it using the SLIP protocol on the fly.
                uint16_t fill_len = FUART_TxFIFO_SIZE - u->uart->tx_count;
                int remain = u->current_tx_buf->size - u->current_tx_pnt;
                uint8_t *pnt = u->current_tx_buf->data + u->current_tx_pnt;

                while ((remain > 0) && (fill_len > 2)) { // at least 3 chars will fit (one escaped char, data + end char)
                    if (*pnt == 0xC0) {
                        u->uart->data = 0xDB;
                        u->uart->data = 0xDC;
                        fill_len-=2;
                    } else if (*pnt == 0xDB) {
                        u->uart->data = 0xDB;
                        u->uart->data = 0xDD;
                        fill_len-=2;
                    } else {
                        u->uart->data = *pnt;
                        fill_len--;
                    }
                    pnt++;
                    remain--;
                }
                u->current_tx_pnt = u->current_tx_buf->size - remain;
                if (remain == 0) {
                    u->uart->data = 0xC0;  // close!
                    cmd_buffer_free_isr(u->packets, u->current_tx_buf, &HPTaskAwoken); // free buffer!
                    u->current_tx_buf = NULL; // currently not transmitting
                }
                u->uart->ictrl |= FUART_TxIRQ_EN;
            }
        } // end of transmit code


        if (uart_intr_status & FUART_RxInterrupt) {
            // Turn off Interrupts temporarily
            u->uart->ictrl &= ~FUART_RxIRQ_EN;
            // Acknowledge the interrupt
            u->uart->flags = FUART_RxInterrupt;

            if (( !u->current_rx_buf) && (u->slipMode)) { // no buffer available; let's try to get one
                if (cmd_buffer_get_free_isr(u->packets, &(u->current_rx_buf), &HPTaskAwoken) == pdTRUE) {
                    u->current_rx_buf->size = 0; // reset pointer
                    u->current_rx_buf->slipEscape = 0;
                }
            }

            if (u->current_rx_buf) { // Buffer available
                // Let's see how many bytes there are in the FIFO
                rx_fifo_len = (int)u->uart->rx_count;
                buf = u->current_rx_buf;

                // Copy data into buffer, decoding SLIP packet on the fly
                while(rx_fifo_len > 0) {
                    data = u->uart->data;
                    store = 1;
                    switch (data) {
                    case 0xC0:
                        buf->slipEscape = 0;
                        store = 0;
                        if (buf->size) {
                            if (buf->object) { // Is there a dispatcher associated with the buffer? Use it.
                                cmd_buffer_received_isr(u->packets, buf, &HPTaskAwoken);
                            }
                            u->current_rx_buf = NULL;
                            rx_fifo_len = 0;
                            buf = NULL;
                            // let the next IRQ get a new buffer.
                        }
                        break;

                    case 0xDB:
                        store = 0;
                        buf->slipEscape = 1;
                        break;

                    case 0xDC:
                        if (buf->slipEscape) {
                            data = 0xC0;
                        }
                        buf->slipEscape = 0;
                        break;

                    case 0xDD:
                        if (buf->slipEscape) {
                            data = 0xDB;
                        }
                        buf->slipEscape = 0;
                        break;

                    default:
                        buf->slipEscape = 0;
                        break;
                    }

                    if (store && buf) {
                        if (buf->size < CMD_BUF_SIZE) {
                            buf->data[buf->size++] = data;
                        } else {
                            buf->dropped++;
                        }
                    }
                    rx_fifo_len--;
                }
                // Enable the receive interrupt again
                u->uart->ictrl |= FUART_RxIRQ_EN;
            } else {
                rxBuffer_t* b = &u->stdRx;
                int next = (b->rxHead + 1) & (RX_BUFFER_SIZE-1);
                if (next != b->rxTail) {
                    b->rxBuffer[b->rxHead] = data;
                    b->rxHead = next;
                }
            }

        } // end of receive code
    }
    if (HPTaskAwoken == pdTRUE) {
        vTaskSwitchContext(); // TODO: This should not be here, but right now, the UART is the only client on this IRQ number
    }
}

BaseType_t FastUART :: SendSlipPacket(const uint8_t *data, int len)
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

BaseType_t FastUART :: TransmitPacket(command_buf_t *buf)
{
    if (cmd_buffer_transmit(packets, buf) == pdTRUE) {
        printf("Transmit packet:\n");
        dump_hex_relative(buf->data, buf->size);

        // Now enable the interrupt, so that the packet is going to be transmitted
        uart->ictrl |= FUART_TxIRQ_EN;
        return pdTRUE;
    }

    printf("Transmit packet failed; transmit queue full\n");
    cmd_buffer_free(packets, buf);
    return pdFALSE;
}

BaseType_t FastUART :: ReceivePacket(command_buf_t **buf, TickType_t ticks)
{
    return cmd_buffer_received(packets, buf, ticks);
}

BaseType_t FastUART :: FreeBuffer(command_buf_t *buf)
{
    BaseType_t ret = cmd_buffer_free(packets, buf);

    // Enable Rx Interrupt
    uart->ictrl |= FUART_RxIRQ_EN;

    return ret;
}

BaseType_t FastUART :: GetBuffer(command_buf_t **buf, TickType_t ticks)
{
    BaseType_t ret = cmd_buffer_get(packets, buf, ticks);
    // Initialize
    if (ret == pdTRUE) {
        (*buf)->size = 0;
        (*buf)->dropped = 0;
    }
    return ret;
}

