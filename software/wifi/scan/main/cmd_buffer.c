/*
 * cmd_buffer.c
 *
 *  Created on: Oct 23, 2021
 *      Author: gideon
 */

#include "cmd_buffer.h"

#define INL inline __attribute__((always_inline))

void cmd_buffer_init(command_buf_context_t *context)
{
    context->freeQueue     = xQueueCreate(NUM_BUFFERS, sizeof(command_buf_t*));
    context->transmitQueue = xQueueCreate(NUM_BUFFERS, sizeof(command_buf_t*));
    context->receivedQueue = xQueueCreate(NUM_BUFFERS, sizeof(command_buf_t*));

    for(int i=0; i<NUM_BUFFERS; i++) {
        command_buf_t *buf = &(context->bufs[i]);
        xQueueSend(context->freeQueue, &buf, 0);
        context->bufs[i].bufnr = i;
        context->bufs[i].size = 0;
        context->bufs[i].dropped = 0;
        context->bufs[i].slipEscape = 0;
        context->bufs[i].object = NULL;
    }
}

// Application side function calls
// when data has been processed, it the buffer is returned to the free queue
INL BaseType_t cmd_buffer_free(command_buf_context_t *context, command_buf_t *b)
{
    return xQueueSend(context->freeQueue, &b, 0);
}

// application needs a buffer to write its data to, it takes it from the free queue
INL BaseType_t cmd_buffer_get(command_buf_context_t *context, command_buf_t **b, TickType_t t) {
    return xQueueReceive(context->freeQueue, b, t);
}

// application has written data into transmit buffer and presents it to the link. Should be followed by enabling TX interrupt
INL BaseType_t cmd_buffer_transmit(command_buf_context_t *context, command_buf_t *b) {
    return xQueueSend(context->transmitQueue, &b, 0);
}

// application waits for command
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
    return xQueueSendFromISR(context->freeQueue, &b, w);
}

// when the ISR currently does not have a buffer to write its data to, it takes one
INL BaseType_t cmd_buffer_get_free_isr(command_buf_context_t *context, command_buf_t **b, BaseType_t *w) {
    return xQueueReceiveFromISR(context->freeQueue, b, w);
}

// when the ISR recognizes the end of a packet, it pushes the buffer into the received queue
INL BaseType_t cmd_buffer_received_isr(command_buf_context_t *context, command_buf_t *b, BaseType_t *w) {
    return xQueueSendFromISR(context->receivedQueue, &b, w);
}

