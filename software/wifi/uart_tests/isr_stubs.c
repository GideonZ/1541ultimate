/*
 * isr_stubs.c
 *
 * A UART for the modules' interrupt handler (isr_env.h): an RX FIFO that a case fills before each interrupt, the
 * pending interrupt bits, a TX FIFO with room for one small frame during isr_tx_interrupt() and none otherwise
 * (a UART that sends nothing until the case says so), and a count of what the handler writes to the UART0 console.
 * Included at the end of the cut-out handler (output/isr/<module>/isr_code.c), so that it reaches the handler's
 * types. Below: what the cases call (isr_test.cpp).
 */

#include "isr_test_api.h"

static my_uart_obj_t uart_obj;
static command_buf_context_t ctx;

static uint8_t fifo[256];
static int fifo_n, fifo_at;
static uint32_t pending;   // interrupt status of the enabled interrupts (int_st)
static uint32_t raw_only;  // raw status of an interrupt that is not enabled (RXFIFO_OVF before it is in the enable mask)
static uint32_t enabled = MODULE_INTR_ENABLE_MASK; // the interrupts enabled, as my_uart_init() enables them
// The TX FIFO. Default: room (64 bytes) only during isr_tx_interrupt(), which stands for "the UART has sent what it had";
// at other times the FIFO is full (a UART that sends nothing until the case says so). Model mode (isr_txm, txm 1): 128 bytes,
// room = 128 - used, drained by isr_uart_drain(); the TXFIFO_EMPTY interrupt is edge-like as a C64U showed: it is raised by a
// threshold crossing (used from above the threshold of 96 to 96 or below, with the interrupt enabled) or by enabling it from a
// task while the FIFO is below the threshold; enabling it from the handler raises nothing, and kicks that come before the
// interrupt is serviced merge into one (isr_service_tx).
static int txm, tx_room, tx_used, txe_pending, in_handler, tx_total;
#define TX_FIFO 128
#define TX_THR  96
int console_writes;

uint32_t isr_read_reg(uint32_t addr)
{
    if ((addr == UART_FIFO_REG(1)) && (fifo_at < fifo_n)) {
        return fifo[fifo_at++];
    }
    return 0;
}

void isr_write_reg(uint32_t addr, uint32_t value)
{
    if (addr == UART_FIFO_AHB_REG(0)) {
        console_writes++;
    } else if (addr == UART_FIFO_AHB_REG(1)) {
        tx_room--;
        tx_used++;
        tx_total++;
    }
}

uint32_t uart_hal_get_intsts_mask(uart_hal_context_t *hal) { return pending | (raw_only & enabled); }
uint32_t uart_hal_get_intraw_mask(uart_hal_context_t *hal) { return pending | raw_only; }
void uart_hal_disable_intr_mask(uart_hal_context_t *hal, uint32_t mask) { enabled &= ~mask; }
void uart_hal_ena_intr_mask(uart_hal_context_t *hal, uint32_t mask)
{
    if (txm && (mask & UART_INTR_TXFIFO_EMPTY) && !in_handler && tx_used <= TX_THR) {
        txe_pending = 1; // enabled from a task with the FIFO below the threshold: the interrupt is asserted
    }
    enabled |= mask;
}
void uart_hal_clr_intsts_mask(uart_hal_context_t *hal, uint32_t mask) { pending &= ~mask; raw_only &= ~mask; }
uint16_t uart_hal_get_txfifo_len(uart_hal_context_t *hal) { return (uint16_t)(txm ? TX_FIFO - tx_used : (tx_room > 0 ? tx_room : 0)); }
uint32_t uart_hal_get_rxfifo_len(uart_hal_context_t *hal) { return (uint32_t)(fifo_n - fifo_at); }
void uart_hal_rxfifo_rst(uart_hal_context_t *hal) { fifo_n = fifo_at = 0; }

// --- what the cases call --------------------------------------------------------------------

void isr_reset(void)
{
    cmd_buffer_init(&ctx);
    memset(&uart_obj, 0, sizeof(uart_obj));
    uart_obj.uart_num = UART_NUM_1;
    uart_obj.buffer_context = &ctx;
    p_uart_obj[UART_NUM_1] = &uart_obj;
    console_writes = 0;
    fifo_n = fifo_at = 0;
    pending = 0;
    raw_only = 0;
    enabled = MODULE_INTR_ENABLE_MASK;
    tx_room = 0;
    txm = 0;
    tx_used = 0;
    txe_pending = 0;
    in_handler = 0;
    tx_total = 0;
}

void isr_overflow(void)
{
    raw_only |= UART_INTR_RXFIFO_OVF;
}

void isr_interrupt(const uint8_t *bytes, int n, uint32_t extra)
{
    int left = fifo_n - fifo_at; // bytes the handler did not read stay in the FIFO, like the hardware's
    memmove(fifo, fifo + fifo_at, (size_t)left);
    memcpy(fifo + left, bytes, (size_t)n);
    fifo_n = left + n;
    fifo_at = 0;
    pending = (n ? UART_INTR_RXFIFO_FULL : 0) | extra;
    my_uart_intr_handler(&uart_obj);
}

// One TX interrupt: the UART has sent what it had, and there is room for one small frame
void isr_tx_interrupt(void)
{
    tx_room = 64;
    pending |= UART_INTR_TXFIFO_EMPTY;
    in_handler = 1;
    my_uart_intr_handler(&uart_obj);
    in_handler = 0;
    tx_room = 0;
}

// --- the model mode ---------------------------------------------------------------------------------------------
void isr_txm(int on) { txm = on; tx_used = 0; txe_pending = 0; tx_room = 0; }
// The interrupt, serviced once (what the CPU does when the line is asserted): one handler entry
void isr_service_tx(void)
{
    if (txe_pending && (enabled & UART_INTR_TXFIFO_EMPTY)) {
        txe_pending = 0;
        pending |= UART_INTR_TXFIFO_EMPTY;
        in_handler = 1;
        my_uart_intr_handler(&uart_obj);
        in_handler = 0;
    }
}
// The UART sends n bytes of its FIFO; crossing the threshold with the interrupt enabled raises it
void isr_uart_drain(int n)
{
    int before = tx_used;
    tx_used = (tx_used > n) ? tx_used - n : 0;
    if (before > TX_THR && tx_used <= TX_THR && (enabled & UART_INTR_TXFIFO_EMPTY)) {
        txe_pending = 1;
    }
}
int isr_tx_fifo_used(void) { return tx_used; }
int isr_tx_total(void) { return tx_total; } // bytes the handler wrote to the TX FIFO since isr_reset()

// A reply of this size from the dispatcher: a buffer, filled, handed to my_uart_transmit_packet (a task: it kicks the interrupt)
int isr_reply(int size)
{
    command_buf_t *b;
    if (my_uart_get_buffer(UART_NUM_1, &b, 0) != pdTRUE) {
        return 0;
    }
    memset(b->data, 0x55, (size_t)size);
    b->size = size;
    return my_uart_transmit_packet(UART_NUM_1, b) == pdTRUE;
}

int isr_tx_pending(void)
{
    return (int)uxQueueMessagesWaiting(ctx.transmitQueue) + (uart_obj.current_tx_buf != NULL);
}

int isr_event(void)
{
    command_buf_t *b;
    if (my_uart_get_buffer(UART_NUM_1, &b, 0) != pdTRUE) {
        return 0;
    }
    b->data[0] = 0x42;
    b->size = 5;
    my_uart_transmit_packet(UART_NUM_1, b);
    return 1;
}

// The next frame the dispatcher would get
int isr_received(int *size, int *dropped, int keep)
{
    command_buf_t *b;
    if (cmd_buffer_received(&ctx, &b, 0) != pdTRUE) {
        return 0;
    }
    *size = b->size;
    *dropped = b->dropped;
    if (!keep) {
        cmd_buffer_free(&ctx, b); // the dispatcher is done with it
    }
    return 1;
}

// The UART drains and the interrupt is serviced, until every queued frame is out: returns the rounds (max_rounds: never got there)
int isr_tx_settle(int max_rounds)
{
    for (int rounds = 0; rounds < max_rounds; rounds++) {
        isr_service_tx();
        isr_uart_drain(1000);
        isr_service_tx();
        if (isr_tx_pending() == 0) {
            return rounds;
        }
    }
    return max_rounds;
}
