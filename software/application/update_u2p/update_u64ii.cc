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
#include "controller_probe.h"
#include "../esp32_diagnostic/platform.h"

extern uint32_t _u64e2_50t_swp_start;
extern uint32_t _u64e2_50t_swp_end;
extern uint32_t _u64e2_100t_swp_start;
extern uint32_t _u64e2_100t_swp_end;

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
extern const char _rest_api_openapi_u64_yaml_start[];
extern const char _rest_api_openapi_u64_yaml_end[1];
extern const char _index_html_start[];
extern const char _index_html_end[1];


static void status_callback(void *user)
{
    UserInterface *ui = (UserInterface *)user;
    ui->update_progress(NULL, 1);
}

#if UPDATE_FPGA_TYPE
#include "color_timings.h"
#include "hdmi_scan.h"

extern "C" void SetVideoPll(t_video_mode mode, int ppm);
extern "C" void ResetHdmiPll(void);

// Network recovery starts this updater straight from the bootloader, so no
// application has set up the video output. NTSC, because HDMI then runs at
// 640x480, which every display takes. The steps are those of
// U64Config::effectuate_settings() for that mode.
static void init_video(void)
{
    const t_video_color_timing *ct = color_timings[(int)e_NTSC_60];
    C64_PHASE_INCR = ct->phase_inc;
    SetVideoPll(e_NTSC_60, 0);
    C64_VIDEOFORMAT = ct->mode_bits;
    SetVideoMode1080p(e_NTSC_60, e_480p_576p);
    ResetHdmiPll();
}
#endif

bool update_esp32_impl(void)
{
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
        return status == 0;
    } else {
        user_interface->popup("Could not set ESP32 to download mode", BUTTON_OK);
        return false;
    }
}

// Runs before setup(): mounting an unformatted flash disk can itself write.
// No UNKNOWN/equal/newer controller is silently combined with new native code.
static bool controller_preflight(void)
{
    static ControllerProbeHardware probe_hardware;
    ControllerIdentity identity;
    diagnostic_line(1, "COORDINATED UPDATE: PREFLIGHT");
    diagnostic_line(2, "NO PERSISTENT WRITES BEFORE ACCEPTANCE");
    const uint8_t board = getFpgaType();
    if ((board != 2 && board != 3) ||
        (UPDATE_FPGA_TYPE && board != UPDATE_FPGA_TYPE)) {
        diagnostic_line(4, "STOP: UNSUPPORTED FPGA");
        return false;
    }
#if UPDATE_FPGA_TYPE != 3
    if (board == 2 && calc_checksum((uint8_t *)&_u64e2_50t_swp_start, (uint8_t *)&_u64e2_50t_swp_end) != CHK_u64e2_50t_swp) return false;
#endif
#if UPDATE_FPGA_TYPE != 2
    if (board == 3 && calc_checksum((uint8_t *)&_u64e2_100t_swp_start, (uint8_t *)&_u64e2_100t_swp_end) != CHK_u64e2_100t_swp) return false;
#endif
    if (calc_checksum((uint8_t *)&_ultimate_app_start, (uint8_t *)&_ultimate_app_end) != CHK_ultimate_app ||
        calc_checksum((uint8_t *)&_bootloader_bin_start, (uint8_t *)&_bootloader_bin_start + (uintptr_t)&_bootloader_bin_size) != CHK_bootloader_bin ||
        calc_checksum((uint8_t *)&_partition_table_bin_start, (uint8_t *)&_partition_table_bin_start + (uintptr_t)&_partition_table_bin_size) != CHK_partition_table_bin ||
        calc_checksum((uint8_t *)&_u64ctrl_bin_start, (uint8_t *)&_u64ctrl_bin_start + (uintptr_t)&_u64ctrl_bin_size) != CHK_u64ctrl_bin) {
        diagnostic_line(4, "STOP: BAD PACKAGE CHECKSUM");
        return false;
    }
    // Updater constructors already own the UART. This is one planned module
    // startup, not a read-only query; the standalone diagnostic never does this.
    esp32.EnableRunMode();
    vTaskDelay(pdMS_TO_TICKS(2000));
    wifi_command_init();
    esp32.uart->EnableIRQ(false);
    portENTER_CRITICAL();
    const ControllerProbeResult result = controller_probe(probe_hardware, identity);
    portEXIT_CRITICAL();
    const ControllerInstallDecision decision = controller_install_decision(
        result == ControllerProbeResult::Identified, identity, IDENT_MAJOR, IDENT_MINOR);
    diagnostic_line(5, identity.name);
    if (decision != ControllerInstallDecision::Older) {
        diagnostic_line(4, decision == ControllerInstallDecision::EqualUnverified ? "STOP: EQUAL VERSION, IMAGE UNVERIFIED" :
            decision == ControllerInstallDecision::Newer ? "STOP: NEWER CONTROLLER - NO DOWNGRADE" : "STOP: CONTROLLER IDENTITY UNKNOWN");
        return false;
    }
    diagnostic_line(4, "OLDER CONTROLLER: UPDATE ELIGIBLE");
    return true;
}

void do_update(void)
{
#if UPDATE_FPGA_TYPE
    init_video();
#endif
    setup("\033\025** Ultimate 64 Elite-II Updater **\n\033\037");
    usb2.initHardware();

    Flash *flash2 = get_flash();
    console_print(screen, "\033\024Detected Flash: %s\n", flash2->get_type_string());

    uint8_t fpgatype_id = getFpgaType();
    const char *fpga_types[] = { "5CEBA2", "5CEBA4", "XC7A50T", "XC7A100T" };
    const char *fpgaType = fpgatype_id < 4 ? fpga_types[fpgatype_id] : "Unknown";
    console_print(screen, "Detected FPGA Type: %s.\nBoard Revision: %s\n\033\037\n", fpgaType, getBoardRevision());

    /* Extra check on the loaded images */
    const char *check_error = "\033\022\nBAD...\n\nNot flashing.\n";
    const char *check_ok = "\033\025OK!\n\033\037";

    console_print(screen, "\033\027Checking checksums of loaded images..\n");

    // An updater that carries one image falls through to default on the other
    // FPGA, and flashes nothing.
    console_print(screen, "\033\037Checksum of FPGA image:   ");
    switch(fpgatype_id) {
#if UPDATE_FPGA_TYPE != 3
    case 2:
        if(calc_checksum((uint8_t *)&_u64e2_50t_swp_start, (uint8_t *)&_u64e2_50t_swp_end) == CHK_u64e2_50t_swp) {
            console_print(screen, check_ok);
        } else {
            console_print(screen, check_error);
            while(1);
        }
        break;
#endif
#if UPDATE_FPGA_TYPE != 2
    case 3:
        if(calc_checksum((uint8_t *)&_u64e2_100t_swp_start, (uint8_t *)&_u64e2_100t_swp_end) == CHK_u64e2_100t_swp) {
            console_print(screen, check_ok);
        } else {
            console_print(screen, check_error);
            while(1);
        }
        break;
#endif
    default:
        user_interface->popup("FPGA Type not supported.", BUTTON_OK);
        goto off;
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
        write_api_files(_rest_api_openapi_u64_yaml_start, _rest_api_openapi_u64_yaml_end);

        flash2->protect_disable();
        switch(fpgatype_id) {
#if UPDATE_FPGA_TYPE != 3
        case 2:
            if (!flash_buffer_at(flash2, screen, 0x000000, false, &_u64e2_50t_swp_start, &_u64e2_50t_swp_end, "V1.0", "Runtime FPGA") ||
                !flash_buffer_at(flash2, screen, 0x220000, false, &_ultimate_app_start, &_ultimate_app_end, "V1.0", "Ultimate Application")) {
                flash2->protect_enable();
                goto failed;
            }
            write_protect(flash2, 4096);
            break;
#endif
#if UPDATE_FPGA_TYPE != 2
        case 3:
            if (!flash_buffer_at(flash2, screen, 0x000000, false, &_u64e2_100t_swp_start, &_u64e2_100t_swp_end, "V1.0", "Runtime FPGA") ||
                !flash_buffer_at(flash2, screen, 0x3C0000, false, &_ultimate_app_start, &_ultimate_app_end, "V1.0", "Ultimate Application")) {
                flash2->protect_enable();
                goto failed;
            }
            write_protect(flash2, 4096); // cannot protect just 5.5 MB... :-(
            break;
#endif
        default:
            user_interface->popup("FPGA Type not supported.", BUTTON_OK);
            goto off;
        }
#ifndef NO_ESP
        if (!update_esp32_impl()) {
            goto failed;
        }
#endif
        reset_config(flash2);
    }
    goto off;
failed:
    console_print(screen, "Coordinated update FAILED. Stop; preserve this result.\n");
    for (;;) vTaskDelay(200);
off:
    esp32.EnableRunMode();
    vTaskDelay(200);
    wifi_command_init();
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
#ifdef NO_ESP
    // A coordinated candidate must never omit its required controller update.
    diagnostic_line(4, "STOP: CONTROLLER PAYLOAD DISABLED");
    for (;;) vTaskDelay(200);
#else
    if (!controller_preflight()) {
        diagnostic_line(7, "NOT INSTALLED. NO AUTOMATIC RETRY.");
        for (;;) vTaskDelay(200);
    }
#endif
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
