#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "itu.h"
#include "c64.h"
#include "c64_subsys.h"
#include "c1541.h"
#include "screen.h"
#include "keyboard.h"
#include "userinterface.h"
#include "tree_browser.h"
#include "browsable_root.h"
#include "filemanager.h"
#include "sd_card.h"
#include "file_device.h"
#include "config.h"
#include "path.h"
#include "rtc.h"
#include "tape_controller.h"
#include "tape_recorder.h"
#include "stream.h"
#include "host_stream.h"
#include "ui_stream.h"
#include "stream_menu.h"
#include "audio_select.h"
#include "overlay.h"
#include "init_function.h"
#include "stream_uart.h"
#include "stream_textlog.h"
#include "dump_hex.h"
#include "usb_base.h"
#include "home_directory.h"
#include "reu_preloader.h"
#include "u2p.h"
#include "u64.h"
#include "keyboard_usb.h"

// these should move to main_loop.h
extern "C" void main_loop(void *a);

bool isEliteBoard(void) __attribute__((weak));
bool isEliteBoard(void)
{
    return false;
}

bool connectedToU64 = false;

C1541 *c1541_A;
C1541 *c1541_B;

TreeBrowser *root_tree_browser;
StreamMenu *root_menu;
Overlay *overlay;
C64 *c64;
C64_Subsys *c64_subsys;
HomeDirectory *home_directory;
REUPreloader *reu_preloader;
StreamTextLog textLog(96*1024);

extern "C" void (*custom_outbyte)(int c);

void outbyte_log(int c)
{
	textLog.charout(c);
}

extern "C" void ultimate_main(void *a)
{
    char time_buffer[32];

    uint32_t capabilities = getFpgaCapabilities();

	printf("*** 1541 Ultimate V3.0 ***\n");
    printf("*** FPGA Capabilities: %8x ***\n\n", capabilities);
    
	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

	puts("Executing init functions.");
	InitFunction :: executeAll();
    
	if (capabilities & CAPAB_CARTRIDGE) {
		c64 = C64 :: getMachine();
		c64_subsys = new C64_Subsys(c64);
		c64->init();
		c64->start();
	} else {
		c64 = NULL;
	}

    usb2.initHardware();

    char title[48];
    if(capabilities & CAPAB_ULTIMATE64) {
        if (isEliteBoard()) {
            sprintf(title, "\eA** Ultimate 64 Elite V1.%b - %s **\eO", C64_CORE_VERSION, APPL_VERSION);
        } else {
            sprintf(title, "\eA*** Ultimate 64 V1.%b - %s ***\eO", C64_CORE_VERSION, APPL_VERSION);
        }
    } else if(capabilities & CAPAB_ULTIMATE2PLUS) {
    	sprintf(title, "\eA*** Ultimate-II Plus %s (1%b) ***\eO", APPL_VERSION, getFpgaVersion());
    } else {
    	sprintf(title, "\eA*** 1541 Ultimate-II %s (1%b) ***\eO", APPL_VERSION, getFpgaVersion());
    }

    if(capabilities & CAPAB_ULTIMATE64) {
        system_usb_keyboard.setMatrix((volatile uint8_t *)MATRIX_KEYB);
        system_usb_keyboard.enableMatrix(true);
    }

    overlay = NULL;

    UserInterface *overlayUserInterface = NULL;
    if ((capabilities & CAPAB_OVERLAY) && (capabilities & CAPAB_ULTIMATE64)) {
        overlay = new Overlay(false);
        Keyboard *kb = new Keyboard_C64(overlay, C64_PLD_PORTB, C64_PLD_PORTA);
        overlay->setKeyboard(kb);
        overlayUserInterface = new UserInterface(title);
        Browsable *root = new BrowsableRoot();
        root_tree_browser = new TreeBrowser(overlayUserInterface, root);
        overlayUserInterface->activate_uiobject(root_tree_browser); // root of all evil!
        overlayUserInterface->init(overlay);
        if(overlayUserInterface->cfg->get_value(CFG_USERIF_START_HOME)) {
            new HomeDirectory(overlayUserInterface, root_tree_browser);
            // will clean itself up
        }
    }

    UserInterface *c64UserInterface = NULL;
    if(c64) {
        c64UserInterface = new UserInterface(title);
        // Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
        root_tree_browser = new TreeBrowser(c64UserInterface, root);
        c64UserInterface->activate_uiobject(root_tree_browser); // root of all evil!
        c64UserInterface->init(c64);
        if(c64UserInterface->cfg->get_value(CFG_USERIF_START_HOME)) {
            new HomeDirectory(c64UserInterface, root_tree_browser);
            // will clean itself up
        }
    }

    if(capabilities & CAPAB_C2N_STREAMER)
	    tape_controller = new TapeController;
    if(capabilities & CAPAB_C2N_RECORDER)
	    tape_recorder   = new TapeRecorder;
    if(capabilities & CAPAB_DRIVE_1541_1)
        c1541_A = new C1541(C1541_IO_LOC_DRIVE_1, 'A');
    if(capabilities & CAPAB_DRIVE_1541_2) {
        c1541_B = new C1541(C1541_IO_LOC_DRIVE_2, 'B');
    }

    if(c1541_A) {
    	c1541_A->init();
    }

    if(c1541_B) {
    	c1541_B->init();
    }

    reu_preloader = new REUPreloader();
    
    printf("All linked modules have been initialized and are now running.\n");
    static char buffer[8192];
    vTaskList(buffer);
    puts(buffer);

/*
#ifdef U64
    {
       *C64_PLD_PORTA = 251;
       uint8_t k1 = *C64_PLD_PORTB;
       *C64_PLD_PORTA = 247;
       uint8_t k2 = *C64_PLD_PORTB;
       *C64_PLD_PORTA = 255;

       if (k1 == 239)
          C64_VIDEOFORMAT = 0, C64_BURST_PHASE=24;
       if (k1 == 253)
          C64_VIDEOFORMAT = 4;
       if (k1 == 251)
          U64_HDMI_ENABLE = 0;
       if (k2 == 223)
          U64_HDMI_ENABLE = 1;

    }
#endif
*/

    custom_outbyte = outbyte_log;

    while(c64) {
        int doIt = 0;
        c64->checkButton();
        if (c64->buttonPush()) {
            doIt = 1;
        }
        switch(system_usb_keyboard.getch()) {
        case KEY_SCRLOCK:
        case KEY_F10:
            doIt = 1;
            break;
        case 0x04: // CTRL-D
            doIt = 2;
            break;
        }
        UserInterface *ui = c64UserInterface;
        if (overlayUserInterface) {
            if ((U64_HDMI_REG & U64_HDMI_HPD_CURRENT) && (doIt)) {
                if (overlayUserInterface->getPreferredType() == 1) {
                    ui = overlayUserInterface;
                }
            }
        }

        switch (doIt) {
        case 0:
            c64UserInterface->pollInactive();
            if (overlayUserInterface) {
                overlayUserInterface->pollInactive();
            }
            break;
        case 1:
            ui->run_once();
            break;
        case 2:
            ui->swapDisk();
            break;
        }
        vTaskDelay(3);
    }

    custom_outbyte = 0; // stop logging
    printf("GUI running on C64 host has terminated? This should not happen.\n");

    vTaskDelay(400);
    puts(textLog.getText());

    vTaskSuspend(NULL); // Stop executing init task

    // We will never come here.

    if(overlay)
        delete overlay;
    if(root_tree_browser)
        delete root_tree_browser;
    if(overlayUserInterface)
        delete overlayUserInterface;
    if(c64UserInterface)
        delete c64UserInterface;
    if(c64_subsys)
    	delete c64_subsys;
    if(c64)
        delete c64;
    if(c1541_A)
        delete c1541_A;
    if(c1541_B)
        delete c1541_B;
    if(tape_controller)
	    delete tape_controller;
    if(tape_recorder)
	    delete tape_recorder;
    if(home_directory)
        delete home_directory;
    if(reu_preloader)
      delete reu_preloader;
    
    printf("Graceful exit!!\n");
//    return 0;
}
