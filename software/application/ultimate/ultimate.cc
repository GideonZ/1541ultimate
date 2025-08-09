#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "itu.h"
#include "syslog.h"
#include "c64.h"
#include "c64_subsys.h"
#include "screen.h"
#include "keyboard.h"
#include "userinterface.h"
#include "tree_browser.h"
#include "browsable_root.h"
#include "filemanager.h"
#include "config.h"
#include "path.h"
#include "rtc.h"
#include "stream.h"
#include "host_stream.h"
#include "ui_stream.h"
#include "stream_menu.h"
#include "overlay.h"
#include "init_function.h"
#include "stream_uart.h"
#include "stream_textlog.h"
#include "dump_hex.h"
#include "usb_base.h"
#include "home_directory.h"
#include "u2p.h"
#include "u64.h"
#include "keyboard_usb.h"
#include "i2c_drv.h"
#include "product.h"

bool connectedToU64 = false;

TreeBrowser *root_tree_browser;
StreamMenu *root_menu;
Overlay *overlay;
C64 *c64;
C64_Subsys *c64_subsys;
HomeDirectory *home_directory;
StreamTextLog textLog(96*1024);
Syslog syslog;

extern "C" void (*custom_outbyte)(int c);

extern "C" {
	void print_tasks(void);
}

void outbyte_log(int c)
{
	textLog.charout(c);
}

void outbyte_log_syslog(int c)
{
	textLog.charout(c);  // Internal log
	syslog.charout(c);  // Remote syslog server
}

extern "C" void ultimate_main(void *a)
{
    // Normal boot log size is about 5k right now, so 16k should be enough to
    // give time to flush the buffer.
    const int syslog_bufsize = 16*1024;
    custom_outbyte = syslog.init(syslog_bufsize) ? outbyte_log_syslog : outbyte_log;

    char time_buffer[32];

    uint32_t capabilities = getFpgaCapabilities();

    char product_version[64];
    printf("*** %s ***\n", getProductVersionString(product_version, sizeof(product_version), true));
    printf("*** FPGA Capabilities: %8x ***\n\n", capabilities);

	puts("Executing init functions.");
	InitFunction :: executeAll();
   
    printf("%s ", rtc.get_long_date(time_buffer, 32));
    printf("%s\n", rtc.get_time_string(time_buffer, 32));

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
    getProductTitleString(title, sizeof(title), false);

    if(capabilities & CAPAB_ULTIMATE64) {
        system_usb_keyboard.setMatrix((volatile uint8_t *)MATRIX_KEYB);
        system_usb_keyboard.enableMatrix(true);
    }

    overlay = NULL;
    UserInterface *overlayUserInterface = NULL;

#if U64 == 2
    i2c->enable_scan(true, false);
    overlay = new Overlay(false, 12, U64II_OVERLAY_BASE);
    Keyboard *kb = new Keyboard_C64(overlay, &U64II_KEYB_ROW, &U64II_KEYB_COL, &U64II_KEYB_JOY);
    overlay->setKeyboard(kb);
#elif U64 == 1
    overlay = new Overlay(false, 11, U64_OVERLAY_BASE);
    Keyboard *kb = new Keyboard_C64(overlay, C64_PLD_PORTB, C64_PLD_PORTA, C64_PLD_PORTA);
    overlay->setKeyboard(kb);
#endif

#if U64
    overlayUserInterface = new UserInterface(title);
    Browsable *root = new BrowsableRoot();
    root_tree_browser = new TreeBrowser(overlayUserInterface, root);
    overlayUserInterface->activate_uiobject(root_tree_browser); // root of all evil!
    overlayUserInterface->init(overlay);
    if(overlayUserInterface->cfg->get_value(CFG_USERIF_START_HOME)) {
        new HomeDirectory(overlayUserInterface, root_tree_browser);
        // will clean itself up
    }
#endif

    UserInterface *c64UserInterface = NULL;
    if(c64) {
        c64UserInterface = new UserInterface(title);
        // Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
        root_tree_browser = new TreeBrowser(c64UserInterface, root);
        c64UserInterface->activate_uiobject(root_tree_browser); // root of all evil!
        c64UserInterface->init(c64);
        if(c64UserInterface->cfg->get_value(CFG_USERIF_START_HOME)) {
            home_directory = new HomeDirectory(c64UserInterface, root_tree_browser);
            // will clean itself up
        }
    }

    printf("All linked modules have been initialized and are now running.\n");
    print_tasks();

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
            system_usb_keyboard.enableMatrix(false);
            ui->run_once();
            system_usb_keyboard.enableMatrix(true);
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
    if(home_directory)
        delete home_directory;
    
    printf("Graceful exit!!\n");
//    return 0;
}
