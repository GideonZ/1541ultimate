#include <stdio.h>
#include <string.h>
#include "acia.h"

#include "iomap.h"
#include "itu.h"

Acia :: Acia(uint32_t base)
{
    if (!(getFpgaCapabilities() & CAPAB_ACIA)) {
        printf("No ACIA found in the FPGA.\n");
        return;
    }
    regs = (volatile acia_t *)base;
    tx_ram = (volatile uint8_t *)(base + ACIA_TX_RAM_OFFSET);
    rx_ram = (volatile uint8_t *)(base + ACIA_RX_RAM_OFFSET);

    regs->slot_base = 0;
    regs->enable = 0;
    controlQueue = NULL;
    dataQueue = NULL;
    buffer = NULL;

    xTaskCreate( Acia :: TaskStart, "ACIA Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
}

void Acia :: TaskStart(void *a)
{
    Acia *obj = (Acia *)a;
    obj->Task();
}

void Acia :: Task()
{
    QueueHandle_t queue = xQueueCreate(16, sizeof(AciaMessage_t));
    DataBuffer *buf = new DataBuffer(2048); // 2K receive buffer
    AciaMessage_t message;
    char outbuf[32];
    uint8_t txbuf[32];
    int len;

    init(0xDE00, true, queue, queue, buf);
    const int baudRates[] = { -1, 100, 150, 220, 269, 300, 600, 1200, 2400, 3600, 4800, 7200, 9600, 14400, 19200, 38400 };

    while(1) {
        xQueueReceive(dataQueue, &message, portMAX_DELAY);
        printf("Message type: %d. Value: %b. DataLen: %d Buffer: %d\n", message.messageType, message.smallValue, message.dataLength, buffer->availableData());
        switch(message.messageType) {
        case ACIA_MSG_CONTROL:
            sprintf(outbuf, "BAUD=%d\r", baudRates[message.smallValue & 0x0F]);
            this->send((uint8_t *)outbuf, strlen(outbuf));
            break;
        case ACIA_MSG_HANDSH:
            sprintf(outbuf, "HANDSH=%b\r", message.smallValue);
            this->send((uint8_t *)outbuf, strlen(outbuf));
            break;
        case ACIA_MSG_TXDATA:
            len = buf->get(txbuf, 30);
            txbuf[len] = 0;
            sprintf(outbuf, "DATA=%s\r", txbuf);
            this->send((uint8_t *)outbuf, strlen(outbuf));
            break;
        }
    }
}

int Acia :: init(uint16_t base, bool useNMI, QueueHandle_t controlQueue, QueueHandle_t dataQueue, DataBuffer *buffer)
{
    if ((base < 0xDE00) || (base > 0xDFFC)) {
        return -1;
    }
    printf("Enabling ACIA at address %04x\n", base);

    base -= 0xDE00;
    base >>= 2;
    if (useNMI) {
        base |= 0x80;
    }
    regs->slot_base = (uint8_t)base;

    this->controlQueue = controlQueue;
    this->dataQueue = dataQueue;
    this->buffer = buffer;

    uint8_t enable = 1;
    if (controlQueue) {
        enable |= ACIA_IRQ_CTRL | ACIA_IRQ_HANDSH;
    }
    if (dataQueue) {
        enable |= ACIA_IRQ_TX;
    }
    regs->enable = enable;
    ioWrite8(ITU_IRQ_HIGH_EN, 1);

    return 0;
}

void Acia :: deinit(void)
{
    regs->enable = 0;
    this->controlQueue = NULL;
    this->dataQueue = NULL;
    if (buffer) {
        delete buffer;
        buffer = NULL;
    }
    ioWrite8(ITU_IRQ_HIGH_EN, 0);
}

int Acia :: send(uint8_t *data, int length)
{
    uint8_t head = regs->rx_head;
    uint8_t tail = regs->rx_tail;
    uint8_t space = (tail - head) - 1; // wrap around 8 bit size... Hack!
    if (length > space) {
        length = space;
    }
    memcpy((void *)(rx_ram + head), data, length);
    regs->rx_head = head + (uint8_t)length;
    return length;
}

uint8_t Acia :: IrqHandler(void)
{
    uint8_t source = regs->irq_source;
    uint8_t tx_head;
    uint32_t length;
    AciaMessage_t msg;
    BaseType_t retVal = pdFALSE;

    if (source & ACIA_IRQ_CTRL) { //
        //printf("ACIA_CONTROL: %b\n", regs->control);
        if (controlQueue) {
            msg.messageType = ACIA_MSG_CONTROL;
            msg.smallValue = regs->control;
            msg.dataLength = 0;
            xQueueSendFromISR(controlQueue, &msg, &retVal);
        }
        regs->irq_source = ACIA_IRQ_CTRL;
    }
    if (source & ACIA_IRQ_HANDSH) { //
        //printf("ACIA_HANDSH: %b\n", regs->handsh);
        if (controlQueue) {
            msg.messageType = ACIA_MSG_HANDSH;
            msg.smallValue = regs->handsh;
            msg.dataLength = 0;
            xQueueSendFromISR(controlQueue, &msg, &retVal);
        }
        regs->irq_source = ACIA_IRQ_HANDSH;
    }
    if (source & ACIA_IRQ_TX) {
        tx_head = regs->tx_head;
        length = tx_head - regs->tx_tail;
        if (dataQueue) {
            msg.messageType = ACIA_MSG_TXDATA;
            msg.smallValue = 0;
            msg.dataLength = buffer->put((uint8_t *)tx_ram + regs->tx_tail, (int)length);
            xQueueSendFromISR(dataQueue, &msg, &retVal);
        }
        //printf("ACIA MSG: %s\n", message);
        regs->tx_tail = tx_head;
    }
//    if (source & ACIA_IRQ_RX) {
//        printf("ACIA_RX\n");
//        regs->enable &= ~ACIA_IRQ_RX;
//    }
    // Let's turn it off for now
    // regs->enable &= ~source;
    return 0;
}

// Global Static
Acia acia(ACIA_BASE);

extern "C" {

uint8_t acia_irq(void)
{
    return acia.IrqHandler();
}

}
