/*
 * test_loader.cc
 *
 *  Created on: Nov 20, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "u2p.h"
#include "usb_base.h"
#include "filemanager.h"
#include "fpll.h"
#include "chargen.h"
#include "screen.h"
#include "rtc.h"
#include "prog_flash.h"
#include "u64_tester.h"

typedef struct {
	const char *fileName;
	const char *romName;
	const uint32_t flashAddress;
	uint32_t *buffer;
	uint32_t  size;
} BinaryImage_t;


BinaryImage_t images[] = {
     { "/Usb?/u64/u64.swp",      "FPGA Binary",         0x000000, 0, 0 },
     { "/Usb?/u64/ultimate.app", "Application Binary",  0x290000, 0, 0 },
     { "/Usb?/u64/ar5pal.bin",   "Action Replay 5",     0x400000, 0, 0 },
     { "/Usb?/u64/ar6pal.bin",   "Action Replay 6",     0x408000, 0, 0 },
     { "/Usb?/u64/final3.bin",   "Final Cartridge III", 0x410000, 0, 0 },
     { "/Usb?/u64/rr38pal.bin",  "Retro Replay",        0x420000, 0, 0 },
	 { "/Usb?/u64/ss5pal.bin",   "Super Snapshot",      0x430000, 0, 0 },
	 { "/Usb?/u64/tar_pal.bin",  "Turbo Assembler",     0x440000, 0, 0 },
     { "/Usb?/u64/rr38ntsc.bin", "Retro Replay NTSC",   0x450000, 0, 0 },
     { "/Usb?/u64/ss5ntsc.bin",  "Super Snapshot NTSC", 0x460000, 0, 0 },
     { "/Usb?/u64/tar_ntsc.bin", "Turbo Assembler NTSC",0x470000, 0, 0 },
	 { "/Usb?/u64/kcs.bin",      "KCS",                 0x480000, 0, 0 },
     { "/Usb?/u64/epyx.bin",     "Epyx Fastloader",     0x484000, 0, 0 },
//     { "/Usb?/u64/kerna*.bin",   "Kernal ROM",          0x446000, 0, 0 },
};

/*
{ FLASH_ID_AR5PAL,     0x00, 0x400000, 0x400000, 0x08000 },
{ FLASH_ID_AR6PAL,     0x00, 0x408000, 0x408000, 0x08000 },
{ FLASH_ID_FINAL3,     0x00, 0x410000, 0x410000, 0x10000 },
{ FLASH_ID_RR38PAL,    0x00, 0x420000, 0x420000, 0x10000 },
{ FLASH_ID_SS5PAL,     0x00, 0x430000, 0x430000, 0x10000 },
{ FLASH_ID_TAR_PAL,    0x00, 0x440000, 0x440000, 0x10000 },

{ FLASH_ID_RR38NTSC,   0x00, 0x450000, 0x450000, 0x10000 },
{ FLASH_ID_SS5NTSC,    0x00, 0x460000, 0x460000, 0x10000 },
{ FLASH_ID_TAR_NTSC,   0x00, 0x470000, 0x470000, 0x10000 },

{ FLASH_ID_KCS,        0x00, 0x480000, 0x480000, 0x04000 },
{ FLASH_ID_EPYX,       0x00, 0x484000, 0x484000, 0x02000 },
*/

#define NUM_IMAGES (sizeof(images) / sizeof(BinaryImage_t))

void jump_run(uint32_t a)
{
	void (*function)();
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}
#define ESP_UART_BASE  0xA0000500

#define ESP_UART_DATA  (ESP_UART_BASE + 0x00)
#define ESP_UART_GET   (ESP_UART_BASE + 0x01)
#define ESP_UART_FLAGS (ESP_UART_BASE + 0x02)
#define ESP_UART_ICTRL (ESP_UART_BASE + 0x03)

int esp32_get_byte(int delay)
{
    uint8_t d;

    ioWrite8(ITU_TIMER, 240);
    while(!(ioRead8(ESP_UART_FLAGS) & UART_RxDataAv)) {
        if (!ioRead8(ITU_TIMER)) {
            ioWrite8(ITU_TIMER, 240);
            delay --;
            if (delay <= 0) {
                return -2;
            }
        }
    }
    d = ioRead8(ESP_UART_DATA);
    ioWrite8(ESP_UART_GET, 0);
    return (int)d;
}

char *esp32_get_string(char *buffer, int buflen)
{
    buffer[buflen - 1] = 0;
    for(int i=0;i<buflen-1;i++) {
        int c = esp32_get_byte(100);
        if ((c == -2) || (c == 0x0a)) {
            buffer[i] = 0;
            break;
        }
        if (c == 0x0d) {
            continue;
        }
        buffer[i] = c;
    }
    return buffer;
}

void esp32_put_byte(uint8_t c)
{
    while(ioRead8(ESP_UART_FLAGS) & UART_TxFifoFull)
        ;
    ioWrite8(ESP_UART_DATA, c);
}

const char *getBoardRevision(void)
{
    uint8_t rev = (U2PIO_BOARDREV >> 3);

    switch (rev) {
    case 0x10:
        return "U64 Prototype";
    case 0x11:
        return "U64 V1.1 (Null Series)";
    case 0x12:
        return "U64 V1.2 (Mass Prod)";
    case 0x13:
        return "U64 V1.3 (Elite)";
    }
    return "Unknown";
}

int load_file(BinaryImage_t *flashFile)
{
	FRESULT fres;
	File *file;
	uint32_t transferred;

	FileManager *fm = FileManager :: getFileManager();
	fres = fm->fopen(flashFile->fileName, FA_READ, &file);
	if (fres == FR_OK) {
		flashFile->size = file->get_size();
		flashFile->buffer = (uint32_t *)malloc(flashFile->size + 8);
		fres = file->read(flashFile->buffer, flashFile->size, &transferred);
		if (transferred != flashFile->size) {
			printf("Expected to read %d bytes, but got %d bytes.\n", flashFile->size, transferred);
			return -1;
		}
	} else {
		printf("Warning: Could not open file '%s'! %s\n", flashFile->fileName, FileSystem :: get_error_string(fres));
		return -2;
	}
    printf("Successfully read %s.\n", flashFile->fileName);
    fm->fclose(file);
    return 0;
}

Screen *screen;

#define CHARGEN_BASE      0xA0040000
#define CHARGEN_TIMING    (CHARGEN_BASE + 0x0000)
#define CHARGEN_REGISTERS (CHARGEN_BASE + 0x4000)
#define CHARGEN_SCREEN    (CHARGEN_BASE + 0x5000)
#define CHARGEN_COLOR     (CHARGEN_BASE + 0x6000)

extern "C" {
    static void screen_outbyte(int c) {
        screen->output(c);
    }
}

// -- A0040000 is chargen base
// Mapping:
// 0000 : timing
// 4000 : chargen registers
// 5000 : screen
// 6000 : color ram

void test_esp32(void)
{
    char buffer[100];

    for(int i=0;i<16;i++) {
        ioWrite8(ESP_UART_GET, 0);
    }

    // First boot in bootloader mode
//    screen->clear();
//    screen->move_cursor(0,0);
    printf("\033\036Listening to BOOT...\n\033\030");
    U64_WIFI_CONTROL = 2;
    vTaskDelay(150);
    U64_WIFI_CONTROL = 7;


    for (int i=0;i<200000;i++) {
        if (ioRead8(ESP_UART_FLAGS) & UART_RxDataAv) {
            uint8_t c = ioRead8(ESP_UART_DATA);
            screen->output(c);
            ioWrite8(ESP_UART_GET, 0);
        }
    }
    printf("###\n");

    printf("\033\036Listening to RUN...\n\033\030");

    // Then boot in normal mode:
    U64_WIFI_CONTROL = 0;
    vTaskDelay(50);
    U64_WIFI_CONTROL = 5;

    for (int i=0;i<200000;i++) {
        if (ioRead8(ESP_UART_FLAGS) & UART_RxDataAv) {
            uint8_t c = ioRead8(ESP_UART_DATA);
            screen->output(c);
            ioWrite8(ESP_UART_GET, 0);
        }
    }
    printf("###\n");
}

int test_memory(void)
{
    uint16_t *random = new uint16_t[4096];

    uint16_t seed = 0x1B7F;
    for(int i=0; i < 4096; i++) {
        if (seed & 0x8000) {
            seed = (seed << 1) ^ 0x8004;
        } else {
            seed = (seed << 1) | 1;
        }
        random[i] = seed;
    }

    uint32_t dest = 0;
    uint16_t *src = random;
    int retval = 0;

    while (dest < (64*1024*1024)) {
        memcpy((void *)dest, src, 256);
        printf(">");
        src += 132;
        if (!dest) {
            dest = 0x100;
        } else {
            dest <<= 1;
        }
    }

    printf("\n");
    dest = 0;
    src = random;
    uint32_t verifyBuffer[64];

    while (dest < (64*1024*1024)) {
        memcpy(verifyBuffer, (void *)dest, 256);
        printf("<");

        if(memcmp(verifyBuffer, src, 256) != 0) {
            printf("Verify failure. RAM error at address %p.\n", dest);
            dump_hex_verify(src, verifyBuffer, 256);
            retval = -3;
            break;
        }

        src += 132;
        if (!dest) {
            dest = 0x100;
        } else {
            dest <<= 1;
        }
    }
    return retval;
}

void do_update(void)
{
    char time_buffer[32];
    printf("\n%s ", rtc.get_long_date(time_buffer, 32));
    printf("%s\n", rtc.get_time_string(time_buffer, 32));

    Flash *flash2 = get_flash();
    printf("\033\024Detected Flash: %s\n", flash2->get_type_string());

    const char *fpgaType = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) ? "5CEBA4" : "5CEBA2";
    printf("Detected FPGA Type: %s.\nBoard Revision: %s\n\033\037\n", fpgaType, getBoardRevision());

    flash2->protect_disable();

    for(int i=0;i<NUM_IMAGES;i++) {
        if (! flash_buffer_length(flash2, screen, images[i].flashAddress, false, images[i].buffer, images[i].size, "?", images[i].romName)) {
            printf("\033\022ERROR!\n\n");
            while(1)
                ;
        }
    }
    printf("\nConfiguring Flash write protection..\n");
    flash2->protect_configure();
    flash2->protect_enable();
    printf("Done!                            \n");
}

#define CHARGEN_REGS  ((volatile t_chargen_registers *)CHARGEN_REGISTERS)

void initScreen()
{
    TVideoMode mode = { 01, 25175000,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 };  // VGA 60
    SetScanModeRegisters((volatile t_video_timing_regs *)CHARGEN_TIMING, &mode);

    CHARGEN_REGS->TRANSPARENCY     = 0;
    CHARGEN_REGS->CHAR_WIDTH       = 8;
    CHARGEN_REGS->CHAR_HEIGHT      = 0x80 | 16;
    CHARGEN_REGS->CHARS_PER_LINE   = 79;
    CHARGEN_REGS->ACTIVE_LINES     = 29;
    CHARGEN_REGS->X_ON_HI          = 0;
    CHARGEN_REGS->X_ON_LO          = 0;
    CHARGEN_REGS->Y_ON_HI          = 0;
    CHARGEN_REGS->Y_ON_LO          = 8;
    CHARGEN_REGS->POINTER_HI       = 0;
    CHARGEN_REGS->POINTER_LO       = 0;
    CHARGEN_REGS->PERFORM_SYNC     = 0;
    CHARGEN_REGS->TRANSPARENCY     = 0x84;

    screen = new Screen_MemMappedCharMatrix((char *)CHARGEN_SCREEN, (char *)CHARGEN_COLOR, 79, 29);
    screen->clear();
    custom_outbyte = screen_outbyte;
    printf("Screen Initialized.\n");
}

int load_images(void)
{
    usb2.initHardware();
    FileManager *fm = FileManager :: getFileManager();

    printf("Waiting for USB storage device to become available.\n");
    FileInfo info(32);
    FRESULT res;
    do {
        vTaskDelay(100);
        res = fm->fstat("/Usb?", info);
        //printf("%s\n", FileSystem :: get_error_string(res));
    } while (res != FR_OK);

    for(int i=0;i<NUM_IMAGES;i++) {
        if(load_file(&images[i])) {
            printf("\033\022Could not load image. Did not flash.\n");
            return -1;
        }
    }
    return 0;
}

extern "C" {
	void main_task(void *context)
	{
	    initScreen();
	    printf("Ultimate-64 - LOADER...\n");

	    int errors = 0;
	    if (!test_memory()) {
	        if (!load_images()) {
	            screen->clear();
	            screen->move_cursor(0,0);
	            test_esp32();
	            errors  = U64TestKeyboard();
	            //errors += U64TestUserPort();
                errors += U64TestIEC();
	            errors += U64TestCartridge();
	            errors += U64TestCassette();
	            if (!rtc.is_valid()) {
	                errors ++;
	                printf("\e2RTC not valid. Battery inserted?\n\eO");
	            }
	            if (errors) {
	                printf("\n\e2** BOARD FAILED **\n");
	            } else {
	                printf("\n\e5-> Passed! Now programming....\eO\n");
	                do_update();
	            }
	        }
	    }
        printf("\n\n\033\023Waiting for you to turn off the machine..\n");
        while (1)
            ;
	}
}
