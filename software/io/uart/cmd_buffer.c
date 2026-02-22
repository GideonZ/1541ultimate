/*
 * cmd_buffer.c
 *
 *  Created on: Oct 23, 2021
 *      Author: gideon
 */

#include "cmd_buffer.h"
#include <string.h>

#define IS_RX_BUFFER 0x40

void cmd_buffer_init(command_buf_context_t *context)
{
    context->freeRxQueue   = xQueueCreate(NUM_RX_BUFFERS, sizeof(command_buf_t*));
    context->freeTxQueue   = xQueueCreate(NUM_TX_BUFFERS, sizeof(command_buf_t*));
    context->receivedQueue = xQueueCreate(NUM_RX_BUFFERS, sizeof(command_buf_t*));
    context->transmitQueue = xQueueCreate(NUM_TX_BUFFERS, sizeof(command_buf_t*));

    cmd_buffer_reset(context);
}

void cmd_buffer_reset(command_buf_context_t *context)
{
    xQueueGenericReset(context->freeRxQueue, pdFALSE);
    xQueueGenericReset(context->freeTxQueue, pdFALSE);
    xQueueGenericReset(context->transmitQueue, pdFALSE);
    xQueueGenericReset(context->receivedQueue, pdFALSE);

    for(int i=0; i<NUM_TX_BUFFERS; i++) {
        command_buf_t *buf = &(context->tx_bufs[i]);
        memset(buf->data, 0x44, 8);
        xQueueSend(context->freeTxQueue, &buf, 0);
        buf->bufnr = i;
        buf->size = 0;
        buf->dropped = 0;
        buf->slipEscape = 0;
        buf->object = NULL;
    }

    for(int i=0; i<NUM_RX_BUFFERS; i++) {
        command_buf_t *buf = &(context->rx_bufs[i]);
        memset(buf->data, 0x55, 8);
        xQueueSend(context->freeRxQueue, &buf, 0);
        buf->bufnr = i | IS_RX_BUFFER;
        buf->size = 0;
        buf->dropped = 0;
        buf->slipEscape = 0;
        buf->object = NULL;
    }
}

// Application side function calls
// when data has been processed, it the buffer is returned to the free queue
INL BaseType_t cmd_buffer_free(command_buf_context_t *context, command_buf_t *b)
{
    if(b->bufnr & IS_RX_BUFFER) {
        return xQueueSend(context->freeRxQueue, &b, 0);
    }
    return xQueueSend(context->freeTxQueue, &b, 0);
}

// application needs a buffer to write its data to, it takes it from the free queue
INL BaseType_t cmd_buffer_get(command_buf_context_t *context, command_buf_t **b, TickType_t t) {
    return xQueueReceive(context->freeTxQueue, b, t);
}

// application has written data into transmit buffer and presents it to the link. Should be followed by enabling TX interrupt
INL BaseType_t cmd_buffer_transmit(command_buf_context_t *context, command_buf_t *b) {
    return xQueueSend(context->transmitQueue, &b, 200); // one second
}

// application sends a message to itself, e.g. from one thread to be processed by another
INL BaseType_t cmd_buffer_loopback(command_buf_context_t *context, command_buf_t *b) {
    return xQueueSend(context->receivedQueue, &b, 100);
}

// slave application waits for command, or master application reads the response
INL BaseType_t cmd_buffer_received(command_buf_context_t *context, command_buf_t **b, TickType_t t) {
    return xQueueReceive(context->receivedQueue, b, t);
}

// Functions that are needed in the interrupt service routine
// when interrupt routine finishes transmitting a buffer, it can check for the next one
INL BaseType_t cmd_buffer_get_tx_isr(command_buf_context_t *context, command_buf_t **b, BaseType_t *w) {
    return xQueueReceiveFromISR(context->transmitQueue, b, w);
}

// free when a transmit buffer has been transferred to the TxFIFO completely
INL BaseType_t cmd_buffer_free_isr(command_buf_context_t *context, command_buf_t *b, BaseType_t *w) {
    return xQueueSendFromISR(context->freeTxQueue, &b, w);
}

// when the ISR currently does not have a buffer to write its data to, it takes one
INL BaseType_t cmd_buffer_get_free_isr(command_buf_context_t *context, command_buf_t **b, BaseType_t *w) {
    return xQueueReceiveFromISR(context->freeRxQueue, b, w);
}

// when the ISR recognizes the end of a packet, it pushes the buffer into the received queue
INL BaseType_t cmd_buffer_received_isr(command_buf_context_t *context, command_buf_t *b, BaseType_t *w) {
    return xQueueSendFromISR(context->receivedQueue, &b, w);
}
