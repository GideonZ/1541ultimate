#include "integer.h"
#include "itu.h"
#include "keys.h"

#define CAPABILITIES_0    *((volatile uint8_t*)(ITU_BASE + 0x0C))
#define CAPABILITIES_1    *((volatile uint8_t*)(ITU_BASE + 0x0D))
#define CAPABILITIES_2    *((volatile uint8_t*)(ITU_BASE + 0x0E))
#define CAPABILITIES_3    *((volatile uint8_t*)(ITU_BASE + 0x0F))

uint32_t getFpgaCapabilities()
{
	uint32_t res = 0;
#if !RUNS_ON_PC
	res  = ((uint32_t)CAPABILITIES_0 << 24);
	res |= ((uint32_t)CAPABILITIES_1 << 16);
	res |= ((uint32_t)CAPABILITIES_2 << 8);
	res |= ((uint32_t)CAPABILITIES_3);
#endif
	return res;
}

uint8_t getFpgaVersion()
{
#if RUNS_ON_PC
	return 0x33;
#else
	return ioRead8(ITU_FPGA_VERSION);
#endif
}


/*
-------------------------------------------------------------------------------
							wait_ms
							=======
  Abstract:

	Simple blocking wait using the small timer.

  Parameters
	time:		integer, time in ms.

-------------------------------------------------------------------------------
*/
void wait_ms(int time)
{
	int i;
    for(i=0;i<time;i++) {
        ioWrite8(ITU_TIMER, 200);
        while(ioRead8(ITU_TIMER))
            ;
    }
}

uint16_t getMsTimer() {
	return ioRead16(__ITU_MS_TIMER);
}
/*
-------------------------------------------------------------------------------
							uart_data_available
							===================
  Abstract:

	Returns if something is available from the uart

  Parameters
	hnd:		unused
	buf:		pointer to buffer receiving data
	count:		number of bytes to read

  Return:
	BOOL:		FALSE if not
	others:		if so
-------------------------------------------------------------------------------
*/
BOOL uart_data_available(void)
{
	return (ioRead8(UART_FLAGS) & UART_RxDataAv);
}

/*
-------------------------------------------------------------------------------
							uart_read_buffer
							================
  Abstract:

	Reads from the uart

  Parameters
	hnd:		unused
	buf:		pointer to buffer receiving data
	count:		number of bytes to read

  Return:
	i:		number of bytes read
-------------------------------------------------------------------------------
*/
uint16_t uart_read_buffer(const void *buf, uint16_t count)
{
    volatile uint8_t st;
    uint8_t *pnt = (uint8_t *)buf;
    uint16_t i = 0;
    uint8_t d;

	d = 0;   
    do {
        st = ioRead8(UART_FLAGS);
        if (st & UART_RxDataAv) {
            if (count) {
                d = ioRead8(UART_DATA);
                ioWrite8(UART_DATA, d); // echo
               	*(pnt++) = d;
                ioWrite8(UART_GET, 0);
                count --;
                i++;
            }
        } else {
            //break; // only break for FILES, not for uart. Break on newline
        }
        d &= 0x7F;
    } while(count && (d != VT100_CR) && (d != VT100_NL));
//    UART_DATA = 0x2E;
    
    return i;
}

/*
-------------------------------------------------------------------------------
							uart_write_buffer
							=================
  Abstract:

	Writes to the uart

  Parameters
	hnd:		unused
	buf:		pointer to buffer containing data to write
	count:		number of bytes to write

  Return:
	i:		number of bytes read
-------------------------------------------------------------------------------
*/
uint16_t uart_write_buffer(const void *buf, uint16_t count)
{
    uint16_t i;
    uint8_t *pnt = (uint8_t *)buf;

    for(i=0;i<count;i++) {
        while(ioRead8(UART_FLAGS) & UART_TxFifoFull);
//        while(!(st & UART_TxReady));

//        if(*pnt == 0x0A) {
//            UART_DATA = 0x0D;
//        }

//		if(*pnt == 0x0D) {
//	        UART_DATA = *pnt++;
//            UART_DATA = 0x0A;
//			continue;
//        }
        ioWrite8(UART_DATA, *pnt++);
    }

    return count;
}

/*
-------------------------------------------------------------------------------
							uart_write_hex
							==============
  Abstract:

	Writes a hex byte to the uart

  Parameters
	hex_byte:   hex byte
	count:		number of bytes to write

  Return:
	i:		number of bytes read
-------------------------------------------------------------------------------
*/
uint16_t uart_write_hex(uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    outbyte(hex[b >> 4]);
    outbyte(hex[b & 15]);
    return 2;
}

/*
-------------------------------------------------------------------------------
							uart_get_byte
							=============
  Abstract:

	Gets one byte from the uart, waiting for a maximum of n ms

  Parameters
	delay:		number of ms to wait

  Return:
	i:		    character returned (returns -2 on timeout)
-------------------------------------------------------------------------------
*/
int uart_get_byte(int delay)
{
    uint8_t d;
    
    while(!(ioRead8(UART_FLAGS) & UART_RxDataAv)) {
        ioWrite8(ITU_TIMER, 240);
        while(ioRead8(ITU_TIMER))
            ;
        delay--;
        if(delay <= 0)
            return -2;
    }
    d = ioRead8(UART_DATA);
    ioWrite8(UART_GET, 0);
    return (int)d;
}
/*
-------------------------------------------------------------------------------
							uart_put_byte
							=============
  Abstract:

	Puts one byte into the fifo of the UART.

-------------------------------------------------------------------------------

void uart_put_byte(BYTE c)
{
    while(UART_FLAGS & UART_TxFifoFull);
    UART_DATA = c;
}
*/

void (*custom_outbyte)(int c) = 0;

void outbyte(int c)
{
    if (custom_outbyte) {
        custom_outbyte(c);
    } else {
        // Wait for space in FIFO
        while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
        ioWrite8(UART_DATA, c);
    }
}

#ifdef RUNS_ON_PC
#include <stdio.h>
void ioWrite8(uint32_t addr, uint8_t value) {
	printf("Write %02x to %08x\n", value, addr);
}

void ioWrite16(uint32_t addr, uint16_t value) {
	printf("Write %04x to %08x\n", value, addr);
}

void ioWrite32(uint32_t addr, uint32_t value) {
	printf("Write %08x to %08x\n", value, addr);
}

uint8_t  ioRead8(uint32_t addr) {
	return 0;
}
uint16_t ioRead16(uint32_t addr) {
	return 0;
}
uint32_t ioRead32(uint32_t addr) {
	return 0;
}
void outbyte(int a) {
	fputc(a, stdout);
}
#endif

