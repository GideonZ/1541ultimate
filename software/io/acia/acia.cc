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

int Acia :: SendToRx(uint8_t *data, int length)
{
    uint8_t head = regs->rx_head;
    uint8_t tail = regs->rx_tail;
    uint8_t space = (tail - head) - 1; // wrap around 8 bit size... Hack!
    if (length > space) {
        length = space;
    }
    uint8_t *dest =  (uint8_t *)(rx_ram + head);
    memcpy(dest, data, length);
    regs->rx_head = head + (uint8_t)length;
    return length;
}

void Acia :: SetDCD(uint8_t value)
{
    if (value) {
        regs->handsh |= ACIA_HANDSH_DCD;
    } else {
        regs->handsh &= ~ACIA_HANDSH_DCD;
    }
}

void Acia :: SetDSR(uint8_t value)
{
    if (value) {
        regs->handsh |= ACIA_HANDSH_DSR;
    } else {
        regs->handsh &= ~ACIA_HANDSH_DSR;
    }
}

void Acia :: SetCTS(uint8_t value)
{
    if (value) {
        regs->handsh |= ACIA_HANDSH_CTS;
    } else {
        regs->handsh &= ~ACIA_HANDSH_CTS;
    }
}

uint8_t Acia :: IrqHandler(void)
{
    uint8_t source = regs->irq_source;
    uint8_t tx_head;
    uint8_t length;
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
            msg.dataLength = buffer->Put((uint8_t *)tx_ram + regs->tx_tail, (int)length);
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

int      Acia :: GetRxSpace(void)
{
    uint8_t head = regs->rx_head;
    uint8_t tail = regs->rx_tail;
    uint8_t space = (tail - head) - 1; // wrap around 8 bit size... Hack!
    return (int)space;
}

volatile uint8_t *Acia :: GetRxPointer(void)
{
    uint8_t head = regs->rx_head;
    return rx_ram + head;
}

void Acia :: AdvanceRx(int adv)
{
    regs->rx_head += adv;
}

// Global Static
Acia acia(ACIA_BASE);

extern "C" {

uint8_t acia_irq(void)
{
    return acia.IrqHandler();
}

}
