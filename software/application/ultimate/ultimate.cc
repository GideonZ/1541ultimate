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
#include "u2p.h"
#include "keyboard_usb.h"

// these should move to main_loop.h
extern "C" void main_loop(void *a);

C1541 *c1541_A;
C1541 *c1541_B;

TreeBrowser *root_tree_browser;
StreamMenu *root_menu;
Overlay *overlay;
GenericHost *c64;
C64_Subsys *c64_subsys;
UserInterface *primaryUserInterface = 0;
HomeDirectory *home_directory;
StreamTextLog textLog(65536);

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
	usb2.initHardware();
    
	if (capabilities & CAPAB_CARTRIDGE) {
		c64 = new C64;
		c64_subsys = new C64_Subsys((C64 *)c64);
	} else {
		c64 = new GenericHost;
	}

    char title[48];
    if(capabilities & CAPAB_ULTIMATE2PLUS) {
    	sprintf(title, "\eA*** Ultimate-II Plus %s (1%b) ***\eO", APPL_VERSION, getFpgaVersion());
    } else {
    	sprintf(title, "\eA**** 1541 Ultimate %s (%b) ****\eO", APPL_VERSION, getFpgaVersion());
    }

    if(c64 && c64->exists()) {
        primaryUserInterface = new UserInterface(title);

    	// Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(primaryUserInterface, root);
        primaryUserInterface->activate_uiobject(root_tree_browser); // root of all evil!
        primaryUserInterface->init(c64);

    } else if(capabilities & CAPAB_OVERLAY) {
        printf("Using Overlay module as user interface...\n");
        overlay = new Overlay(false);
        primaryUserInterface = new UserInterface(title);
        system_usb_keyboard.setMatrix((volatile uint8_t *)MATRIX_KEYB);
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(primaryUserInterface, root);
        primaryUserInterface->activate_uiobject(root_tree_browser); // root of all evil!
        primaryUserInterface->init(overlay);
    } else {
    	// from now on, log to memory, freeing the uart
/*
    	custom_outbyte = outbyte_log;

    	Stream *stream = new Stream_UART;
        GenericHost *host = new HostStream(stream);
        ui = new UserInterface;
        ui->init(host);
        // Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(ui, root);
        ui->activate_uiobject(root_tree_browser); // root of all evil!
*/
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

    home_directory = new HomeDirectory(primaryUserInterface, root_tree_browser);
    
    printf("All linked modules have been initialized and are now running.\n");
    static char buffer[8192];
    vTaskList(buffer);
    puts(buffer);

    if(primaryUserInterface) {
    	primaryUserInterface->run();
    }
/*
    else {
    	vTaskDelay(2000);
    	printf("Attempting to write a test file to /Usb0.\n");
    	FileManager *fm = FileManager :: getFileManager();
    	File *file;
    	FRESULT fres;
    	DWORD tr;
    	fres = fm->fopen("/Usb0/testje.txt", FA_CREATE_NEW|FA_CREATE_ALWAYS|FA_WRITE, &file);
    	printf("%s\n", FileSystem :: get_error_string(fres) );
    	if (fres == FR_OK) {
    		file->write(buffer, 8192, &tr);
    		fm->fclose(file);
    	}
    	vTaskSuspend(NULL); // Stop main task and wait forever
    }
*/

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
    if(primaryUserInterface)
        delete primaryUserInterface;
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
    
    printf("Graceful exit!!\n");
//    return 0;
}
