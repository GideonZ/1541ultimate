/*
 * test_loader.cc
 *
 *  Created on: Nov 20, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "chargen.h"
#include "screen.h"
#include "u64ii_tester.h"
#include "stream_textlog.h"
#include "init_function.h"
#include "u64.h"
#include "dump_hex.h"
#include "usb_base.h"
#include "screen_logger.h"
#include "flash.h"
#include "product.h"
#include "wifi_cmd.h"

#include "fpll.h"
#include "color_timings.h"
#include "hdmi_scan.h"

// These defines are from the test concept that runs tests via jtag.
#define PROG_BUFFER      (uint8_t *)(*(volatile int *)(0x0080))
#define TEST_PARAM		 (*(volatile int *)(0x0084))
#define PROG_PROGRESS    (*(volatile int *)(0x0088))
#define PROG_LENGTH      (*(volatile int *)(0x008C))
#define PROG_LOCATION    (*(volatile int *)(0x0090))
#define DUT_TO_TESTER    (*(volatile uint32_t *)(0x0094))
#define TESTER_TO_DUT	 (*(volatile uint32_t *)(0x0098))
#define TEST_STATUS		 (*(volatile int *)(0x009C))
#define VOLTAGES         ((volatile voltages_t *)(0x00A0))
#define SERIAL_NUMBER    ((volatile char *)(0x00B0))

Screen *screen;
Screen *screen2;
StreamTextLog textLog(96*1024);

extern "C" {
    static void screen_outbyte(int c) {
        //screen2->output(c);
        textLog.charout(c);
    }
}

#define CHARGEN_REGS  ((volatile t_chargen_registers *)U64II_CHARGEN_REGS)
void initScreen()
{
    // const t_video_mode systemMode = e_NTSC_60;
    // const t_video_color_timing *ct = color_timings[(int)systemMode];
    // SetVideoPll(systemMode);
    // SetHdmiPll(systemMode, ct->mode_bits);
    // SetVideoMode1080p(systemMode);
    // ResetHdmiPll();
    // wait_ms(200);
    printf("Measurement: %08x\n", U64_CLOCK_FREQ);

    //TVideoMode mode = { 01, 25175000,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 };  // VGA 60
    //SetScanModeRegisters((volatile t_video_timing_regs *)CHARGEN_TIMING, &mode);

    CHARGEN_REGS->TRANSPARENCY     = 0;
    CHARGEN_REGS->CHAR_WIDTH       = 12;
    CHARGEN_REGS->CHAR_HEIGHT      = 31;
    CHARGEN_REGS->CHARS_PER_LINE   = 100;
    CHARGEN_REGS->ACTIVE_LINES     = 39;
    CHARGEN_REGS->X_ON_HI          = 1;
    CHARGEN_REGS->X_ON_LO          = 100;
    CHARGEN_REGS->Y_ON_HI          = 0;
    CHARGEN_REGS->Y_ON_LO          = 72;
    CHARGEN_REGS->POINTER_HI       = 0;
    CHARGEN_REGS->POINTER_LO       = 0;
    CHARGEN_REGS->PERFORM_SYNC     = 0;
    CHARGEN_REGS->TRANSPARENCY     = 0x80;

    screen = new Screen_MemMappedCharMatrix((char *)U64II_CHARGEN_SCREEN_RAM, (char *)U64II_CHARGEN_COLOR_RAM, 100, 39);
    screen->clear();
    custom_outbyte = screen_outbyte;
    printf("Screen Initialized.\n");
}

#define CHARGEN_VGA   ((volatile t_chargen_registers *)U64II_VGA_CHARGEN_REGS)
void initVGAScreen()
{
    CHARGEN_VGA->TRANSPARENCY     = 0;
    CHARGEN_VGA->CHAR_WIDTH       = 12;
    CHARGEN_VGA->CHAR_HEIGHT      = 31;
    CHARGEN_VGA->CHARS_PER_LINE   = 40;
    CHARGEN_VGA->ACTIVE_LINES     = 3;
    CHARGEN_VGA->X_ON_HI          = 0;
    CHARGEN_VGA->X_ON_LO          = 158;
    CHARGEN_VGA->Y_ON_HI          = 0;
    CHARGEN_VGA->Y_ON_LO          = 140;
    CHARGEN_VGA->POINTER_HI       = 0;
    CHARGEN_VGA->POINTER_LO       = 0;
    CHARGEN_VGA->PERFORM_SYNC     = 0;
    CHARGEN_VGA->TRANSPARENCY     = 0x80;

    screen2 = new Screen_MemMappedCharMatrix((char *)U64II_VGA_CHARGEN_SCREEN_RAM, (char *)U64II_VGA_CHARGEN_COLOR_RAM, 40, 3);
    screen2->clear();
    console_print(screen2, "\n\e1           VGA Test Pattern...\n");
}

extern "C" {
    void ultimate_main(void *context);
}

static bool my_memcmp(void *a, void *b, int len)
{
    uint32_t *pula = (uint32_t *)a;
    uint32_t *pulb = (uint32_t *)b;
    len >>= 2;
    while (len--) {
        if (*pula != *pulb) {
            printf("ERR: %p: %8x, %p %8x.\n", pula, *pula, pulb, *pulb);
            return false;
        }
        pula++;
        pulb++;
    }
    return true;
}

bool flash_buffer_at(int address, void *buffer, int length)
{
    Flash *flash = get_flash();
    flash->protect_disable();

    static int last_sector = -1;
    if (length > 9000000) {
        return false;
    }
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    char *verify_buffer = new char[page_size]; // is never freed, but the hell we care!

    p = (char *)buffer;

    bool do_erase = flash->need_erase();
    int sector;
    while (length > 0) {
        if (do_erase) {
            sector = flash->page_to_sector(page);
            if (sector != last_sector) {
                last_sector = sector;
                // printf("Erasing sector %d\n", sector);
                if (!flash->erase_sector(sector)) {
                    printf("Erase failed...\n");
                    last_sector = -1;
                    return false;
                }
            }
        }
        int retry = 3;
        while (retry > 0) {
            retry--;
            if (!flash->write_page(page, p)) {
                printf("Programming error on page %d.\n", page);
                continue;
            }
            PROG_PROGRESS++;
            if (do_erase) { // HACK: For AT45, which does not need to erase, we do not verify either; because write page
                            // size = 528 and read page size = 512. ;-)
                flash->read_page(page, verify_buffer);
                if (!my_memcmp(verify_buffer, p, page_size)) {
                    printf("Verify failed on page %d. (%d/3) (Page size: %d)\n", page, retry, page_size);
                    dump_hex_verify(p, verify_buffer, page_size);
                    continue;
                }
            }
            retry = -2;
        }
        if (retry != -2) {
            last_sector = -1;
            printf("Programming failed...\n");
            return false;
        }
        page++;
        p += page_size;
        length -= page_size;
    }
    return true;
}

int verify(uint32_t size_addr, uint32_t src_addr, uint32_t dest_addr)
{
    volatile uint32_t *size = (volatile uint32_t *)size_addr;
    uint8_t *src = (uint8_t *)src_addr;

    info_message("Verifying %d bytes at %08x\n", *size, dest_addr);
    Flash *flash = get_flash();
    int pages = *size / flash->get_page_size();
    int offset = dest_addr / flash->get_page_size();
    uint8_t *buffer = (uint8_t *)malloc(flash->get_page_size());
    for (int i=0; i<pages; i++) {
        flash->read_page(offset + i, buffer);
        if (memcmp(buffer, src, flash->get_page_size())) {
            info_message("\ejVerification failed at page %d.\n", i);
            dump_hex_relative(buffer, flash->get_page_size());
            dump_hex_relative(src, flash->get_page_size());
            return 1;
        }
        src += flash->get_page_size();
    }
    return 0;
}

int attempt_programming(uint32_t size_addr, uint32_t src_addr, uint32_t dest_addr)
{
    volatile uint32_t *size = (volatile uint32_t *)size_addr;
    uint8_t *src = (uint8_t *)src_addr;

    while (*size == 0) {
        info_message("Waiting for programming data...\n");
        vTaskDelay(500);
    }

    info_message("Programming %d bytes to %08x\n", *size, dest_addr);
    if (flash_buffer_at(dest_addr, src, *size)) {
        pass_message("Programming successful.");
        return 0;
    } else {
        fail_message("Programming aborted or failed.");
        return -1;
    }
}

void esp_cb(void *)
{
    PROG_PROGRESS++;
}

int programESP32()
{
    if (PROG_LENGTH > 0x200000) {
        warn_message("Flashing", "Flash buffer too big (%d)\n", PROG_LENGTH);
        return -2;
    }
    if (PROG_LENGTH < 0) {
        warn_message("Flashing", "Flash length < 0 (%d)\n", PROG_LENGTH);
        return -3;
    }
    if ((PROG_LOCATION < 0) || (PROG_LOCATION > 8*1024*1024)) {
        warn_message("Flashing", "Illegal flash address %08x\n", PROG_LOCATION);
        return -4;
    }
    int j = PROG_LENGTH >> 2;
    uint32_t sum = 0;
    for(int i = 0; i < j; i++) {
        sum += ((uint32_t *)PROG_BUFFER)[i];
    }
    info_message("Programming %d bytes to ESP32 at %08x, from %p. Sum = %08x\n", PROG_LENGTH, PROG_LOCATION, PROG_BUFFER, sum);
    return esp32.Flash(PROG_BUFFER, PROG_LOCATION, PROG_LENGTH, esp_cb, NULL);
}

int programFlash()
{
	PROG_PROGRESS = 0;

	if (PROG_LENGTH > 0x400000) {
		warn_message("Flashing", "Flash buffer too big (%d)\n", PROG_LENGTH);
		return -2;
	}
	if (PROG_LENGTH < 0) {
		warn_message("Flashing", "Flash length < 0 (%d)\n", PROG_LENGTH);
		return -3;
	}
	if ((PROG_LOCATION < 0) || (PROG_LOCATION > 16*1024*1024)) {
		warn_message("Flashing", "Illegal flash address %08x\n", PROG_LOCATION);
		return -4;
	}
    int j = PROG_LENGTH >> 2;
    uint32_t sum = 0;
    for(int i = 0; i < j; i++) {
        sum += ((uint32_t *)PROG_BUFFER)[i];
    }
    info_message("Programming %d bytes to Flash at %08x, from %p. Sum = %08x\n", PROG_LENGTH, PROG_LOCATION, PROG_BUFFER, sum);

	if (flash_buffer_at(PROG_LOCATION, PROG_BUFFER, PROG_LENGTH)) {
		return 0;
	} else {
		return -1;
	}
}

int test_all()
{
    int errors = 0;
    memset((void *)VOLTAGES, 0, sizeof(voltages_t));

    usb2.initHardware();
    if (TEST_PARAM & 0x0001) errors += U64TestKeyboard();
    if (TEST_PARAM & 0x0002) errors += U64TestIEC();
    if (TEST_PARAM & 0x0004) errors += U64TestUserPort();
    if (TEST_PARAM & 0x0008) errors += U64TestCartridge();
    if (TEST_PARAM & 0x0010) errors += U64TestCassette();
    if (TEST_PARAM & 0x0020) errors += U64TestJoystick();
    if (TEST_PARAM & 0x0040) errors += U64TestPaddle();
    if (TEST_PARAM & 0x0080) errors += U64TestSidSockets();
    if (TEST_PARAM & 0x0100) errors += U64TestAudioCodecSilence();
    if (TEST_PARAM & 0x0200) errors += U64TestAudioCodecPurity();
    if (TEST_PARAM & 0x0400) errors += U64TestSpeaker();
    if (TEST_PARAM & 0x0800) errors += U64TestWiFiComm();
    if (TEST_PARAM & 0x1000) errors += U64TestVoltages();
    if (TEST_PARAM & 0x2000) errors += U64TestUsbHub();
    if (TEST_PARAM & 0x4000) errors += U64TestEthernet();

    info_message("\nTest completed with %d errors.\n", errors);
    return errors;
}

void test_loop()
{
    while (1) {
        vTaskDelay(1);
        if (!TESTER_TO_DUT)
            continue;

        int result = 0;

        switch (TESTER_TO_DUT) {
        case 1:
            result = U64TestKeyboard();
            break;
        case 2:
            result = U64TestIEC();
            break;
        case 3:
            result = U64TestUserPort();
            break;
        case 4:
            result = U64TestCartridge();
            break;
        case 5:
            result = U64TestCassette();
            break;
        case 6:
            result = U64TestJoystick();
            break;
        case 7:
            result = U64TestPaddle();
            break;
        case 8:
            result = U64TestSidSockets();
            break;
        case 9:
            result = U64TestAudioCodecSilence();
            break;
        case 10:
            result = U64TestAudioCodecPurity();
            break;
        case 11:
            result = U64TestSpeaker();
            break;
        case 12:
            result = U64TestWiFiComm();
            break;
        case 13:
            result = U64TestVoltages();
            break;
        case 14:
            result = U64TestUsbHub();
            break;
        case 15:
            usb2.initHardware();
            vTaskDelay(200);
            break;
        case 16:
            result = U64TestEthernet();
            break;
        case 17:
            result = U64TestOff();
            break;
        case 18:
            result = U64TestReboot();
            break;
        case 19:
            result = U64TestSetSerial();
            break;
        case 20:
            result = U64TestGetSerial();
            break;
        case 21:
            result = U64TestClearConfig();
            break;
        case 22:
            result = U64TestClearFlash();
            break;
        case 50:
            result = programFlash();
            break;
        case 51:
            PROG_PROGRESS = 0;
            result = esp32.Download();
            break;
        case 52:
            result = programESP32();
            break;
        case 53:
            esp32.EnableRunMode();
            wifi_command_init();
            break;
        case 99:
            printf("Alive V1.3!\n");
            break;
        case 101:
            result = test_all();
            break;
        default:
            result = -5;
        }
        TEST_STATUS = result;
        TESTER_TO_DUT = 0;
        DUT_TO_TESTER ++;
    }
}

void ultimate_main(void *context)
{
    // Report ourselves to the tester.
    printf("DUT Main..\n");
	DUT_TO_TESTER = 1;
	TEST_STATUS = 0;
    uint32_t *__warm_boot = (uint32_t *)0x80001FFC;

    Flash *flash = get_flash();
    initVGAScreen();
    initScreen();
    screen->clear();
    screen->move_cursor(0,0);
    info_message("U64E-II Tester - 20.03.2025 - 11:48\n\n");
    info_message("Board Revision: %s\n", getBoardRevision());
    uint8_t serial[8];
    flash->read_serial(serial);
    info_message("Flash Type: %s\n", flash->get_type_string());
    info_message("Hardware Serial Number: %b%b:%b%b:%b%b:%b%b\n\n", serial[0], serial[1], serial[2], serial[3], serial[4], serial[5], serial[6], serial[7]);
    info_message("Warm boot: %08x\n", *__warm_boot);
	InitFunction :: executeAll();

    test_loop();

    int errors = 0; // always flash

    if (errors == 0) {
        attempt_programming(0xFFFFF8, 0x1800000, 0x400000);
        attempt_programming(0xFFFFF4, 0x1400000, 0x220000);
        attempt_programming(0xFFFFF0, 0x1000000, 0x000000);
        errors += verify(0xFFFFF0, 0x1000000, 0x000000);
        errors += verify(0xFFFFF4, 0x1400000, 0x220000);
        errors += verify(0xFFFFF8, 0x1800000, 0x400000);
        if(errors) {
            fail_message("Flashing Firmware, board not OK.");
        } else {
            flash->protect_configure(4096);
            flash->protect_enable();
            pass_message("Board OK!");
        }
    } else {
        fail_message("Board not OK.");
    }
    vTaskDelay(portMAX_DELAY);
}
