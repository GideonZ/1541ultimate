// This file defines the common functions used in the updaters
#include "prog_flash.h"
#include <stdio.h>
#include "itu.h"
#include "host.h"
#include "c64.h"
#include "c64_crt.h"
#include "screen.h"
#include "keyboard.h"
#include "stream.h"
#include "stream_uart.h"
#include "host_stream.h"
#include "dump_hex.h"
#include "u2p.h"
#include "rtc.h"
#include "userinterface.h"
#include "s25fl_l_flash.h"
#include "u64.h"
#include "overlay.h"
#include "filemanager.h"
#include "init_function.h"
#include "blockdev_flash.h"
#if U64 == 2
    #include "wifi_cmd.h"
#endif

static Screen *screen;
static UserInterface *user_interface;

#ifndef HTML_DIRECTORY
#define HTML_DIRECTORY "/flash/html"
#endif

int calc_checksum(uint8_t *buffer, uint8_t *buffer_end)
{
    int check = 0;
    int b;
    while(buffer != buffer_end) {
        b = (int)*buffer;
        if(check < 0)
            check = check ^ 0x801618D5; // makes it positive! ;)
        check <<= 1;
        check += b;
        buffer++;
    }
    return check;
}

const uint8_t orig_kernal[] = {
        0x85, 0x56, 0x20, 0x0f, 0xbc, 0xa5, 0x61, 0xc9, 0x88, 0x90, 0x03, 0x20, 0xd4, 0xba, 0x20, 0xcc
};

static void turn_off()
{
#if U64 == 1
    console_print(screen, "\n\033\022Turning OFF machine in 5 seconds....\n");

    wait_ms(5000);
    U64_POWER_REG = 0x2B;
    U64_POWER_REG = 0xB2;
    U64_POWER_REG = 0x2B;
    U64_POWER_REG = 0xB2;
    console_print(screen, "You shouldn't see this!\n");
#elif U64 == 2
    console_print(screen, "\n\033\022Turning OFF machine in 5 seconds....\n");

    wait_ms(5000);
    wifi_machine_off();
    wait_ms(1000);
    console_print(screen, "You shouldn't see this!\n");
#else
    console_print(screen, "\nPLEASE TURN OFF YOUR MACHINE.\n");
#endif

    while(1)
        ;
}

static void reset_config(Flash *fl)
{
    if(user_interface->popup("Reset Configuration? (Recommended)", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        int num = fl->get_number_of_config_pages();
        for (int i=0; i < num; i++) {
            fl->clear_config_page(i);
        }
    }
}

static bool original_kernal_found(Flash *flash, int addr)
{
    uint8_t buffer[16];
    flash->read_linear_addr(addr, 16, buffer);
    return (memcmp(buffer, orig_kernal, 16) == 0);
}

static void create_dir(const char *name)
{
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->create_dir(name);
    console_print(screen, "Creating '%s': %s\n", name, FileSystem :: get_error_string(fres));
}

static FRESULT write_flash_file(const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(ROMS_DIRECTORY, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        console_print(screen, "Writing %s to /flash: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
    if (fres != FR_OK) {
        user_interface->popup("Failed to write essentials. Abort!", BUTTON_OK);
        turn_off();
    }
    return fres;
}

static FRESULT write_html_file(const char *name, const char *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(HTML_DIRECTORY, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        console_print(screen, "Writing %s to /flash: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
    return fres;
}

static void copy_flash_binary(Flash *flash, uint32_t addr, uint32_t len, const char *fn)
{
    uint8_t *buffer = new uint8_t[len];
    flash->read_linear_addr(addr, len, buffer);
    FileManager *fm = FileManager :: getFileManager();
    console_print(screen, "Saving %s to /flash..\n", fn);
    fm->save_file(false, ROMS_DIRECTORY, fn, buffer, len, NULL);
}

static FRESULT check_flashdisk_empty()
{
    FileManager *fm = FileManager :: getFileManager();
    Path p;
    p.cd("/flash");
    IndexedList<FileInfo *> dir(16, NULL);
    FRESULT fres = fm->get_directory(&p, dir, NULL);
    if (fres != FR_OK) {
        return fres;
    }
    if (dir.get_elements() > 0) {
        return FR_DIR_NOT_EMPTY;
    }
    return FR_NO_FILE;
}

static uint32_t print_free_flash_blocks()
{
    FileManager *fm = FileManager :: getFileManager();
    Path p;
    p.cd("/flash");
    uint32_t free = 0, cs = 0;
    fm->get_free(&p, free, cs);
    console_print(screen, "\e5%d Flash Blocks Free\e?\n", free);
    return free;
}

static void clear_field(void)
{
    // The price we pay for not using a window to draw on
    for(int y=2; y<screen->get_size_y()-2;y++) {
        screen->move_cursor(0, y);
        screen->output_fixed_length("", 0, screen->get_size_x());
    }
    screen->move_cursor(0, 2);
}

static void setup(const char *title)
{
    printf(title);
    InitFunction::executeAll();
    init_flash_disk();

    Flash *flash = get_flash();
    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    C64 *c64 = C64 :: getMachine();

    if (c64->exists()) {
        host = c64;
    } else {
        host = new HostStream(stream);
    }
    screen = host->getScreen();

    user_interface = new UserInterface(title);
    user_interface->init(host);
    host->take_ownership(user_interface);
    user_interface->appear();
    screen->move_cursor(0, 2);

    char time_buffer[32];
    console_print(screen, "%s ", rtc.get_long_date(time_buffer, 32));
    console_print(screen, "%s\n", rtc.get_time_string(time_buffer, 32));
}

static void check_flash_disk()
{
    InitFunction :: executeAll();

    screen->move_cursor(0, 16);
    uint32_t free = print_free_flash_blocks();

    FRESULT flashdisk = check_flashdisk_empty();
    switch(flashdisk) {
        case FR_NO_FILE: // the only case we don't need to do anything
            break;
        case FR_DIR_NOT_EMPTY: // it may be OKAY, but we can ask the user if they want to reformat it
            if(user_interface->popup("Reformat Flash Disk?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
                console_print(screen, "\e7Formatting...\n");
                reformat_flash_disk();
                free = print_free_flash_blocks();
            }
            break;
        default:
            if(user_interface->popup("Error reading Flash Disk. Format?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
                console_print(screen, "\e7Formatting...\n");
                reformat_flash_disk();
                free = print_free_flash_blocks();
            }
    }
    if (free == 0) {
        user_interface->popup("Problem with Flash.. Abort!", BUTTON_OK);
        turn_off();
    }
}

static void write_protect(Flash *flash, int kilobytes)
{
    console_print(screen, "\nConfiguring Flash write protection..\n");
    flash->protect_configure(kilobytes);
    flash->protect_enable();
    console_print(screen, "Done!                            \n");
}
