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

Screen *screen;
StreamTextLog textLog(96*1024);

extern "C" {
    static void screen_outbyte(int c) {
        screen->output(c);
        textLog.charout(c);
    }
}

#define CHARGEN_REGS  ((volatile t_chargen_registers *)U64II_CHARGEN_REGS)

void initScreen()
{
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

extern "C" {
    void ultimate_main(void *context);
}

void ultimate_main(void *context)
{
    initScreen();
    screen->clear();
    screen->move_cursor(0,0);
    printf("\e4U64E-II Tester - 02.11.2024 - 11:48\e?\n");
	InitFunction :: executeAll();
    usb2.initHardware();

    int errors = 0;
    errors += U64TestKeyboard();
    errors += U64TestIEC();
    errors += U64TestUserPort();
    errors += U64TestCartridge();
    errors += U64TestCassette();
    errors += U64TestJoystick();
    errors += U64TestPaddle();
    errors += U64TestSidSockets();
    errors += U64TestAudioCodec();
    errors += U64TestWiFiComm();
    errors += U64TestVoltages();

    vTaskDelay(portMAX_DELAY);
}
