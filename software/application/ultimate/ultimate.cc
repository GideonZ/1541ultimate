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

// these should move to main_loop.h
extern "C" void main_loop(void *a);

C1541 *c1541_A;
C1541 *c1541_B;

TreeBrowser *root_tree_browser;
StreamMenu *root_menu;
Overlay *overlay;
C64 *c64;
C64_Subsys *c64_subsys;

StreamTextLog textLog(65536);

extern "C" void (*custom_outbyte)(int c);

void outbyte_log(int c)
{
	textLog.charout(c);
}

int main(void *a)
{
	char time_buffer[32];
	uint32_t capabilities = getFpgaCapabilities();

	printf("*** 1541 Ultimate V3.0 ***\n");
    printf("*** FPGA Capabilities: %8x ***\n\n", capabilities);
    
	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

	// from now on, log to memory
	custom_outbyte = outbyte_log;

	puts("Executing init functions.");
	InitFunction :: executeAll();

	UserInterface *ui = 0;
    
    c64 = new C64;
    c64_subsys = new C64_Subsys(c64);

    if(c64 && c64->exists()) {
        ui = new UserInterface;
        ui->init(c64);

    	// Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(ui, root);
        ui->activate_uiobject(root_tree_browser); // root of all evil!

        // now that everything is running, initialize the C64
    	// which might load custom ROMs from the file system.
        c64->init_cartridge();

    } else if(capabilities & CAPAB_OVERLAY) {
        printf("Using Overlay module as user interface...\n");
        overlay = new Overlay(false);
        ui = new UserInterface;
        ui->init(overlay);
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(ui, root);
        ui->activate_uiobject(root_tree_browser); // root of all evil!
    } else if(custom_outbyte) {
        Stream *stream = new Stream_UART;
        GenericHost *host = new HostStream(stream);
/*
        stream->write("Hello, I am a little dog\n", 25);
        for(int i='A'; i <= 'Z'; i++)
        	stream->charout(i);
        stream->write("\nHello, I am a little cat\n", 26);
        stream->format("This should be formatted. %d %s %3x\n", 15, "rabbit", 64);
        Keyboard *kb = host->getKeyboard();
        for(int i=20; i>=0; ) {
        	int k = kb->getch();
        	if (k != -1) {
        		stream->format("%b ", k);
        		i--;
        	}
        }
*/
        ui = new UserInterface;
        ui->init(host);
        // Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(ui, root);
        ui->activate_uiobject(root_tree_browser); // root of all evil!
    } else {
        Stream *stream = new Stream_UART;
    	// stand alone mode
        printf("Using Stream module as user interface...\n");
        UserInterfaceStream *ui_str = new UserInterfaceStream(stream);
        root_menu = new StreamMenu(ui_str, stream, new BrowsableRoot());
        ui_str->set_menu(root_menu); // root of all evil!
        ui = ui_str;
        //ui->add_to_poll();
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
	
    printf("All linked modules have been initialized and are now running.\n");
    static char buffer[8192];
    vTaskList(buffer);
    puts(buffer);

    if(ui) {
    	ui->run();
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
    if(ui)
        delete ui;
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
    
    printf("Graceful exit!!\n");
    return 0;
}


