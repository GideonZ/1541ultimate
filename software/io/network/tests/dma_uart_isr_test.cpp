// The receive interrupt of the real io/uart/dma_uart.cc and the real io/uart/cmd_buffer.c on the
// build host. The Makefile builds a copy of dma_uart.h whose register block is replaced by the proxy
// fields of uart_host/dma_uart_regs.h, which talk to a model of uart_dma.vhd (DmaUartHw): the status
// register and the interrupt control register are one byte, and `length` is the TX length when
// written and the RX length when read, so a struct in RAM could not stand in for them. The tests script
// the hardware (a frame of N bytes arrives, the overflow bit is set) and run DmaUartInterrupt() on it.
// FreeRTOS is the FIFOs below, and the console is a string.
//
// The suite is meant to be extended: later changes to the ISR (a TX-done wake, an RX reserve) add
// cases here.

#include <deque>
#include <string>
#include <vector>
#include <string.h>
#include "../../usb/tests/host_test/host_test.h"
#include "dma_uart.h" // the generated copy, with the register proxies
#include "net_counters.h"

// ---- FreeRTOS queues: FIFOs of items, never blocking -------------------------------------
namespace {
struct Fifo {
    size_t item;
    size_t cap;
    std::deque<std::vector<uint8_t> > q;
};
} // namespace

extern "C" {
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size)
{
    Fifo *f = new Fifo;
    f->item = size;
    f->cap = len;
    return f;
}
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t) { ((Fifo *)q)->q.clear(); return pdPASS; }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t)
{
    Fifo *f = (Fifo *)q;
    if (f->q.size() >= f->cap) {
        return pdFALSE;
    }
    f->q.push_back(std::vector<uint8_t>((const uint8_t *)item, (const uint8_t *)item + f->item));
    return pdTRUE;
}
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *) { return xQueueSend(q, item, 0); }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t)
{
    Fifo *f = (Fifo *)q;
    if (f->q.empty()) {
        return pdFALSE;
    }
    memcpy(item, f->q.front().data(), f->item);
    f->q.pop_front();
    return pdTRUE;
}
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *) { return xQueueReceive(q, item, 0); }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return ((Fifo *)q)->q.size(); }

// ---- itu.h: the console bytes and the interrupt install -----------------------------------
}
namespace {
std::string console;
}
extern "C" {
void ioWrite8(volatile uint8_t *reg, uint8_t value)
{
    if (reg == UART_DATA) {
        console += (char)value;
    }
}
uint8_t ioRead8(volatile uint8_t *) { return 0; }
void install_high_irq(int, irq_function_t, void *) { }
}
void dump_hex_relative(const void *, int) { } // C++ linkage, as in system/dump_hex.h

namespace {

int delivered;                 // frames the receive callback was given
std::vector<command_buf_t *> delivered_bufs;
bool callback_refuses;         // the callback cannot queue the frame

BaseType_t counting_callback(command_buf_context_t *context, command_buf_t *b, BaseType_t *w)
{
    delivered++;
    delivered_bufs.push_back(b);
    if (callback_refuses) {
        return pdFALSE;
    }
    return cmd_buffer_received_isr(context, b, w);
}

// The DmaUART as the firmware has it, on the model: interrupts enabled, and the ISR run to give
// the receiver its first buffer
struct Rig {
    command_buf_context_t ctx;
    dma_uart_t regs;
    DmaUART *uart;

    Rig()
    {
        dmauart_hw().reset_all();
        memset(&net_counters, 0, sizeof(net_counters));
        console.clear();
        delivered = 0;
        delivered_bufs.clear();
        callback_refuses = false;
        memset(&ctx, 0, sizeof(ctx));
        cmd_buffer_init(&ctx);
        uart = new DmaUART(&regs, 3, NULL, &ctx);
        uart->SetReceiveCallback(counting_callback);
        uart->EnableIRQ(true);
        isr();
    }
    ~Rig() { delete uart; }

    void isr() { DmaUART::DmaUartInterrupt(uart); }

    // A frame arrives into the armed buffer, then the interrupt runs
    void frame(size_t len, uint8_t fill = 0x00)
    {
        ASSERT_TRUE(dmauart_hw().receive(len, fill));
        isr();
    }

    int rx_index(const command_buf_t *b) const { return (int)(b - ctx.rx_bufs); }
    size_t queued(QueueHandle_t q) const { return uxQueueMessagesWaiting(q); }
};

// How many times the interrupt control register was written with the "clear overflow" bit and
// without the set bit
int clear_overflow_writes()
{
    int n = 0;
    for (size_t i = 0; i < dmauart_hw().ictrl_writes.size(); i++) {
        uint8_t v = dmauart_hw().ictrl_writes[i];
        if ((v & 0x08) && !(v & 0x80)) {
            n++;
        }
    }
    return n;
}

} // namespace

TEST(DmaUartIsr, a_frame_is_delivered_with_its_length)
{
    Rig r;
    r.frame(100, 0xA5);
    EXPECT_EQ(delivered, 1);
    ASSERT_EQ(delivered_bufs.size(), (size_t)1);
    EXPECT_EQ(delivered_bufs[0]->size, 100);
    EXPECT_EQ(delivered_bufs[0]->data[99], (uint8_t)0xA5);
    EXPECT_EQ(r.queued(r.ctx.receivedQueue), (size_t)1);
    EXPECT_EQ(net_counters.uart_rx_long, (uint32_t)0);
    EXPECT_EQ(net_counters.uart_rx_overflow, (uint32_t)0);
    EXPECT_EQ(console, std::string()); // no console byte per frame
}

TEST(DmaUartIsr, the_next_buffer_is_armed_after_a_frame)
{
    Rig r;
    r.frame(100);
    EXPECT_TRUE(dmauart_hw().rx_addr_valid);
    EXPECT_TRUE(dmauart_hw().rx_addr == r.ctx.rx_bufs[1].data);
}

// A frame of exactly the buffer size fits
TEST(DmaUartIsr, a_frame_of_the_buffer_size_is_not_long)
{
    Rig r;
    r.frame(CMD_BUF_SIZE, 0x5A);
    EXPECT_EQ(net_counters.uart_rx_long, (uint32_t)0);
    EXPECT_EQ(delivered, 1);
}

// One byte over: the DMA has run past the buffer. N1: an END-less stream, which the receiver
// cannot cap, is what a module's self-reset boot log looks like at the host's baud rate
TEST(DmaUartIsr, a_frame_over_the_buffer_size_is_counted)
{
    Rig r;
    r.frame(CMD_BUF_SIZE + 1);
    EXPECT_EQ(net_counters.uart_rx_long, (uint32_t)1);
    r.frame(CMD_BUF_SIZE + 400);
    EXPECT_EQ(net_counters.uart_rx_long, (uint32_t)2);
}

// The overflow bit: counted once, cleared with a write that does not touch the enables
TEST(DmaUartIsr, the_overflow_bit_is_counted_and_cleared)
{
    Rig r;
    dmauart_hw().set_overflow();
    size_t writes = dmauart_hw().ictrl_writes.size();
    r.frame(60);
    EXPECT_EQ(net_counters.uart_rx_overflow, (uint32_t)1);
    EXPECT_EQ(clear_overflow_writes(), 1);
    EXPECT_FALSE(dmauart_hw().overflow);
    // it is bit 3 alone: bit 7 (set) is 0, and bits 0-2 are 0, so no enable changes
    bool found = false;
    for (size_t i = writes; i < dmauart_hw().ictrl_writes.size(); i++) {
        if (dmauart_hw().ictrl_writes[i] == 0x08) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_TRUE(dmauart_hw().rx_irq_en);
    EXPECT_TRUE(dmauart_hw().buf_irq_en);
    EXPECT_EQ(delivered, 1); // the frame itself is unaffected
}

TEST(DmaUartIsr, no_overflow_bit_no_count_and_no_clear_write)
{
    Rig r;
    r.frame(60);
    r.frame(61);
    EXPECT_EQ(net_counters.uart_rx_overflow, (uint32_t)0);
    EXPECT_EQ(clear_overflow_writes(), 0);
}

// The existing starvation signal: no free RX buffer. One '^' and the buffer request interrupt is
// disabled until a buffer is freed again (not changed by the counters)
TEST(DmaUartIsr, starvation_writes_a_caret_and_disables_the_buffer_request)
{
    Rig r;
    command_buf_t *b;
    while (xQueueReceive(r.ctx.freeRxQueue, &b, 0) == pdTRUE) {
    }
    r.frame(100); // delivered; the next buffer cannot be armed
    EXPECT_EQ(console, std::string("^"));
    EXPECT_FALSE(dmauart_hw().buf_irq_en);
    r.isr();
    EXPECT_EQ(console, std::string("^")); // not repeated: the request is off
}
