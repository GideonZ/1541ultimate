/*
 (C) COPYRIGHT 2016 TECHNOLUTION B.V., GOUDA NL
   =======          I                   ==          I    =
      I             I                    I          I
 |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
 |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
 |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
 |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
 |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
 |                 +---------------------------------------------------+
 +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
      |            |             ++++++++++++++++++++++++++++++++++++++|
      +------------+                          +++++++++++++++++++++++++|
                                                         ++++++++++++++|
                                                                  +++++|
 */

/**
 * @file    main.c
 * @author  Roy Rietveld (roy.rietveld@technolution.eu)
 * @brief   Bootloader copy SD card image into DDR and execute
 * 			Adapted version in order to run Ultimate executable on SigmaXG hardware.
 */

#include <stdint.h>

#include "sys/alt_cache.h"
#include "sys/alt_irq.h"

#define LOADER_UART 0xC1000400
#define DEBUG_UART  0xC0000010

typedef struct {
	uint8_t data;
	uint8_t get;
	uint8_t flags;
	uint8_t ictrl;
} uart_t;

volatile uart_t *debugUart  = (volatile uart_t *) DEBUG_UART;
volatile uart_t *loaderUart = (volatile uart_t *) LOADER_UART;

#define UART_Overflow    0x01
#define UART_TxFifoFull  0x10
#define UART_RxFifoFull  0x20
#define UART_TxReady     0x40
#define UART_RxDataAv    0x80

// Record types
#define HALT_RECORD 0xFFFFFFFF
#define JUMP_RECORD 0x00000000

// The NIOS2 reset address
#define RESET_ADDRESS 0x022c

// Store the SVN revision at known location, so can find it in the application
// static const char __attribute__((section (".version"))) version[32] = VERSION;


static inline void SerialFlush(volatile uart_t* uart)
{
    while (uart->flags & UART_RxDataAv) {
        uart->get = 1;
    }
}

static inline uint8_t SerialRead(volatile uart_t* uart)
{
    while (!(uart->flags & UART_RxDataAv));
    uint8_t data = uart->data;
    uart->get = 1;
    return data;
}

static inline void SerialWrite(volatile uart_t* uart, uint8_t data)
{
    while (uart->flags & UART_TxFifoFull);
    uart->data = data;
}

static void SerialReadBuffer(volatile uart_t* uart, uint8_t* buffer, uint32_t length)
{
    uint32_t i;
    for (i = 0; i < length; i++) {
        buffer[i] = SerialRead(uart);
    }
}

static void SerialWriteBuffer(volatile uart_t* uart, const uint8_t* buffer, uint32_t length)
{
    uint32_t i;
    for (i = 0; i < length; i++) {
        SerialWrite(uart, buffer[i]);
    }
}

static void Debug(const char* str)
{
    while (*str) {
        SerialWrite(debugUart, *str++);
    }
}

static void SendErpcRequest(void)
{
    static const uint8_t request[] = {0x10, 0x02, 0x02, 0x00, 0xA0, 0x5B, 0xC8, 0xBA, 0x10, 0x03};

    // Send the request
    SerialWriteBuffer(loaderUart, request, sizeof(request));
}

static void JumpToApplication(void target(void))
{
    /*
     * If you have any outstanding I/O or system resources that needed to be
     * cleanly disabled before leaving the boot copier program, then this is
     * the place to do that.
     *
     * In this example we only need to ensure the state of the Nios II cpu is
     * equivalent to reset.  If we disable interrupts, and flush the caches,
     * then the program we jump to should receive the cpu just as it would
     * coming out of a hardware reset.
     */
    alt_irq_disable_all();
//    alt_dcache_flush_all();
//    alt_icache_flush_all();

    /*
     * The cpu state is as close to reset as we can get it, so we jump to the new
     * application.
     */
    target();
}

/**
 *  Main entry point.
 *  @return  None.
 */
int main(void)
{
    uint32_t length;
    uint32_t address = 0;
    uint32_t startaddr = 0x022c;

    // Let the world know we are alive
//    Debug("Bootloader stopped now...\n");
//
//    while(1)
//    	;

    Debug("Bootloader starting...\n");

    // Flush any data in the receive buffer
    SerialFlush(loaderUart);

    // Let the FPGA loader know we are ready to receive
    SendErpcRequest();

	// Get the section address
	SerialReadBuffer(loaderUart, (uint8_t*)&address, sizeof(address));

	// Get the section length
	SerialReadBuffer(loaderUart, (uint8_t*)&length, sizeof(length));

	// Get the runaddr
	SerialReadBuffer(loaderUart, (uint8_t*)&startaddr, sizeof(startaddr));


	SerialReadBuffer(loaderUart, (uint8_t*)address, length);
	Debug("Done loading.\n");

	JumpToApplication((void (*)(void))startaddr);
}
