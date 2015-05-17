/*
 * mb_ui.cc
 *
 *  Created on: May 15, 2015
 *      Author: Gideon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern "C" {
	#include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
}

#include "host.h"
#include "c64.h"
#include "flash.h"
#include "screen.h"
#include "keyboard.h"
#include "config.h"
#include "userinterface.h"
#include "browsable.h"
#include "tree_browser.h"

#include "FreeRTOS.h"
#include "task.h"

//using namespace std;
#include "mystring.h"

extern "C" void main_loop(void *a);

Screen *screen;
UserInterface *user_interface;
Flash *flash;

int main()
{
	string work;

	printf("*** Ultimate User Interface Test ***\n\n");
    printf("FPGA Capabilities = %8x\n", getFpgaCapabilities());

    user_interface = new UserInterface;

    GenericHost *host;
	host = new C64;
	host->reset();
    wait_ms(500);
    host->freeze();

    screen = new Screen(host->get_screen(), host->get_color_map(), 40, 25);
    screen->move_cursor(0,0);
    screen->output("**** 1541 Ultimate II UI Test ****\n"); // \020 = alpha \021 = beta
    for(int i=0;i<40;i++)
        screen->output('\002');
    screen->output("\x1B[31mRED\n");
    screen->output("\x1B[32mGREEN\n");
    screen->output("\x1B[33mYELLOW\n");
    screen->output("\x1B[34mBLUE\n");
    screen->output("\x1B[35mMAGENTA\n");
    screen->output("\x1B[36mCYAN\n");
    screen->output("\x1B[37mWHITE\n");
    screen->output("\x1B[2mDIM\n");
    screen->output("\x1B[7mREVERSE\n");
    screen->output("\x1B[35;1mBRIGHT MAGENTA\n");
    screen->output("\x1B[0mRESET\n");

    user_interface->init(host, host->get_keyboard());
    user_interface->set_screen(screen);

/* Test popup */
	user_interface->popup("This is a PopUp", BUTTON_OK);

/* Test progress bar */
	user_interface->show_progress("Wait for 5 seconds...", 50);
	for(int i=0;i<25;i++) {
		vTaskDelay(20);
		user_interface->update_progress(NULL, 1);
	}
	work = "Wait.";
	for(int i=0;i<5;i++) {
		vTaskDelay(100);
		work += ".";
		user_interface->update_progress(work.c_str(), 5);
	}
	user_interface->hide_progress();

/* Test string input box */
	char name_string[20];
	memset(name_string, 0xBB, 20);
	strcpy(name_string, "Gideon");
	user_interface->string_box("What is your name?", name_string, 16);
	dump_hex(name_string, 20);

/* Test Tree Browser */

	Browsable *root = new BrowsableTest(100, true, "Root");
	TreeBrowser *tree_browser = new TreeBrowser(root);
    user_interface->activate_uiobject(tree_browser);

    main_loop(NULL);
}
