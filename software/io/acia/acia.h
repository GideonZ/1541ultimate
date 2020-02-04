#ifndef ACIA_H
#define ACIA_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

typedef struct _acia_t {
    uint8_t rx_head;
    uint8_t rx_tail;
    uint8_t tx_head;
    uint8_t tx_tail;
    uint8_t control;
    uint8_t command;
    uint8_t status;
    uint8_t enable;
    uint8_t handsh;
    uint8_t irq_source;
    uint8_t slot_base;
} acia_t;

#define ACIA_TX_RAM_OFFSET 0x800
#define ACIA_RX_RAM_OFFSET 0xA00

#define ACIA_HANDSH_CTS 0x01
#define ACIA_HANDSH_RTS 0x02 // Read Only
#define ACIA_HANDSH_DSR 0x04
#define ACIA_HANDSH_DTR 0x08 // Read Only
#define ACIA_HANDSH_DCD 0x10

#define ACIA_IRQ_RX     0x02
#define ACIA_IRQ_TX     0x04
#define ACIA_IRQ_CTRL   0x08
#define ACIA_IRQ_HANDSH 0x10

typedef struct _AciaMessage_t
{
    uint8_t messageType;
    uint8_t smallValue;
    uint16_t dataLength;
} AciaMessage_t;

#define ACIA_MSG_CONTROL 1
#define ACIA_MSG_HANDSH  2
#define ACIA_MSG_TXDATA  3
#define ACIA_MSG_RXDATA  4
#define ACIA_MSG_DCD     5
#define ACIA_MSG_DSR     6
#define ACIA_MSG_CTS     7

class DataBuffer
{
    int size;
    uint8_t *data;
    int head;
    int tail;

    int AvailableSpace() {
        return size - AvailableData() - 1;
    }

public:
    DataBuffer(int size) {
        this->size = size;
        data = new uint8_t[size];
        head = 0;
        tail = 0;
    }
    virtual ~DataBuffer() {
        delete[] data;
    }

    int AvailableData() {
        int p = head - tail;
        if (p < 0) {
            p += size;
        }
        return p;
    }
    int Put(uint8_t *d, int length) {
        int available = AvailableSpace();
        if (length > available) {
            length = available;
        }
        if (head + length <= size) { // no wrap
            memcpy(data + head, d, length);
        } else { // wrap
            int now = size - head;
            memcpy(data + head, d, now);
            int remain = length - now;
            memcpy(data, d + now, remain);
        }

        int newHead = head + length;
        while (newHead >= size) {
            newHead -= size;
        }
        head = newHead;
        return length;
    }

    int Get(uint8_t *d, int length) {
        int available = AvailableData();
        if (length > available) {
            length = available;
        }
        if (tail + length <= size) { // no wrap
            memcpy(d, data + tail, length);
        } else { // wrap
            int now = size - tail;
            memcpy(d, data + tail, now);
            int remain = length - now;
            memcpy(d + now, data, remain);
        }

        AdvanceReadPointer(length);
        return length;
    }

    int AvailableContiguous() {
        if (head < tail) { // wrap
            return size - tail;
        }
        return head - tail;
    }

    uint8_t *GetReadPointer() {
        return &data[tail];
    }

    void AdvanceReadPointer(int length) {
        int newTail = tail + length;
        while (newTail >= size) {
            newTail -= size;
        }
        tail = newTail;
    }
};

class Acia
{
    volatile acia_t *regs;
    volatile uint8_t *tx_ram;
    volatile uint8_t *rx_ram;
    QueueHandle_t controlQueue;
    QueueHandle_t dataQueue;
    DataBuffer *buffer;
    static void TaskStart(void *a);
    void Task();
public:
    Acia(uint32_t base);
    virtual ~Acia() {}

    int  init(uint16_t base, bool useNMI, QueueHandle_t controlQueue, QueueHandle_t dataQueue, DataBuffer *buffer);
    void deinit(void);
    int  SendToRx(uint8_t *data, int length);
    void SetDCD(uint8_t value);
    void SetDSR(uint8_t value);
    void SetCTS(uint8_t value);

    // efficient transfers
    int      GetRxSpace(void);
    volatile uint8_t *GetRxPointer(void);
    void     AdvanceRx(int);

    uint8_t IrqHandler(void);
};

extern Acia acia;

#endif
