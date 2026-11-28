// The register block of the DMA UART for the host (replaces the struct of dma_uart.h in the test
// copy that the Makefile generates). The real struct is a volatile block of hardware registers in
// which `status` (read) and `ictrl` (write) are one byte and `length` is the TX length when written
// and the RX length when read, so RAM does not model it. Here every field is a proxy whose reads
// and writes go to a model of fpga/io/uart_lite/vhdl_source/uart_dma.vhd (DmaUartHw), which
// the test scripts and inspects.
#ifndef UART_HOST_DMA_UART_REGS_H
#define UART_HOST_DMA_UART_REGS_H
#include <stdint.h>
#include <string.h>
#include <vector>

struct DmaUartHw {
    // The state of uart_dma.vhd that the ISR sees
    bool rx_irq_en, tx_irq_en, buf_irq_en, overflow;
    bool rx_addr_valid, rx_len_valid, tx_addr_ready;
    uint8_t flowctrl;
    uint8_t *rx_addr, *tx_addr;
    uint16_t rx_len, tx_len;
    // What the ISR did
    std::vector<uint8_t> ictrl_writes;  // every write to the interrupt control register, in order
    int rx_pops, tx_pushes, rx_addr_writes;

    DmaUartHw() { reset_all(); }
    void reset_all()
    {
        rx_irq_en = tx_irq_en = buf_irq_en = overflow = false;
        rx_addr_valid = rx_len_valid = false;
        tx_addr_ready = true;
        flowctrl = 0;
        rx_addr = tx_addr = NULL;
        rx_len = tx_len = 0;
        ictrl_writes.clear();
        rx_pops = tx_pushes = rx_addr_writes = 0;
    }

    // Register reads. uart_dma.vhd: rx_interrupt <= rx_irq_enable and rx_len_valid, and so on
    uint8_t status() const
    {
        return (rx_irq_en && rx_len_valid ? 0x01 : 0) | (tx_irq_en && tx_addr_ready ? 0x02 : 0)
             | (buf_irq_en && !rx_addr_valid ? 0x04 : 0) | (overflow ? 0x08 : 0)
             | (rx_len_valid ? 0x20 : 0) | (tx_addr_ready ? 0x40 : 0) | (!rx_addr_valid ? 0x80 : 0);
    }

    // c_uart_imask: bit 7 selects set or clear of the enables in bits 0-2; bit 3 clears the overflow
    void write_ictrl(uint8_t v)
    {
        ictrl_writes.push_back(v);
        bool set = (v & 0x80) != 0;
        if (v & 0x01) rx_irq_en = set;
        if (v & 0x02) tx_irq_en = set;
        if (v & 0x04) buf_irq_en = set;
        if (v & 0x08) overflow = false;
    }
    // c_uart_flowctrl: bit 7 is a soft reset that also drops the enables and the RX address
    void write_flowctrl(uint8_t v)
    {
        flowctrl = v & 0x7F;
        if (v & 0x80) {
            rx_irq_en = tx_irq_en = buf_irq_en = false;
            rx_addr_valid = false;
        }
    }
    void write_rx_addr(uint8_t *p) { rx_addr = p; rx_addr_valid = true; rx_addr_writes++; }
    void write_tx_push() { tx_pushes++; tx_addr_ready = false; }
    void write_rx_pop() { rx_pops++; rx_len_valid = false; }

    // The test's side of the hardware
    // A frame of 'len' bytes arrives into the buffer that was given an address, whatever its size
    // (the receiver has no length limit), and the address is used up
    bool receive(size_t len, uint8_t fill)
    {
        if (!rx_addr_valid) {
            return false;
        }
        memset(rx_addr, fill, len);
        rx_len = (uint16_t)len;
        rx_len_valid = true;
        rx_addr_valid = false;
        return true;
    }
    void set_overflow() { overflow = true; }
    void tx_complete() { tx_addr_ready = true; }
};

inline DmaUartHw &dmauart_hw()
{
    static DmaUartHw hw;
    return hw;
}

// A register that reads and writes through the model. The members are trivial so that the
// DmaUART code, which reads them in printf() calls too, compiles as it is.
struct RegStatusIctrl {
    uint8_t unused;
    operator uint8_t() const volatile { return dmauart_hw().status(); }
    void operator=(uint8_t v) volatile { dmauart_hw().write_ictrl(v); }
};
struct RegFlowctrl {
    uint8_t unused;
    operator uint8_t() const volatile { return dmauart_hw().flowctrl; }
    void operator=(uint8_t v) volatile { dmauart_hw().write_flowctrl(v); }
    void operator|=(int v) volatile { *this = (uint8_t)(dmauart_hw().flowctrl | v); }
    void operator&=(int v) volatile { *this = (uint8_t)(dmauart_hw().flowctrl & v); }
};
struct RegPlain { // baud rate registers: only the value matters
    uint8_t v;
    operator uint8_t() const volatile { return v; }
    void operator=(uint8_t x) volatile { v = x; }
};
struct RegTxAddr {
    uint8_t unused;
    void operator=(uint8_t *p) volatile { dmauart_hw().tx_addr = p; }
};
struct RegRxAddr {
    uint8_t unused;
    void operator=(uint8_t *p) volatile { dmauart_hw().write_rx_addr(p); }
};
struct RegLength { // the TX length when written, the length of the received frame when read
    uint8_t unused;
    operator uint16_t() const volatile { return dmauart_hw().rx_len; }
    void operator=(uint16_t v) volatile { dmauart_hw().tx_len = v; }
};
struct RegTxPush {
    uint8_t unused;
    void operator=(uint8_t) volatile { dmauart_hw().write_tx_push(); }
};
struct RegRxPop {
    uint8_t unused;
    void operator=(uint8_t) volatile { dmauart_hw().write_rx_pop(); }
};

typedef struct _dma_uart_t
{
    RegPlain rate_l;
    RegPlain rate_h;
    union {
        RegStatusIctrl ictrl;
        RegStatusIctrl status;
    };
    RegFlowctrl flowctrl;
    RegTxAddr tx_addr;
    RegLength length;
    RegTxPush tx_push;
    RegRxPop rx_pop;
    RegRxAddr rx_addr;
} dma_uart_t;

#endif
