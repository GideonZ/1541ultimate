/*
 * update.cc (for Ultimate-64)
 *
 *      Author: gideon
 */
#define HTML_DIRECTORY "/flash/html"

#include "update_common.h"
#include "checksums.h"
#include "i2c_drv.h"
#include "wifi_cmd.h"
#include "product.h"
#include "usb_base.h"
#include "rpc_dispatch.h"

extern uint32_t _u64_rbf_start;
extern uint32_t _u64_rbf_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint32_t _bootloader_bin_start;
extern uint32_t _bootloader_bin_size;
extern uint32_t _partition_table_bin_start;
extern uint32_t _partition_table_bin_size;
extern uint32_t _u64ctrl_bin_start;
extern uint32_t _u64ctrl_bin_size;

extern uint8_t _1581_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;
extern const char _index_html_start[];
extern const char _index_html_end[1];


static void status_callback(void *user)
{
    UserInterface *ui = (UserInterface *)user;
    ui->update_progress(NULL, 1);
}

void do_update(void)
{
#if COMMODORE
    setup("\033\025** Commodore 64 Ultimate Updater **\n\033\037");
#else
    setup("\033\025** Ultimate 64 Elite-II Updater **\n\033\037");
#endif
    usb2.initHardware();

    Flash *flash2 = get_flash();
    console_print(screen, "\033\024Detected Flash: %s\n", flash2->get_type_string());

    const char *fpgaType = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) ? "5CEBA4" : "5CEBA2";
    console_print(screen, "Detected FPGA Type: %s.\nBoard Revision: %s\n\033\037\n", fpgaType, getBoardRevision());

    /* Extra check on the loaded images */
    const char *check_error = "\033\022\nBAD...\n\nNot flashing.\n";
    const char *check_ok = "\033\025OK!\n\033\037";

    console_print(screen, "\033\027Checking checksums of loaded images..\n");

    console_print(screen, "\033\037Checksum of FPGA image:   ");
    if(calc_checksum((uint8_t *)&_u64_rbf_start, (uint8_t *)&_u64_rbf_end) == CHK_u64_swp) {
        console_print(screen, check_ok);
    } else {
        console_print(screen, check_error);
        while(1);
    }
    console_print(screen, "\033\037Checksum of Application:  ");
    if(calc_checksum((uint8_t *)&_ultimate_app_start, (uint8_t *)&_ultimate_app_end) == CHK_ultimate_app) {
        console_print(screen, check_ok);
    } else {
        console_print(screen, check_error);
        while(1);
    }

    check_flash_disk();

#if SILENT
#else
    if(user_interface->popup("About to update. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES)
#endif
    {
        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        create_dir(HTML_DIRECTORY);

        write_flash_file("1581.rom", &_1581_bin_start, 0x8000);
        write_flash_file("1571.rom", &_1571_bin_start, 0x8000);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_flash_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
        write_flash_file("snds1581.bin", &_snds1581_bin_start, 0xC000);

        write_html_file("index.html", _index_html_start, (int)_index_html_end - (int)_index_html_start);

        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_u64_rbf_start, &_u64_rbf_end,   "V1.0", "Runtime FPGA");
        flash_buffer_at(flash2, screen, 0x220000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");

        write_protect(flash2, 4096);
    }

    reset_config(flash2);

    esp32.EnableRunMode();
    wifi_command_init();

#if SILENT
    goto esp32_done;
#endif
    uint16_t major = 0, minor = 0;
    char moduleName[32];
    BaseType_t module_detected;
    module_detected = wifi_detect(&major, &minor, moduleName, 32);
    module_detected = wifi_detect(&major, &minor, moduleName, 32); // second time should pass
    if (module_detected == pdTRUE) {
        console_print(screen, "WiFi module detected: %s (%d.%d)\n", moduleName, major, minor);
        
        // if(user_interface->popup("Want to update the WiFi Module?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        if ((major != IDENT_MAJOR) || (minor != IDENT_MINOR)) {
            if (esp32.Download() == 0) {
                uint32_t total_size = (uint32_t)&_bootloader_bin_size + (uint32_t)&_partition_table_bin_size + (uint32_t)&_u64ctrl_bin_size;
                user_interface->show_progress("Flashing ESP32", total_size / 1024);
                int status = 0;
                status = esp32.Flash((uint8_t *)&_bootloader_bin_start, 0x000000, (uint32_t)&_bootloader_bin_size, status_callback, user_interface);
                if (status == 0) {
                    status = esp32.Flash((uint8_t *)&_partition_table_bin_start, 0x008000, (uint32_t)&_partition_table_bin_size, status_callback, user_interface);
                }
                if (status == 0) {
                    status = esp32.Flash((uint8_t *)&_u64ctrl_bin_start, 0x010000, (uint32_t)&_u64ctrl_bin_size, status_callback, user_interface);
                }
                user_interface->hide_progress();
                printf("Flashing ESP32 Status: %d.\n", status);
                if (status == 0) {
                    user_interface->popup("Flashing ESP32 Success!", BUTTON_OK);
                } else {
                    user_interface->popup("Flashing ESP32 Failed!", BUTTON_OK);
                }
                esp32.EnableRunMode();
            } else {
                user_interface->popup("Could not set ESP32 to download mode", BUTTON_OK);
            }
        }
    } else {
        console_print(screen, "WiFi module not detected.\n");
    }
esp32_done:
    // assuming that the ESP32 is running still, we should be able to send a slip message to it
    wifi_command_init();
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
    i2c->enable_scan(true, false);
	do_update();
    return 0;
}

extern "C" {
int _close(int file)
{
    return -1;
}

ssize_t _write(int file, const void *ptr, size_t len)
{
    return (size_t)0; // nothing sent
}

ssize_t _read(int file, void *ptr, size_t len)
{
    return 0;
}

off_t _lseek(int file, off_t ptr, int dir)
{
    return 0;
}

}
