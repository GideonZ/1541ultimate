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
#include "itu.h"
#include <stdio.h>
#include <string.h>

#define FUART_Overflow    0x01
#define FUART_ClearToSend 0x02
#define FUART_TxInterrupt 0x04
#define FUART_TxFifoFull  0x08
#define FUART_RxInterrupt 0x10
#define FUART_RxFifoFull  0x20
#define FUART_RxTimeout   0x40
#define FUART_RxDataAv    0x80

#define FUART_RxIRQ_EN    0x01
#define FUART_TxIRQ_EN    0x02
#define FUART_TxFIFO_SIZE 255

#define FUART_HWFLOWCTRL  0x01
#define FUART_LOOPBACK    0x02


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

void FastUART::EnableLoopback(bool enable)
{
    if(enable) {
        uart->flowctrl |= FUART_LOOPBACK;
    } else {
        uart->flowctrl &= ~FUART_LOOPBACK;
    }
}

void FastUART::FlowControl(bool enable)
{
    if(enable) {
        uart->flowctrl |= FUART_HWFLOWCTRL;
    } else {
        uart->flowctrl &= ~FUART_HWFLOWCTRL;
    }
}

int FastUART::GetRxCount(void)
{
    int rx_fifo_len = (int)uart->rx_count;
    if (uart->flags & FUART_RxDataAv) {
        rx_fifo_len++; // This is a side effect of the fall through fifo; it has an extra register which is not counted.
    }
    return rx_fifo_len;
}

void FastUART::ClearRxBuffer(void)
{
    uart->ictrl = 0; // disable interrupts
    stdRx.rxHead = 0;
    stdRx.rxTail = 0;
    if (current_rx_buf) {
        FreeBuffer(current_rx_buf);
        current_rx_buf = NULL;
    }
    if (current_tx_buf) {
        FreeBuffer(current_tx_buf);
        current_tx_buf = NULL;
    }
    int a = 0;
    while(uart->flags & FUART_RxDataAv) {
        a += uart->data;
    }
    cmd_buffer_reset(packets);
}

void FastUART::PrintRxMessage(void)
{
    uint8_t buffertje[128];
    int rx, to = 5;
    do {
        rx = Read(buffertje, 120);
        buffertje[rx] = 0;
        if (rx) {
            printf((char *)buffertje);
            to = 5;
        } else {
            vTaskDelay(15);
            to --;
            if (to == 0) {
                break;
            }
        }
    } while(true);
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
void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
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
        uart_intr_status = u->uart->flags & (FUART_TxInterrupt | FUART_RxInterrupt);
        //Exit form while loop
        if (uart_intr_status == 0) {
            break;
        }
        if (uart_intr_status & FUART_TxInterrupt) {
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
            u->uart->flags = FUART_RxTimeout; // fifo full status cannot be acknowledged; only the timeout

            // Let's see how many bytes there are in the FIFO
            rx_fifo_len = (int)u->uart->rx_count;

            if (u->uart->flags & FUART_RxDataAv) {
                rx_fifo_len++; // This is a side effect of the fall through fifo; it has an extra register which is not counted.
            }

            // Copy data into buffer, decoding SLIP packet on the fly
            // Use the buffer that we already have open, if any
            buf = u->current_rx_buf;

            while(rx_fifo_len > 0) {
                data = u->uart->data;
                store = 1;

                if (data == 0xC0) {
                    store = 0;
                    if (!buf) {
                        if (cmd_buffer_get_free_isr(u->packets, &buf, &HPTaskAwoken) == pdTRUE) {
                            buf->size = 0; // reset pointer
                            buf->slipEscape = 0;
                            u->current_rx_buf = buf;
                        }
                    } else {
                        if (buf->size) {
                            u64_buffer_received_isr(u->packets, buf, &HPTaskAwoken);
                            u->current_rx_buf = NULL;
                            // rx_fifo_len = 0;
                            buf = NULL;
                            // let the next IRQ get a new buffer.
                        } else {
                            // Do nothing, apparently we were out of sync
                        }
                    }
                } else if(buf) {
                    switch(data) {
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

                    if (store) {
                        if (buf->size < CMD_BUF_SIZE) {
                            buf->data[buf->size++] = data;
                        } else {
                            buf->dropped++;
                        }
                    }
                } else { // No buffer, no start of buffer
                    rxBuffer_t* b = &u->stdRx;
                    int next = (b->rxHead + 1) & (RX_BUFFER_SIZE-1);
                    if (next != b->rxTail) {
                        b->rxBuffer[b->rxHead] = data;
                        b->rxHead = next;
                    }
                }
                rx_fifo_len--;
            }
            u->uart->ictrl |= FUART_RxIRQ_EN;
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

BaseType_t FastUART :: TransmitPacket(command_buf_t *buf, uint16_t *ms)
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

