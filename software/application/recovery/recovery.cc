#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "itu.h"
#include "c64.h"
#include "c64_subsys.h"
#include "screen.h"
#include "keyboard.h"
#include "userinterface.h"
#include "tree_browser.h"
#include "browsable_root.h"
#include "filemanager.h"
#include "file_device.h"
#include "config.h"
#include "path.h"
#include "rtc.h"
#include "stream.h"
#include "init_function.h"
#include "dump_hex.h"
#include "usb_base.h"

// these should move to main_loop.h
extern "C" void main_loop(void *a);

TreeBrowser *root_tree_browser;
C64 *c64;
C64_Subsys *c64_subsys;


extern "C" void ultimate_main(void *a)
{
    char time_buffer[32];

    uint32_t capabilities = getFpgaCapabilities();
    custom_outbyte = 0; // stop logging

	printf("*** 1541 Recovery V3.0 ***\n");
    printf("*** FPGA Capabilities: %8x ***\n\n", capabilities);
    
	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

	puts("Executing init functions.");
	InitFunction :: executeAll();
	usb2.initHardware();

	UserInterface *ui = 0;
    
    c64 = C64 :: getMachine();
    c64_subsys = new C64_Subsys(c64);
	c64->init();
    c64->start();

    if(c64 && c64->exists()) {
        ui = new UserInterface("** Ultimate-II+ Recovery **", false);
        ui->init(c64);

    	// Instantiate and attach the root tree browser
        Browsable *root = new BrowsableRoot();
    	root_tree_browser = new TreeBrowser(ui, root);
        ui->activate_uiobject(root_tree_browser); // root of all evil!
    }

    printf("All linked modules have been initialized and are now running.\n");
    static char buffer[8192];
    vTaskList(buffer);
    puts(buffer);

    if(ui) {
    	ui->run();
    } else {
    	vTaskSuspend(NULL); // Stop main task and wait forever
    }

    custom_outbyte = 0; // stop logging
    printf("GUI running on C64 host has terminated? This should not happen.\n");

    vTaskSuspend(NULL); // Stop executing init task

    // We will never come here.
    
    printf("Graceful exit!!\n");
//    return 0;
}
