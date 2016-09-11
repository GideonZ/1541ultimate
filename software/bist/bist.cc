/*
 * bist.c
 *
 *  Created on: Sep 10, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <string.h>

#include "c64.h"
#include "u2p.h"

extern C64 *c64; // dirty, we should create a singleton using a static in the class itself.. More work right now, so we'll leave it.

// on CIA2 DD00-DDFF:

// PA7: DATA
// PA6: CLOCK
// PA5: DATA_OUT
// PA4: CLOCK_OUT
// PA3: ATN_OUT

// on CIA1 DC00-DCFF:
// FLAG = CASS_READ = SRQ_IN

/* Hardware:
 *
IEC_SRQ_IN <= '0' when iec_srq_o   = '0' or sw_iec_o(3) = '0' else 'Z';
IEC_ATN    <= '0' when iec_atn_o   = '0' or sw_iec_o(2) = '0' else 'Z';
IEC_DATA   <= '0' when iec_data_o  = '0' or sw_iec_o(1) = '0' else 'Z';
IEC_CLOCK  <= '0' when iec_clock_o = '0' or sw_iec_o(0) = '0' else 'Z';

sw_iec_i <= IEC_SRQ_IN & IEC_ATN & IEC_DATA & IEC_CLOCK;
*/

int test_iec(Screen *s)
{
	//c64->stop();
	int errors = 0;

	// test FROM C64 to U2P
	// DATA/CLOCK/ATN
	C64_POKE(0xDD02, 0x3B); // 00111011
	uint8_t iec_read;
	for (int i=0;i<8;i++) {
		C64_POKE(0xDD00, 0x03 + uint8_t(i << 3)); // this should generate a pattern on the IEC pins
		C64_PEEK(0xDD00);
		C64_PEEK(0xDD00);
		C64_PEEK(0xDD00);
		iec_read = (U2PIO_SW_IEC >> 4) ^ 0x0F;
		if ((iec_read & 0x01) != ((i & 0x02) >> 1)) {
			console_print(s, "Error on IEC Clock line. C64->U2P\n");
			errors ++;
		}
		if ((iec_read & 0x02) != ((i & 0x04) >> 1)) {
			console_print(s, "Error on IEC Data line. C64->U2P\n");
			errors ++;
		}
		if ((iec_read & 0x04) != ((i & 0x01) << 2)) {
			console_print(s, "Error on IEC ATN line. C64->U2P\n");
			errors ++;
		}
	}
	C64_POKE(0xDD00, 0x03); // this should drive the outputs of the CIA low, thus release the IEC bus pins.

	for (int i=0;i<4;i++) {
		U2PIO_SW_IEC = 0xC0 + (i << 4);
		C64_PEEK(0xDD00);
		C64_PEEK(0xDD00);
		iec_read = C64_PEEK(0xDD00) >> 6;
		if (iec_read != (uint8_t)i) {
			console_print(s, "Error in IEC Clock or Data. U2P->C64.\n");
			errors ++;
		}
	}
	U2PIO_SW_IEC = 0xF0; // release all lines

	// TODO: Check SRQ line, which only routes to the FLAG input and to the 6510 PIO Cassette read
	C64_PEEK(0xDC0D); // clear pending flags
	C64_PEEK(0xDC0D); // clear pending flags
	U2PIO_SW_IEC = 0x70; // pull SRQ line low
	C64_PEEK(0xDD00); // dummy delay
	C64_PEEK(0xDD00);
	iec_read = C64_PEEK(0xDC0D);
	if ((iec_read & 0x10) == 0) {
		console_print(s, "SRQ line did not arrive at the C64.\n");
		errors ++;
	}
	U2PIO_SW_IEC = 0xF0; // release all lines

	return errors;
}

int test_cartbus(Screen *s)
{
	//c64->stop();
	// first test all address and data bits by simply filling the C64 memory from 0000-7FFF and C000-CFFF with its 16-bit address
	volatile uint16_t *ram = (volatile uint16_t *)C64_MEMORY_BASE;

	// disable carts
	C64_SERVE_CONTROL  =  SERVE_WHILE_STOPPED;

	C64_MODE           = 0;
    C64_CARTRIDGE_TYPE = CART_TYPE_NONE;
    C64_MODE 		   = C64_MODE_RESET;
    C64_KERNAL_ENABLE  = 0;
    wait_ms(10);
    C64_MODE           = C64_MODE_UNRESET;

	int errors = 0;
	for (int i=2048;i<16384;i++) {
		ram[i] = (uint16_t)i;
	}
	for (int i=0x6000; i < 0x6800; i++) {
		ram[i] = (uint16_t)i;
	}

	// now verify
	for (int i=2048;i<16384;i++) {
		if (ram[i] != (uint16_t)i) {
			errors ++;
		}
	}
	for (int i=0x6000; i < 0x6800; i++) {
		if(ram[i] != (uint16_t)i) {
			errors ++;
		}
	}
	if (errors) {
		console_print(s, "There were %d memory verify errors!\n", errors);
		return errors;
	}

	// let's test ROML and ROMH. Secretly we also test the GAME line
	C64_MODE           = MODE_ULTIMAX;
    C64_CARTRIDGE_TYPE = CART_TYPE_16K_UMAX;
    C64_MODE = C64_MODE_RESET;
    C64_KERNAL_ENABLE = 0;
    wait_ms(10);
    C64_MODE = C64_MODE_UNRESET;

    // Now that we are in ultimax mode, we should be able to read back our own ROM data at F0000 in DRAM
    uint32_t mem_addr = ((uint32_t)C64_CARTRIDGE_RAM_BASE) << 16;
    uint8_t  *rom = (uint8_t *)mem_addr;

    if (memcmp(rom, (void *)&ram[0x4000], 0x1000) != 0) {
    	console_print(s, "Memory not equal when comparing my own\nDRAM at %p and C64 ROM at 0x8000\n", rom);
    	errors ++;
    }
    if (memcmp(rom + 8192, (void *)&ram[0x7000], 0x1000) != 0) {
    	console_print(s, "Memory not equal when comparing my own\nDRAM at %p and C64 ROM at 0xE000\n", rom+8192);
    	errors ++;
    }

    // now let's test the I/O1 and I/O2 pins. Secretly we also test the EXROM line
	C64_MODE           = 0; // release ultimax mode, let the cartridge decide
    C64_CARTRIDGE_TYPE = CART_TYPE_RETRO;
    C64_MODE           = C64_MODE_RESET;
    wait_ms(10);
    C64_MODE           = C64_MODE_UNRESET;

    // Now we are in retro mode. Mode = 0; bank = 0. So, serve IO1/IO2 should be true.
    // This means we can now read DE00-DFFF through I/O through the bus! BANK0, so we are mapped at offset mem_address + 0x1E00
    if (memcmp(rom + 0x1e04, (void *)&ram[0x6F02], 0x1FC) != 0) {
    	console_print(s, "Memory not equal when comparing my own\nDRAM at %p and C64 ROM at 0xDE00\n", rom+0x1e00);
    	errors ++;
    }

    return errors;
}

#include "audio_select.h"
#include "sampler.h"

extern uint32_t _sample_raw_start;
extern uint32_t _sample_raw_end;

int test_audio(void)
{
    ioWrite8(AUDIO_SELECT_LEFT,  6);
    ioWrite8(AUDIO_SELECT_RIGHT, 7);

    ioWrite8(VOICE_PAN(0), 0x0);
    ioWrite8(VOICE_PAN(1), 0x7);
    ioWrite8(VOICE_PAN(2), 0xF);
    ioWrite8(VOICE_VOLUME(0), 0x3C);
    ioWrite8(VOICE_VOLUME(1), 0x3C);
    ioWrite8(VOICE_VOLUME(2), 0x3C);

    uint32_t start  = uint32_t(&_sample_raw_start);
    uint32_t length = uint32_t(&_sample_raw_end) - uint32_t(&_sample_raw_start);

    for(int i=0;i<3;i++) {
		ioWrite8(VOICE_CONTROL(i), 0);
		ioWrite8(VOICE_START(i), uint8_t(start >> 24));
		ioWrite8(VOICE_START(i)+1, uint8_t(start >> 16));
		ioWrite8(VOICE_START(i)+2, uint8_t(start >> 8));
		ioWrite8(VOICE_START(i)+3, uint8_t(start));
		ioWrite8(VOICE_LENGTH(i), uint8_t(length >> 24));
		ioWrite8(VOICE_LENGTH(i)+1, uint8_t(length >> 16));
		ioWrite8(VOICE_LENGTH(i)+2, uint8_t(length >> 8));
		ioWrite8(VOICE_LENGTH(i)+3, uint8_t(length));
		uint16_t rate = 300 - (20 * i);
		ioWrite8(VOICE_RATE(i)+2, uint8_t(rate >> 8));
		ioWrite8(VOICE_RATE(i)+3, uint8_t(rate));
		ioWrite8(VOICE_CONTROL(i), (VOICE_CTRL_ENABLE | VOICE_CTRL_16BIT));
		wait_ms(1000);
    }
    return 0;
}
