#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "itu.h"
}
#include "c64.h"
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
#include "ui_stream.h"
#include "stream_menu.h"
#include "audio_select.h"
#include "overlay.h"
#include "init_function.h"

// these should move to main_loop.h
extern "C" void main_loop(void *a);

C1541 *c1541_A;
C1541 *c1541_B;

UserInterface *user_interface;
TreeBrowser *root_tree_browser;
StreamMenu *root_menu;
Overlay *overlay;


void poll_drive_1(Event &e)
{
	c1541_A->poll(e);
}

void poll_drive_2(Event &e)
{
	c1541_B->poll(e);
}

void poll_c64(Event &e)
{
	c64->poll(e);
}

int main(void *a)
{
	char time_buffer[32];
	uint32_t capabilities = getFpgaCapabilities();

	printf("*** 1541 Ultimate V3.0 ***\n");
    printf("*** FPGA Capabilities: %8x ***\n\n", capabilities);
    
	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

	puts("Executing init functions.");
	InitFunction :: executeAll();

	Stream my_stream;
    UserInterfaceStream *stream_interface;
    
 	// start the file system, scan the sd-card etc..
	MainLoop :: nop();
	MainLoop :: nop();
	MainLoop :: nop();

    if(capabilities & CAPAB_CARTRIDGE)
        c64     = new C64;

    if(c64 && c64->exists()) {
        user_interface = new UserInterface;
        user_interface->init(c64);
    
    	// Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(root);
        user_interface->activate_uiobject(root_tree_browser); // root of all evil!
    	
    	// add the C64 to the 'OS' (the event loop)
        MainLoop :: addPollFunction(poll_c64);
    
        // now that everything is running, initialize the C64 and C1541 drive
    	// which might load custom ROMs from the file system.
    	c64->init_cartridge();
    } else if(capabilities & CAPAB_OVERLAY) {
        printf("Using Overlay module as user interface...\n");
        overlay = new Overlay(false);
        user_interface = new UserInterface;
        user_interface->init(overlay);
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(root);
        user_interface->activate_uiobject(root_tree_browser); // root of all evil!
        // push_event(e_button_press, NULL, 1);
    } else {
        // stand alone mode
        stream_interface = new UserInterfaceStream(&my_stream);
        user_interface = stream_interface;
        root_menu = new StreamMenu(&my_stream, new BrowsableRoot());
        stream_interface->set_menu(root_menu); // root of all evil!
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

	// add the drive(s) to the 'OS' (the event loop)
    printf("C1541A: %p, C1541B: %p\n", c1541_A, c1541_B);

    if(c1541_A) {
        MainLoop :: addPollFunction(poll_drive_1);
    	c1541_A->init();
    }

    if(c1541_B) {
        MainLoop :: addPollFunction(poll_drive_2);
    	c1541_B->init();
    }
	
    printf("All linked modules have been initialized.\n");
    printf("Starting main loop...\n");

    main_loop(0);

    if(overlay)
        delete overlay;
    if(root_tree_browser)
        delete root_tree_browser;
    if(user_interface)
        delete user_interface;
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

    //printf("Cleaned up main components.. now.. what's left??\n");
    //root.dump();
    
    printf("Graceful exit!!\n");
    return 0;
}


