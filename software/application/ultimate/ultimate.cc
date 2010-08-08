#include <stdlib.h>
#include <string.h>

#include "c64.h"
#include "c1541.h"
#include "small_printf.h"
#include "screen.h"
#include "keyboard.h"
#include "userinterface.h"
#include "tree_browser.h"
#include "filemanager.h"
#include "sd_card.h"
#include "file_device.h"
#include "config.h"
#include "path.h"
#include "rtc.h"
#include "tape_controller.h"

// these should move to main_loop.h
void main_loop(void);
void send_nop(void);

C1541 *c1541;
UserInterface *user_interface;

/*
char *en_dis[] = { "Disabled", "Enabled" };
//char *on_off[] = { "Off", "On" };
char *yes_no[] = { "No", "Yes" };
char *pal_ntsc[] = { "PAL", "NTSC" };

char *rom_sel[] = { "CBM 1541", "1541 C", "1541-II", "Load from SD" };

struct t_cfg_definition old_items[] = {
    { 0xC1, CFG_TYPE_STRING, "Owner",                   "%s", NULL,       1, 31, (int)"Pietje puk" },
    { 0xC2, CFG_TYPE_STRING, "Application to boot",     "%s", NULL,       1, 31, (int)"appl.bin" },
    { 0xC3, CFG_TYPE_ENUM,   "Video System",            "%s", pal_ntsc,   0,  1, 0 },
    { 0xC4, CFG_TYPE_ENUM,   "1541 Drive",              "%s", en_dis,     0,  1, 1 },
    { 0xC5, CFG_TYPE_VALUE,  "1541 Drive Bus ID",       "%d", NULL,       8, 11, 8 },
    { 0xC6, CFG_TYPE_ENUM,   "1541 ROM Select",         "%s", rom_sel,    0,  3, 2 },
    { 0xC7, CFG_TYPE_ENUM,   "1541 RAMBOard",           "%s", en_dis,     0,  1, 0 },
    { 0xC8, CFG_TYPE_ENUM,   "Load last mounted disk",  "%s", yes_no,     0,  1, 0 },
    { 0xC9, CFG_TYPE_VALUE,  "1541 Disk swap delay",    "%d00 ms", NULL,  1, 10, 1 },
    { 0xCA, CFG_TYPE_ENUM,   "IEC SDCard I/F",          "%s", en_dis,     0,  1, 1 },
    { 0xCB, CFG_TYPE_VALUE,  "IEC SDCard I/F Bus ID",   "%d", NULL,       8, 30, 9 },
    { 0xD0, CFG_TYPE_ENUM,   "Pull SD = remove floppy", "%s", en_dis,     0,  1, 1 },
    { 0xD1, CFG_TYPE_ENUM,   "Swap reset/freeze btns",  "%s", yes_no,     0,  1, 0 },
    { 0xD2, CFG_TYPE_ENUM,   "Hide '.'-files",          "%s", yes_no,     0,  1, 1 },
    { 0xD3, CFG_TYPE_ENUM,   "Scroller in menu",        "%s", en_dis,     0,  1, 1 },
    { 0xD4, CFG_TYPE_ENUM,   "Startup in menu",         "%s", yes_no,     0,  1, 0 },
    { 0xD5, CFG_TYPE_ENUM,   "Ethernet Interface",      "%s", en_dis,     0,  1, 0 },
    { 0xD6, CFG_TYPE_VALUE,  "Number of boots",         "%d", NULL,       0, 99999, 0 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};
*/

void poll_drive_1(Event &e)
{
	c1541->poll(e);
}

void poll_c64(Event &e)
{
	c64->poll(e);
}

int main()
{
	char time_buffer[32];
	printf("*** 1541 Ultimate V2.0 ***\n\n");

	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

    c1541 = new C1541(C1541_IO_LOC_DRIVE_1);
    c64   = new C64;

	tape_controller = new TapeController;

    user_interface = new UserInterface;
    user_interface->init(c64, c64->get_keyboard());

    TreeBrowser *root_tree_browser = new TreeBrowser();
    user_interface->activate_uiobject(root_tree_browser); // root of all evil!

	// start the file system, scan the sd-card etc..
	send_nop();
	send_nop();
	
	// add the drive and C64 to the root of all evil
    poll_list.append(&poll_drive_1);

    poll_list.append(&poll_c64);
    c64->init_cartridge();
	
    printf("All linked modules have been initialized.\n");
    printf("Starting main loop...\n");
    main_loop();

    delete root_tree_browser;
    delete user_interface;
	delete tape_controller;
    delete c1541;
    delete c64;

    //printf("Cleaned up main components.. now.. what's left??\n");
    //root.dump();
    
    printf("Graceful exit!!\n");
}
