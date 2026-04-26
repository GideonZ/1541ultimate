/*
 * update.cc (for Ultimate-II+L)
 *
 *  Created on: September 5, 2022
 *      Author: gideon
 */

#include "../update_u2p/update_common.h"
#include "wifi_cmd.h"

extern uint32_t _u2p_ecp5_impl1_bit_start;
extern uint32_t _u2p_ecp5_impl1_bit_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;


extern uint32_t _bootloader_bin_start;
extern uint32_t _bootloader_bin_size;
extern uint32_t _partition_table_bin_start;
extern uint32_t _partition_table_bin_size;
extern uint32_t _bridge_bin_start;
extern uint32_t _bridge_bin_size;
extern uint8_t _1541_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1581_bin_start;
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
    setup("\033\025** Ultimate II+L Updater **\n\033\037");

    Flash *flash2 = get_flash();
    flash2->protect_disable();
    check_flash_disk();

    if(user_interface->popup("Flash " APPL_VERSION "?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
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

        flash_buffer(flash2, screen, FLASH_ID_BOOTFPGA, &_u2p_ecp5_impl1_bit_start, &_u2p_ecp5_impl1_bit_end, "", "Runtime FPGA");
        flash_buffer(flash2, screen, FLASH_ID_APPL,     &_ultimate_app_start,     &_ultimate_app_end,  APPL_VERSION, "Ultimate Application");

        write_protect(flash2, 2048);
    }
    reset_config(flash2);

    wifi_command_init();
    uint16_t major, minor;
    char moduleName[32];
    BaseType_t module_detected;
    module_detected = wifi_detect(&major, &minor, moduleName, 32);
    module_detected = wifi_detect(&major, &minor, moduleName, 32); // second time should pass
    if (module_detected != pdTRUE) {
        esp32.uart->SetBaudRate(115200); // maybe it's an old version?
        module_detected = wifi_detect(&major, &minor, moduleName, 32);
        module_detected = wifi_detect(&major, &minor, moduleName, 32); // second time should pass
    }

    if (module_detected == pdTRUE) {
        console_print(screen, "WiFi module detected:\n\e4%s\037\n", moduleName);
    }

    if (esp32.Download() == 0) {
        if(user_interface->popup("Want to update the WiFi Module?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            uint32_t total_size = (uint32_t)&_bootloader_bin_size + (uint32_t)&_partition_table_bin_size + (uint32_t)&_bridge_bin_size;
            user_interface->show_progress("Flashing ESP32", total_size / 1024);
            int status = 0;
            status = esp32.Flash((uint8_t *)&_bootloader_bin_start, 0x000000, (uint32_t)&_bootloader_bin_size, status_callback, user_interface);
            if (status == 0) {
                status = esp32.Flash((uint8_t *)&_partition_table_bin_start, 0x008000, (uint32_t)&_partition_table_bin_size, status_callback, user_interface);
            }
            if (status == 0) {
                status = esp32.Flash((uint8_t *)&_bridge_bin_start, 0x010000, (uint32_t)&_bridge_bin_size, status_callback, user_interface);
            }
            user_interface->hide_progress();
            printf("Flashing ESP32 Status: %d.\n", status);
            if (status == 0) {
                user_interface->popup("Flashing ESP32 Success!", BUTTON_OK);
            } else {
                user_interface->popup("Flashing ESP32 Failed!", BUTTON_OK);
            }
            esp32.EnableRunMode();
        }
    } else {
        console_print(screen, "WiFi module not detected.\n");
    }

    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
    return 0;
}
