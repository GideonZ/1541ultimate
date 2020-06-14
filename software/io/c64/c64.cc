/*
 * c64.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *  Copyright (C) 2011 Daniel Kahlin <daniel@kahlin.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "integer.h"
#include "dump_hex.h"
#include "c64.h"
#include "u64.h"
#include "flash.h"
#include "keyboard_c64.h"
#include "config.h"

#ifndef CMD_IF_SLOT_BASE
#define CMD_IF_SLOT_BASE       *((volatile uint8_t *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE     *((volatile uint8_t *)(CMD_IF_BASE + 0x1))
#endif

#ifndef NO_FILE_ACCESS
#include "filemanager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "filetype_crt.h"
#endif

int ultimatedosversion = 0;
bool allowUltimateDosDateSet = false;
#ifndef RECOVERYAPP
extern bool connectedToU64;
#endif

/* Configuration */
const char *cart_mode[] = { "None",
                      "Final Cart III",
                      "Action Replay V4.2 PAL",
                      "Action Replay V6.0 PAL",
                      "Retro Replay V3.8q PAL",
                      "SuperSnapshot V5.22 PAL",
                      "TAsm / CodeNet PAL",
#ifndef U64
                      "Action Replay V5.0 NTSC",
#endif
                      "Retro Replay V3.8y NTSC",
                      "SuperSnapshot V5.22 NTSC",
                      "TAsm / CodeNet NTSC",

                      "Epyx Fastloader",
                      "KCS Power Cartridge",

                      "GeoRAM",
                      "Custom 8K ROM",
                      "Custom 16K ROM",
/*
                      "Custom System 3 ROM",
                      "Custom Ocean V1 ROM",
                      "Custom Ocean V2/T2 ROM",
                      "Custom Final III ROM",
*/
                      "Custom Retro Replay ROM",
                      "Custom Snappy ROM",
                      "Custom KCS ROM",
                      "Custom Final V1/V2 ROM",
#ifndef U64
                      "Custom C128 External ROM",
#endif
                      "Custom CRT"
                   };

cart_def cartridges[] = { { 0x00,               0x000000, 0x00000,  0x00 | CART_REU | CART_ETH },
                          { FLASH_ID_FINAL3,    0x000000, 0x10000,  0x04 },
                          { FLASH_ID_AR5PAL,    0x000000, 0x08000,  0x07 },
                          { FLASH_ID_AR6PAL,    0x000000, 0x08000,  0x07 },
                          { FLASH_ID_RR38PAL,   0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_SS5PAL,    0x000000, 0x10000,  0x05 | CART_REU },
                          { FLASH_ID_TAR_PAL,   0x000000, 0x10000,  0x06 | CART_ETH },

#ifndef U64
                          { FLASH_ID_AR5NTSC,   0x000000, 0x08000,  0x07 },
#endif
                          { FLASH_ID_RR38NTSC,  0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_SS5NTSC,   0x000000, 0x10000,  0x05 | CART_REU },
                          { FLASH_ID_TAR_NTSC,  0x000000, 0x10000,  0x06 | CART_ETH },

                          { FLASH_ID_EPYX,      0x000000, 0x02000,  0x0E },
                          { FLASH_ID_KCS,       0x000000, 0x04000,  0x10 },

                          { 0x00,               0x000000, 0x04000,  0x15 | CART_UCI }, // GeoRam
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x02000,  0x01 | CART_REU | CART_ETH },
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x04000,  0x02 | CART_REU | CART_ETH },
/*
                          { 0x00,               0x000000, 0x80000,  0x08 | CART_REU },
                          { 0x00,               0x000000, 0x80000,  0x0A | CART_REU },
                          { 0x00,               0x000000, 0x80000,  0x0B | CART_REU },
                          { 0x00,               0x000000, 0x10000,  0x04 },
*/
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x10000,  0x05 | CART_REU },
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x04000,  0x10 },
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x04000,  0x11 },
#ifndef U64
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x08000,  0x18 | CART_REU | CART_ETH },
#endif
                          { FLASH_ID_CUSTOM_ROM,0x000000, 0x00000,  0x00 }
 };
                          
static const char *reu_size[] = { "128 KB", "256 KB", "512 KB", "1 MB", "2 MB", "4 MB", "8 MB", "16 MB" };
static const char *reu_offset[] = { "0 KB", "128 KB", "256 KB", "512 KB", "1 MB", "2 MB", "4 MB", "8 MB", "16 MB" };
static const char *rom_sel[] = { "Factory", "Original", "Alternative" };
#if U64
static const char *rom_sel_ker[] = { "Factory", "Original", "Alternative", "Alt. 2", "Alt. 3" };
#elif CLOCK_FREQ == 62500000
static const char *rom_sel_ker[] = { "Disabled", "Alternative", "Alt. 2" };
#endif
static const char *buttons[] = { "Reset|Menu|Freezer", "Freezer|Menu|Reset" };
static const char *timing1[] = { "20ns", "40ns", "60ns", "80ns", "100ns", "120ns", "140ns", "160ns" };
static const char *timing2[] = { "16ns", "32ns", "48ns", "64ns", "80ns", "96ns", "112ns", "128ns" };
static const char *timing3[] = { "15ns", "30ns", "45ns", "60ns", "75ns", "90ns", "105ns", "120ns" };
static const char *ultimatedos[] = { "Disabled", "Enabled", "Enabled (v1.1)", "Enabled (v1.0)" };
static const char *fc3mode[] = { "Unchanged", "Desktop", "BASIC" };
static const char *cartmodes[] = { "Auto", "Internal", "External", "Manual" };
static const char *bus_modes[] = { "Quiet", "Writes", "CPU", "CPU/VIC", "VIC" };
static const uint8_t bus_mode_values[] = { 0x00, 0x01, 0x03, 0x07, 0x04 };
static const char *bus_sharing[] = { "Internal", "External", "Both" };

struct t_cfg_definition c64_config[] = {
#if U64
    { CFG_C64_CART,        CFG_TYPE_ENUM, "Cartridge",                    "%s", cart_mode,  0, 19, 0 },
    { CFG_C64_CART_PREF,   CFG_TYPE_ENUM, "Cartridge Preference",         "%s", cartmodes,  0,  3, 0 },
    { CFG_BUS_MODE,        CFG_TYPE_ENUM, "Bus Operation Mode",           "%s", bus_modes,    0,  4, 0 },
    { CFG_BUS_SHARING_ROM, CFG_TYPE_ENUM, "Bus Sharing - ROMs",           "%s", bus_sharing,  0,  2, 2 },
    { CFG_BUS_SHARING_IO,  CFG_TYPE_ENUM, "Bus Sharing - I/O",            "%s", bus_sharing,  0,  2, 2 },
    { CFG_BUS_SHARING_IRQ, CFG_TYPE_ENUM, "Bus Sharing - Interrupts",     "%s", bus_sharing,  0,  2, 2 },
#else
    { CFG_C64_CART,     CFG_TYPE_ENUM,   "Cartridge",                    "%s", cart_mode,  0, 21, 4 },
#endif
    { CFG_C64_FC3MODE,  CFG_TYPE_ENUM,   "Final Cartridge 3 Mode",       "%s", fc3mode,    0,  2, 0 },
    { CFG_C64_FASTRESET,CFG_TYPE_ENUM,   "Fast Reset",                   "%s", en_dis,     0,  1, 0 },
#if U64
    { CFG_C64_ALT_KERN, CFG_TYPE_ENUM,   "Alternate Kernal",             "%s", rom_sel_ker,0,  4, 0 },
    { CFG_C64_ALT_BASI, CFG_TYPE_ENUM,   "Alternate Basic",              "%s", rom_sel,    0,  2, 0 },
    { CFG_C64_ALT_CHAR, CFG_TYPE_ENUM,   "Alternate Chargen",            "%s", rom_sel,    0,  2, 0 },
#elif CLOCK_FREQ == 62500000
    { CFG_C64_ALT_KERN, CFG_TYPE_ENUM,   "Alternate Kernal",             "%s", rom_sel_ker,0,  2, 0 },
#else
    { CFG_C64_ALT_KERN, CFG_TYPE_ENUM,   "Alternate Kernal",             "%s", en_dis,     0,  1, 0 },
#endif
    { CFG_C64_REU_EN,   CFG_TYPE_ENUM,   "RAM Expansion Unit",           "%s", en_dis,     0,  1, 0 },
    { CFG_C64_REU_SIZE, CFG_TYPE_ENUM,   "REU Size",                     "%s", reu_size,   0,  7, 4 },
    { CFG_C64_REU_PRE,  CFG_TYPE_ENUM,   "REU Preload",                  "%s", en_dis,     0,  1, 0 },
    { CFG_C64_REU_IMG,  CFG_TYPE_STRING, "REU Preload Image",            "%s", NULL,       0, 31, (int)"/Usb0/preload.reu" },
    { CFG_C64_REU_OFFS, CFG_TYPE_ENUM,   "REU Preload Offset",           "%s", reu_offset, 0,  8, 0 }, 
    { CFG_C64_MAP_SAMP, CFG_TYPE_ENUM,   "Map Ultimate Audio $DF20-DFFF","%s", en_dis,     0,  1, 0 },
    { CFG_C64_DMA_ID,   CFG_TYPE_VALUE,  "DMA Load Mimics ID:",          "%d", NULL,       8, 31, 8 },
#ifndef U64
    { CFG_C64_SWAP_BTN, CFG_TYPE_ENUM,   "Button order",                 "%s", buttons,    0,  1, 1 },
#endif
#if CLOCK_FREQ == 62500000
    { CFG_C64_TIMING,   CFG_TYPE_ENUM,   "CPU Addr valid after PHI2",    "%s", timing2,    0,  7, 5 },
    { CFG_C64_PHI2_REC, CFG_TYPE_ENUM,   "PHI2 edge recovery",           "%s", en_dis,     0,  1, 0 },
#elif CLOCK_FREQ == 50000000
    { CFG_C64_TIMING,   CFG_TYPE_ENUM,   "CPU Addr valid after PHI2",    "%s", timing1,    0,  7, 3 },
    { CFG_C64_PHI2_REC, CFG_TYPE_ENUM,   "PHI2 edge recovery",           "%s", en_dis,     0,  1, 0 },
#endif
    { CFG_CMD_ENABLE,   CFG_TYPE_ENUM,   "Command Interface",            "%s", ultimatedos,0,  3, 0 },
    { CFG_CMD_ALLOW_WRITE, CFG_TYPE_ENUM,   "UltiDOS: Allow SetDate",    "%s", en_dis,     0,  1, 0 },
#if DEVELOPER > 0
    { CFG_C64_DO_SYNC,  CFG_TYPE_ENUM,   "Perform VIC sync at DMA RUN",  "%s", en_dis,     0,  1, 0 },
#endif
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

extern uint8_t _chars_bin_start;

static unsigned char fastresetPatch[] = { 0xa2, 0x00, 0xa0, 0xa0, 0xad, 0x00, 0x80, 0x49, 0xff, 0x8d, 0x00, 0x80, 0xcd, 0x00, 0x80, 0xf0, 0x02, 0xa0, 0x80, 0x4c, 0x8c, 0xfd };
static unsigned char fastresetOrg[] =   { 0xe6, 0xc2, 0xb1, 0xc1, 0xaa, 0xa9, 0x55, 0x91, 0xc1, 0xd1, 0xc1, 0xd0, 0x0f, 0x2a, 0x91, 0xc1, 0xd1, 0xc1, 0xd0, 0x08, 0x8a, 0x91, 0xc1, 0xc8, 0xd0, 0xe8, 0xf0, 0xe4, 0x98, 0xaa, 0xa4, 0xc2, 0x18, 0x20, 0x2D, 0xfe, 0xa9, 0x08, 0x8d, 0x82, 0x02, 0xa9, 0x04, 0x8d, 0x88, 0x02, 0x60 };

C64::C64()
{
    flash = get_flash();

    register_store(0x43363420, "C64 and Cartridge Settings", c64_config);

    cfg->set_change_hook(CFG_C64_CART_PREF, C64::setCartPref);
    setCartPref(cfg->find_item(CFG_C64_CART_PREF));

    // char_set = new BYTE[CHARSET_SIZE];
    // flash->read_image(FLASH_ID_CHARS, (void *)char_set, CHARSET_SIZE);
    char_set = (uint8_t *) &_chars_bin_start;
    keyb = new Keyboard_C64(this, &CIA1_DPB, &CIA1_DPA);
    screen = new Screen_MemMappedCharMatrix((char *) C64_SCREEN, (char *) C64_COLORRAM, 40, 25);

    C64_STOP_MODE = STOP_COND_FORCE;
    C64_MODE = MODE_NORMAL;
//    C64_STOP = 0;
    isFrozen = false;
//    C64_MODE = C64_MODE_RESET;
    buttonPushSeen = false;
    client = 0;
    available = false;

    if (phi2_present()) {
        init();
    }
#ifndef U64
#ifdef OS
    else {
        printf("No PHI2 clock detected.. Stand alone mode. Stopped = %d\n", C64_STOP);
        xTaskCreate(C64 :: init_poll_task, "C64 Init poll task", 500, this, 2, NULL);
    }
#endif
#else
    else {
        printf("U64, and no PHI2?  Something is seriously wrong!!\n");
    }
#endif
}

int C64 :: setCartPref(ConfigItem *item)
{
    // This is only a UI thing
    if (item->getValue() == 3) { // If manual, enable the other settings
        item->store->enable(CFG_BUS_SHARING_ROM);
        item->store->enable(CFG_BUS_SHARING_IO);
        item->store->enable(CFG_BUS_SHARING_IRQ);
    } else {
        item->store->disable(CFG_BUS_SHARING_ROM);
        item->store->disable(CFG_BUS_SHARING_IO);
        item->store->disable(CFG_BUS_SHARING_IRQ);
    }
    return 1;
}

void C64 :: init(void)
{
    effectuate_settings();

    force_cart = 0x80;

    if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
        init_system_roms();
        init_cartridge();
    } else if (cfg->get_value(CFG_C64_CART) || cfg->get_value(CFG_C64_ALT_KERN)) {
        init_cartridge();
    }
    available = true;
}

void C64 :: start(void)
{
    C64_STOP = 0;
}

#ifdef OS
void C64 :: init_poll_task(void *a)
{
    C64 *obj = (C64 *)a;
    while(!phi2_present()) {
        ioWrite8(UART_DATA, '\\');
        vTaskDelay(100);
    }
    obj->init();
    obj->start(); // assumed that this will only happen on a U2/U2+ inside a U64
    vTaskDelete(NULL);
}
#endif

C64::~C64()
{
    if (isFrozen) {
        restore_io();
        resume();
        isFrozen = false;
    }

    delete keyb;

    if (screen)
        delete screen;
}

void C64 :: resetConfigInFlash(int page)
{
    uint8_t kern = cfg->get_value(CFG_C64_ALT_KERN);
#if U64
    uint8_t basic = cfg->get_value(CFG_C64_ALT_BASI);
    uint8_t chars = cfg->get_value(CFG_C64_ALT_CHAR);

    if (kern > 2) {
        kern = 1;
    }
#endif
    cfg->reset();

    cfg->set_value(CFG_C64_ALT_KERN, kern);
#if U64
    cfg->set_value(CFG_C64_ALT_BASI, basic);
    cfg->set_value(CFG_C64_ALT_CHAR, chars);
#endif
    if (cfg->is_flash_stale()) {
        cfg->write();
    }
}

C64 *C64 :: getMachine(void)
{
    static C64 *c64 = NULL;
    if (!c64) {
        c64 = new C64();
    }
    return c64;
}

void C64::effectuate_settings(void)
{
    // this function will set the parameters that can be switched while the machine is running
    // init_cartridge is called only at reboot, and will cause the C64 to reset.
    C64_SWAP_CART_BUTTONS = cfg->get_value(CFG_C64_SWAP_BTN);

#if U64
    ConfigureU64SystemBus();
#endif
    cart_def *def;
    int cart = cfg->get_value(CFG_C64_CART);
    def = &cartridges[cart];
    set_emulation_flags(def);
}

void C64::new_system_rom(uint8_t flashId)
{
    switch(flashId) {
    case FLASH_ID_ORIG_BASIC:
        cfg->set_value(CFG_C64_ALT_BASI, 1);
        break;
    case FLASH_ID_BASIC_ROM:
        cfg->set_value(CFG_C64_ALT_BASI, 2);
        break;
    case FLASH_ID_ORIG_KERNAL:
        cfg->set_value(CFG_C64_ALT_KERN, 1);
        break;
    case FLASH_ID_KERNAL_ROM:
        cfg->set_value(CFG_C64_ALT_KERN, 2);
        break;
    case FLASH_ID_ORIG_CHARGEN:
        cfg->set_value(CFG_C64_ALT_CHAR, 1);
        break;
    case FLASH_ID_CHARGEN_ROM:
        cfg->set_value(CFG_C64_ALT_CHAR, 2);
        break;
    }
    cfg->write();
    init_system_roms();
}

void C64::set_emulation_flags(cart_def *def)
{
    C64_REU_ENABLE = 0;
    C64_SAMPLER_ENABLE = 0;
    if (getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
        CMD_IF_SLOT_ENABLE = 0;
    }

    C64_REU_SIZE = cfg->get_value(CFG_C64_REU_SIZE);
    if (def->type & CART_REU) {
        if (cfg->get_value(CFG_C64_REU_EN)) {
            printf("Enabling REU!!\n");
            C64_REU_ENABLE = 1;
        }
        // C64_REU_SIZE = cfg->get_value(CFG_C64_REU_SIZE);
        if (getFpgaCapabilities() & CAPAB_SAMPLER) {
            printf("Sampler found in FPGA... IO map: ");
            if (cfg->get_value(CFG_C64_MAP_SAMP)) {
                printf("Enabled!\n");
                C64_SAMPLER_ENABLE = 1;
            } else {
                printf("disabled.\n");
            }
        }
    }
    if (def->type & (CART_REU | CART_UCI)) {
        if (getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
            int choice = cfg->get_value(CFG_CMD_ENABLE);
            CMD_IF_SLOT_ENABLE = !!choice;
            ultimatedosversion = choice;
#ifdef RECOVERYAPP
            CMD_IF_SLOT_BASE = 0x47; // $$DF1C
#else
            CMD_IF_SLOT_BASE = connectedToU64 ? 0x46 : 0x47; // $DF18 when 1541 U2(+) connected to U64, $DF1C else.
#endif
            choice = cfg->get_value(CFG_CMD_ALLOW_WRITE);
            allowUltimateDosDateSet = choice;
        }
    }
    C64_ETHERNET_ENABLE = 0;
    if (def->type & CART_ETH) {
        if (cfg->get_value(CFG_C64_ETH_EN)) {
            C64_ETHERNET_ENABLE = 1;
        }
    }

    int recovery = cfg->get_value(CFG_C64_PHI2_REC);
    if (recovery >= 0) {
        C64_PHI2_EDGE_RECOVER = cfg->get_value(CFG_C64_PHI2_REC);
        C64_TIMING_ADDR_VALID = cfg->get_value(CFG_C64_TIMING);
    } else { // U64
        C64_PHI2_EDGE_RECOVER = 0;
        C64_TIMING_ADDR_VALID = 5;
    }
}

bool C64::exists(void)
{
    if (phi2_present()) {
        return true;
    }
    C64_STOP_MODE = STOP_COND_FORCE;
    C64_MODE = MODE_NORMAL;
    C64_STOP = 0;
    return false;
}

void C64::determine_d012(void)
{
    uint8_t b;
    // pre-condition: C64 is already in the stopped state, and IRQ registers have already been saved
    //                C64 IRQn to SD-IRQ is not enabled.

    VIC_REG(26) = 0x01; // interrupt enable = raster only
    VIC_REG(25) = 0x81; // clear raster interrupts

    // poll until VIC interrupt occurs

    vic_d012 = 0;
    vic_d011 = 0;

    b = 0;
    while (!(VIC_REG(25) & 0x81)) {
        if (!ioRead8(ITU_TIMER)) {
            ioWrite8(ITU_TIMER, 200);
            b++;
            if (b == 40)
                break;
        }
    }

    vic_d012 = VIC_REG(18); // d012
    vic_d011 = VIC_REG(17); // d011

    VIC_REG(26) = 0x00; // disable interrutps
    VIC_REG(25) = 0x81; // clear raster interrupt
}

void C64 :: goUltimax(void)
{
    C64_MODE = MODE_ULTIMAX;
}

void C64 :: hard_stop(void)
{
    if (!(C64_STOP & C64_HAS_STOPPED)) {
        C64_STOP_MODE = STOP_COND_FORCE;
        C64_STOP = 1;
        // wait until it is stopped (it always will!)
        while (!(C64_STOP & C64_HAS_STOPPED))
            ;
    }
    wait_10us(2);
}

void C64::stop(bool do_raster)
{
    int b, w;

//    bool do_raster = true;

    // stop the c-64
    stop_mode = 0;
    raster = 0;
    raster_hi = 0;
    vic_irq_en = 0;
    vic_irq = 0;

    // First we try to break on the occurence of a bad line.
    C64_STOP_MODE = STOP_COND_BADLINE;

    //printf("Stop.. ");

    if (do_raster) {
        // wait maximum for 25 ms (that is 1/40 of a second; there should be a bad line in this time frame
        ioWrite8(ITU_TIMER, 200); // 1 ms

        // request to stop the c-64
        C64_STOP = 1;
        //printf(" condition set.\n");

        for (b = 0; b < 25;) {
            if (C64_STOP & C64_HAS_STOPPED) {  // has the C64 stopped already?
                //            VIC_REG(48) = 0;  // switch to slow mode (C128)
                // enter ultimax mode, so we can be sure that we can access IO space!
                goUltimax();

                printf("Ultimax set.. Now reading registers..\n");
                raster = VIC_REG(18);
                raster_hi = VIC_REG(17);
                vic_irq_en = VIC_REG(26);
                vic_irq = VIC_REG(25);
                stop_mode = 1;
                printf("Mode=1\n");
                break;
            }
            if (!ioRead8(ITU_TIMER)) {
                ioWrite8(ITU_TIMER, 200); // 1 ms
                b++;
            }
        }
    }

    //printf("Raster failed.\n");

    // If that fails, the screen must be blanked or so, so we try to break upon a safe R/Wn sequence
    if (!stop_mode) {
        C64_STOP_MODE = STOP_COND_RWSEQ;

        // request to stop the c-64
        C64_STOP = 1; // again, in case we had to stop without trying raster first

        ioWrite8(ITU_TIMER, 200); // 1 ms

        for (w = 0; w < 100;) { // was 1500
            if (C64_STOP & C64_HAS_STOPPED) {
                stop_mode = 2;
                break;
            }
//            printf("#%b^%b ", ITU_TIMER, w);
            if (!ioRead8(ITU_TIMER)) {
                ioWrite8(ITU_TIMER, 200); // 1 ms
                w++;
            }
        }
    }
//    printf("SM=%d.", stop_mode);

    // If that fails, the CPU must have disabled interrupts, and be in a polling read loop
    // since no write occurs!
    if (!stop_mode) {
        C64_STOP_MODE = STOP_COND_FORCE;

        // wait until it is stopped (it always will!)
        while (!(C64_STOP & C64_HAS_STOPPED))
            ;

        stop_mode = 3;
    }

    if (do_raster) {  // WRONG parameter actually, but this function is only called in two places:
                      // entering the menu, and dma load. menu calls it with true, dma load with false.... :-S
                      // TODO: Clean up
        switch (stop_mode) {
        case 1:
            printf("Frozen on Bad line. Raster = %02x. VIC Irq Enable: %02x. Vic IRQ: %02x\n", raster, vic_irq_en, vic_irq);
            if (vic_irq_en & 0x01) { // Raster interrupt enabled
                determine_d012();
                printf("Original d012/11 content = %02x %02x.\n", vic_d012, vic_d011);
            } else {
                vic_d011 = VIC_REG(17); // for all other bits
            }
            break;
        case 2:
            printf("Frozen on R/Wn sequence.\n");
            break;
        case 3:
            printf("Hard stop!!!\n");
            break;
        default:
            printf("Internal error. Should be one of the cases.\n");
        }
    }
    //printf("@");
}

void C64::resume(void)
{
    int dummy = 0;
    uint8_t rast_lo;
    uint8_t rast_hi;

    if (!raster && !raster_hi) {
        rast_lo = 0;
        rast_hi = 0;
    } else {
        rast_lo = raster - 1;
        rast_hi = raster_hi & 0x80;
        if (!raster)
            rast_hi = 0;
    }

    // either resume in mode 0 or in mode 3  (never on R/Wn sequence of course!)
    if (stop_mode == 1) {
        // this can only occur when we exit from the menu, so we are still
        // in ultimax mode, so we can see the VIC here.
        if (vic_irq & 0x01) { // was raster flag set when we stopped? then let's set it again
            VIC_REG(26) = 0x81;
            VIC_REG(18) = rast_lo;
            VIC_REG(17) = rast_hi;
            while ((VIC_REG(18) != rast_lo) && ((VIC_REG(17) & 0x80) != rast_hi)) {
                if (!ioRead8(ITU_TIMER)) {
                    ioWrite8(ITU_TIMER, 200); // 1 ms
                    dummy++;
                    if (dummy == 40)
                        break;
                }
            }
            VIC_REG(18) = vic_d012;
            VIC_REG(17) = vic_d011;
        } else {
            while ((VIC_REG(18) != rast_lo) && ((VIC_REG(17) & 0x80) != rast_hi)) {
                if (!ioRead8(ITU_TIMER)) {
                    ioWrite8(ITU_TIMER, 200); // 1 ms
                    dummy++;
                    if (dummy == 40)
                        break;
                }
            }
            VIC_REG(18) = vic_d012;
            VIC_REG(17) = vic_d011;
            VIC_REG(25) = 0x81; // clear raster irq
        }
        VIC_REG(26) = vic_irq_en;

        raster = VIC_REG(18);
        vic_irq_en = VIC_REG(26);
        vic_irq = VIC_REG(25);

        C64_STOP_MODE = STOP_COND_BADLINE;

        printf("Normal!!\n");
        // return to normal mode
        C64_MODE = MODE_NORMAL;

        // dummy cycle
        dummy = VIC_REG(0);

        // un-stop the c-64
        C64_STOP = 0;

        printf("Resumed on Bad line. Raster = %02x. VIC Irq Enable: %02x. Vic IRQ: %02x\n", raster, vic_irq_en, vic_irq);
    } else {
        C64_STOP_MODE = STOP_COND_FORCE;
        // un-stop the c-64
        // return to normal mode
        C64_MODE = MODE_NORMAL;

        C64_STOP = 0;
    }
}

void C64::reset(void)
{
    C64_MODE = MODE_NORMAL;
    C64_MODE = C64_MODE_RESET;
    ioWrite8(ITU_TIMER, 20);
    while (ioRead8(ITU_TIMER))
        ;
    C64_MODE = C64_MODE_UNRESET;
}

/*
 -------------------------------------------------------------------------------
 freeze (split in subfunctions)
 ======
 Abstract:

 Stops the C64. Backups the resources the cardridge is allowed to use.
 Clears the screen and sets the default colours.

 -------------------------------------------------------------------------------
 */
void C64::backup_io(void)
{
    int i;
    // enter ultimax mode, as this might not have taken place already!
    goUltimax();

    // back up VIC registers
    for (i = 0; i < NUM_VICREGS; i++)
        vic_backup[i] = VIC_REG(i);

    // now we can turn off the screen to avoid flicker
    VIC_CTRL = 0;
    BORDER = 0; // black
    BACKGROUND = 0; // black for later
    SID_VOLUME = 0;
    SID2_VOLUME = 0;
    SID3_VOLUME = 0;

    // have a look at the timers.
    // These printfs introduce some delay.. if you remove this, some programs won't resume well. Why?!
    printf("CIA1 registers: ");
    for (i = 0; i < 13; i++) {
        printf("%b ", CIA1_REG(i));
    }
    printf("\n");

    // back up 3 kilobytes of RAM to do our thing
    for (i = 0; i < BACKUP_SIZE / 4; i++) {
        ram_backup[i] = MEM_LOC[i];
    }
    for (i = 0; i < COLOR_SIZE / 4; i++) {
        color_backup[i] = COLOR_RAM[i];
    }
    for (i = 0; i < COLOR_SIZE / 4; i++) {
        screen_backup[i] = SCREEN_RAM[i];
    }

    // now copy our own character map into that piece of ram at 0800
    for (i = 0; i < CHARSET_SIZE; i++) {
        CHAR_DEST[i] = char_set[i];
    }

    // backup CIA registers
    cia_backup[0] = CIA2_DDRA;
    cia_backup[1] = CIA2_DPA;
    cia_backup[2] = CIA1_DDRA;
    cia_backup[3] = CIA1_DDRB;
    CIA1_DDRA = 0x00;
    CIA1_DDRB = 0xFF;
    cia_backup[5] = CIA1_DPB;
    CIA1_DDRB = 0x00;
    CIA1_DDRA = 0xFF;
    cia_backup[4] = CIA1_DPA;
    cia_backup[6] = CIA1_CRA;
    cia_backup[7] = CIA1_CRB;

}

void C64::init_io(void)
{
    printf("Init IO.\n");

    // set VIC bank to 0
    CIA2_DDRA &= 0xFC; // set bank bits to input
//  CIA2_DPA  |= 0x03; // don't touch!

    // enable keyboard
    CIA1_DDRB = 0x00; // all in
    CIA1_DDRA = 0xFF; // all out

    printf("CIA DDR: %b %b Mode: %b\n", CIA1_DDRB, CIA1_DDRA, C64_MODE);
    CIA1_DDRA = 0xFF; // all out
    CIA1_DDRB = 0x00; // all in
    printf("CIA DDR: %b %b\n", CIA1_DDRB, CIA1_DDRA);

    CIA1_CRA &= 0xFD; // no PB6 output, interferes with keyboard
    CIA1_CRB &= 0xFD; // no PB7 output, interferes with keyboard

    // Set VIC to use charset at 0800
    // Set VIC to use screen at 0400
    VIC_MMAP = 0x12; // screen $0400 / charset at 0800

    // init_cia_handler clears the state of the cia interrupt handlers.
    // Because we cannot read which interrupts are enabled,
    // the handler simply catches the interrupts and logs which interrupts must
    // have been enabled. These are disabled consequently, and enabled again
    // upon leaving the freezer.
//	init_cia_handler();
//    printf("cia irq mask = %02x. Test = %02X.\n", cia1_imsk, cia1_test);

    VIC_REG(17) = 0x1B; // vic_ctrl	// Enable screen in text mode, 25 rows
    VIC_REG(18) = 0xF8; // raster line to trigger on
    VIC_REG(21) = 0x00; // turn off sprites
    VIC_REG(22) = 0xC8; // Screen = 40 cols with correct scroll
    VIC_REG(32) = 0x00; // black border
    //VIC_REG(26) = 0x01; // Enable Raster interrupt
    //VIC_REG(48) = 252;
}

void C64::freeze(void)
{
    if (!phi2_present())
        return;

    stop();
    backup_io();
    init_io();

    isFrozen = true;
}

/*
 -------------------------------------------------------------------------------
 unfreeze (split in subfunctions)
 ========
 Abstract:

 Restores the backed up resources and resumes the C64

 Parameters:
 Mode:  0 = normal resume
 1 = make screen black, and release in a custom cart
 -------------------------------------------------------------------------------
 */

void C64::restore_io(void)
{
    int i;

    // disable screen
    VIC_CTRL = 0;

    // restore memory
    for (i = 0; i < BACKUP_SIZE / 4; i++) {
        MEM_LOC[i] = ram_backup[i];
    }
    for (i = 0; i < COLOR_SIZE / 4; i++) {
        COLOR_RAM[i] = color_backup[i];
    }
    for (i = 0; i < COLOR_SIZE / 4; i++) {
        SCREEN_RAM[i] = screen_backup[i];
    }

    // restore the cia registers
    CIA2_DDRA = cia_backup[0];
//    CIA2_DPA  = cia_backup[1]; // don't touch!
    CIA1_DDRA = cia_backup[2];
    CIA1_DDRB = cia_backup[3];
    CIA1_DPA = cia_backup[4];
    CIA1_DPB = cia_backup[5];
    CIA1_CRA = cia_backup[6];
    CIA1_CRB = cia_backup[7];

    printf("Set CIA1 %b %b %b %b %b %b\n", cia_backup[0], cia_backup[1], cia_backup[2], cia_backup[3], cia_backup[4],
            cia_backup[5]);

    // restore vic registers
    for (i = 0; i < NUM_VICREGS; i++) {
        VIC_REG(i) = vic_backup[i];
    }

//    restore_cia();  // Restores the interrupt generation

    SID_VOLUME = 15;  // turn on volume. Unfortunately we could not know what it was set to.
    SID2_VOLUME = 15;  // turn on volume. Unfortunately we could not know what it was set to.
    SID3_VOLUME = 15;  // turn on volume. Unfortunately we could not know what it was set to.
    SID_DUMMY = 0;   // clear internal charge on databus!
    SID2_DUMMY = 0;   // clear internal charge on databus!
    SID3_DUMMY = 0;   // clear internal charge on databus!
}

void C64::init_system_roms(void)
{
    C64_KERNAL_ENABLE = 0;

#if U64
    if (cfg->get_value(CFG_C64_ALT_KERN)) {
        uint8_t *temp = new uint8_t[8192];
        if (cfg->get_value(CFG_C64_ALT_KERN) == 1) {
            flash->read_image(FLASH_ID_ORIG_KERNAL, temp, 8192);
        } else if (cfg->get_value(CFG_C64_ALT_KERN) == 2) {
            flash->read_image(FLASH_ID_KERNAL_ROM, temp, 8192);
        } else if (cfg->get_value(CFG_C64_ALT_KERN) == 3) {
            flash->read_image(FLASH_ID_KERNAL_ROM2, temp, 8192);
        } else {
            flash->read_image(FLASH_ID_KERNAL_ROM3, temp, 8192);
        }
        unsigned char *kernal = (unsigned char *)U64_KERNAL_BASE;
        memcpy(kernal, temp, 8192); // as simple as that

        if (cfg->get_value(CFG_C64_FASTRESET)) {
            if (!memcmp((void *) (temp+0x1d6c), (void *) fastresetOrg, sizeof(fastresetOrg))) {
                memcpy((void *) (kernal+0x1d6c), (void *) fastresetPatch, 22);
            }
        }
        delete[] temp;
    }

    if (cfg->get_value(CFG_C64_ALT_BASI)) {
        uint8_t *temp = new uint8_t[8192];
        if (cfg->get_value(CFG_C64_ALT_BASI) == 1) {
            flash->read_image(FLASH_ID_ORIG_BASIC, temp, 8192);
        } else {
            flash->read_image(FLASH_ID_BASIC_ROM, temp, 8192);
        }
        memcpy((void *)U64_BASIC_BASE, temp, 8192); // as simple as that
        delete[] temp;
    }

    if (cfg->get_value(CFG_C64_ALT_CHAR)) {
        uint8_t *temp = new uint8_t[4096];
        if (cfg->get_value(CFG_C64_ALT_CHAR) == 1) {
            flash->read_image(FLASH_ID_ORIG_CHARGEN, temp, 4096);
        } else {
            flash->read_image(FLASH_ID_CHARGEN_ROM, temp, 4096);
        }
        memcpy((void *)U64_CHARROM_BASE, temp, 4096); // as simple as that
        delete[] temp;
    }
#elif CLOCK_FREQ == 62500000
    if (cfg->get_value(CFG_C64_ALT_KERN)) {
        uint8_t *temp = new uint8_t[8192];
        if (cfg->get_value(CFG_C64_ALT_KERN) == 1) {
            flash->read_image(FLASH_ID_KERNAL_ROM, temp, 8192);
        } else {
            flash->read_image(FLASH_ID_KERNAL_ROM2, temp, 8192);
        }
        enable_kernal(temp, cfg->get_value(CFG_C64_FASTRESET));
        delete[] temp;
    } else {
        disable_kernal();
    }
#else
    if (cfg->get_value(CFG_C64_ALT_KERN)) {
        uint8_t *temp = new uint8_t[8192];
        flash->read_image(FLASH_ID_KERNAL_ROM, temp, 8192);
        enable_kernal(temp, cfg->get_value(CFG_C64_FASTRESET));
        delete[] temp;
    } else {
        disable_kernal();
    }
#endif
}

/*
 * Unfreeze - Resumes the C64 after it was frozen by entering the Ultimate Menu.
 */
void C64::unfreeze()
{

    if (!phi2_present())
        return;

    // bring back C64 in original state
    restore_io();
    // resume C64
    resume();

    isFrozen = false;
}

void C64 :: start_cartridge(void *vdef, bool startLater)
{
    cart_def *def = (cart_def *) vdef;

    // If we are called from the overlay or telnet menu, it may be so that the C64 is not even frozen.
    // In this case, we need to stop the machine first in order to poke anything into memory
    hard_stop();

    // Now that the machine is stopped, we can clear certain VIC registers. This is required for the FC3
    // cartridge, but maybe also for others. Obviously, we may not know in what mode we are, so we
    // force Ultimax mode, so that the IO range is accessible.
    goUltimax();

    VIC_REG(32) = 0; // black border
    VIC_REG(17) = 0; // screen off

    // Only in normal mode we can access the RAM. It is important that we clear the CBM80 string here.
    // To be discussed: Should this only happen with the reboot command?
    C64_MODE = MODE_NORMAL;
    C64_POKE(0x8005, 0);

    // Now, let's reset the machine and release it into run mode
    C64_MODE = C64_MODE_RESET;
    C64_CARTRIDGE_TYPE = 0; // disable our carts
    wait_ms(50);

#if U64
    // On the U64, the external carts should be turned off, otherwise the external cart will conflict with our
    // internal cart, or even override it altogether. This is only done when a custom cart definition is given.

    if ((def != 0) || startLater) { // special cart
        // C64_BUS_BRIDGE   = 0; // Quiet mode  (to hear the SID difference, let's not set this register now)
        C64_BUS_INTERNAL = 7; // All ON
        C64_BUS_EXTERNAL = 0; // All OFF
        // these registers will be set back to the correct value upon a reset interrupt, that calls u64_configurator.effectuate_settings()
    }
#endif

    C64_STOP_MODE = STOP_COND_FORCE;
    C64_STOP = 0;


    // The machine is running again, but still in reset.
    if (!startLater)
    {
        // start clean
        C64_CARTRIDGE_TYPE = 0;
        C64_REU_ENABLE = 0;
        C64_SAMPLER_ENABLE = 0;
        CMD_IF_SLOT_ENABLE = 0;

        init_system_roms();
        // Passing 0 to this function means that the default selected cartridge should be run
        if (def == 0) {
            bool external = false;
#if U64
            // In case of the U64, when an external cartridge is detected, we should not enable our cart
            external = ConfigureU64SystemBus();
#endif
            if (!external) {
                int cart = cfg->get_value(CFG_C64_CART);
                def = &cartridges[cart];
                set_cartridge(def);
            }
        } else { // Cartridge specified
            set_cartridge(def);
        }
        C64_MODE = C64_MODE_UNRESET;
    }

    isFrozen = false;
}

Screen *C64::getScreen(void)
{
    /*
     if (!screen)
     screen = new Screen_MemMappedCharMatrix(C64_SCREEN, C64_COLORRAM, 40, 25);
     */
    screen->clear();
    return screen;
}

void C64::releaseScreen()
{
    /*
     if(screen) {
     delete screen;
     screen = 0;
     }
     */
}

bool C64::is_accessible(void)
{
    return isFrozen;
}

Keyboard *C64::getKeyboard(void)
{
    return keyb;
}

void C64::set_cartridge(cart_def *def)
{
    if (!def) {
        int cart = cfg->get_value(CFG_C64_CART);
        def = &cartridges[cart];
    }
    lastCartridgeId = def->id;

    printf("Setting cart mode %u. Reu enable flag: %b\n", def->type, cfg->get_value(CFG_C64_REU_EN));
    C64_CARTRIDGE_TYPE = (uint8_t) (def->type & 0x1F) | force_cart;
    force_cart = 0;

    set_emulation_flags(def);

    uint32_t mem_addr = ((uint32_t)C64_CARTRIDGE_RAM_BASE) << 16;
    if (def->type & CART_RAM) {
        printf("Copying %d bytes from array %p to mem addr %p\n", def->length, def->custom_addr, mem_addr);
        memcpy((void *) mem_addr, def->custom_addr, def->length);

        switch(def->id) {
        case ID_MODPLAYER:
            // now overwrite the register settings
            C64_REU_SIZE = 7;
            C64_REU_ENABLE = 1;
            C64_SAMPLER_ENABLE = 1;
            break;
        case ID_CMDTEST:
            if (getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
                CMD_IF_SLOT_ENABLE = 1;
                CMD_IF_SLOT_BASE = 0x47; // $df1c
            }
            break;
        case ID_SIDCART:
            if (getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
                CMD_IF_SLOT_ENABLE = 1;
                CMD_IF_SLOT_BASE = 0x7F; // $dffc
                printf("Enabled UCI at $DFFC\n");
            }
            break;
        }
    } else if (def->id && def->type) {
        printf("Requesting copy from Flash, id = %b to mem addr %p\n", def->id, mem_addr);
        flash->read_image(def->id, (void *) mem_addr, def->length);
        int fc3mode = cfg->get_value(CFG_C64_FC3MODE);

        if ((def->id == FLASH_ID_FINAL3) && fc3mode)
        {
           int found = -1;
           unsigned char* mem_addr8 = (unsigned char*) mem_addr;
           for (int i=0; i<300; i++) {
              if ( mem_addr8[i+0] == 0x58 && mem_addr8[i+1] == 0x68
                   && mem_addr8[i+2] == 0xAA && mem_addr8[i+3] == 0x68
                   && mem_addr8[i+4] == 0xE0 && mem_addr8[i+5] == 0x7F
                   && (mem_addr8[i+6] == 0xD0 || mem_addr8[i+6] == 0xF0)
                   && mem_addr8[i+8] == 0xe0 && mem_addr8[i+9] == 0xDF
                   && mem_addr8[i+10] == 0xf0 ) {
                   found = i;
                   break;
              }
           }
           if (found != -1) {
               mem_addr8[found+6] = fc3mode == 1 ? 0xF0 : 0xD0;
           }
        }
    } else if (def->id && !def->type) {
#ifndef RECOVERYAPP
#ifndef NO_FILE_ACCESS
        char* buffer = new char[128 * 1024];
        printf("Requesting crt copy from Flash, id = %b to mem addr %p\n", def->id, buffer);
        flash->read_image(def->id, (void *) buffer, 128 * 1024);
        FileTypeCRT::parseCrt(buffer);
        delete buffer;
#endif
#endif
    } else if (def->length) { // not ram, not flash.. then it has to be custom
        *(uint8_t *) (mem_addr + 5) = 0; // disable previously started roms.
    }

    // clear function RAM on the cartridge
    mem_addr -= 65536; // TODO: We know it, because we made the hardware, but the hardware should tell us!
    memset((void *) mem_addr, 0x00, 65536);
}

void C64::set_colors(int background, int border)
{
    BORDER = uint8_t(border);
    BACKGROUND = uint8_t(background);
}

void C64::enable_kernal(uint8_t *rom, bool fastreset)
{
    if (fastreset && !memcmp((void *) (rom+0x1d6c), (void *) fastresetOrg, sizeof(fastresetOrg)))
        memcpy((void *) (rom+0x1d6c), (void *) fastresetPatch, 22);

    if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
        memcpy((void *)U64_KERNAL_BASE, rom, 8192); // as simple as that
    } else { // the good old way
        C64_KERNAL_ENABLE = 1;
        uint8_t *src = rom;
        uint8_t *dst = (uint8_t *) (C64_KERNAL_BASE + 1);
        for (int i = 0; i < 8192; i++) {
            *(dst) = *(src++);
            dst += 2;
        }
    }
}

void C64::disable_kernal()
{
    C64_KERNAL_ENABLE = 0;

    if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
    	uint8_t* kernal = (uint8_t *)U64_KERNAL_BASE;
        flash->read_image(FLASH_ID_ORIG_KERNAL, (uint8_t *)U64_KERNAL_BASE, 8192);
        if (cfg->get_value(CFG_C64_FASTRESET) && !memcmp((void *) (kernal+0x1d6c), (void *) fastresetOrg, sizeof(fastresetOrg)))
            memcpy((void *) (kernal+0x1d6c), (void *) fastresetPatch, 22);
    }
}

void C64::init_cartridge()
{
    if (!cfg)
        return;

    if (C64_STOP == 0) {
        C64_MODE = C64_MODE_RESET;
    }
    C64_KERNAL_ENABLE = 0;
    C64_CARTRIDGE_TYPE = 0;

    init_system_roms();

#if U64
    if (ConfigureU64SystemBus()) { // returns true if external cartridge is present and has preference
        printf("External Cartridge Selected. Not initializing cartridge.\n");
        wait_ms(100);
        C64_MODE = C64_MODE_UNRESET;
        C64_STOP = 0;
        return;
    }
#endif

    int cart = cfg->get_value(CFG_C64_CART);
    cart_def *cart2 = &cartridges[cart];

    if (cart2->id ==  FLASH_ID_FINAL3)
    {
        C64_MODE = C64_MODE_UNRESET;
        C64_STOP = 0;
        wait_ms(100);
        freeze();
        wait_ms(1400);
        start_cartridge(cart2, false);
    }
    else if (cart2->id == FLASH_ID_CUSTOM_ROM && !cart2->type)
    {
        C64_MODE = C64_MODE_UNRESET;
        C64_STOP = 0;
        wait_ms(100);
        freeze();
        wait_ms(1400);
        start_cartridge(cart2, false);
    }
    else
    {
       set_cartridge(cart2);
       wait_ms(100);
       C64_MODE = C64_MODE_UNRESET;
       C64_STOP = 0;
    }
}

void C64::cartridge_test(void)
{
    for (int i = 0; i < 19; i++) {
        printf("Setting mode %d\n", i);
        cfg->set_value(CFG_C64_CART, i);
        init_cartridge();
        wait_ms(4000);
    }
}

bool C64::buttonPush(void)
{
    bool ret = buttonPushSeen;
    buttonPushSeen = false;

    return ret;
}

void C64::setButtonPushed(void)
{
    buttonPushSeen = true;
}

void C64::checkButton(void)
{
    static uint8_t button_prev;

    // Dont check the button if we are in stand alone mode, since we don't have a cartridge port
    if (!available) {
        return;
    }
    uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    if ((buttons & ~button_prev) & ITU_BUTTON1) {
        buttonPushSeen = true;
    }
    button_prev = buttons;
}

#if U64
bool C64 :: ConfigureU64SystemBus(void)
{
    C64_BUS_BRIDGE   = bus_mode_values[cfg->get_value(CFG_BUS_MODE)];
    uint8_t internal = 0;
    uint8_t external = 0;
    bool ext_cart = ((U64_CART_DETECT & 3) != 3); // true when external cartridge is present

    switch (cfg->get_value(CFG_C64_CART_PREF)) {
    case 0: // Automatic: If external cartridge is present, switch entire bus to external
        if (ext_cart) {
            internal = 0;
            external = 7;
        } else {
            internal = 7;
            external = 0;
        }
        break;
    case 1: // Internal: Force all internal resources
        internal = 7;
        external = 0;
        ext_cart = false;
        break;
    case 2: // External: Force all external resources
        internal = 0;
        external = 7;
        break;

    case 3: // Manual: Take settings from other config items
        ext_cart = false; // let's assume there is none

        switch (cfg->get_value(CFG_BUS_SHARING_ROM)) {
        case 0: // Internal
            internal |= 0x02;
            break;
        case 1: // External
            external |= 0x02;
            break;
        case 2: // Both
            internal |= 0x02;
            external |= 0x02;
            break;
        }

        switch (cfg->get_value(CFG_BUS_SHARING_IRQ)) {
        case 0: // Internal
            internal |= 0x04;
            break;
        case 1: // External
            external |= 0x04;
            break;
        case 2: // Both
            internal |= 0x04;
            external |= 0x04;
            break;
        }

        switch (cfg->get_value(CFG_BUS_SHARING_IO)) {
        case 0: // Internal
            internal |= 0x01;
            break;
        case 1: // External
            external |= 0x01;
            break;
        case 2: // Both
            internal |= 0x01;
            external |= 0x01;
            break;
        }

    }

    C64_BUS_INTERNAL = internal;
    C64_BUS_EXTERNAL = external;

    return ext_cart;
}

void C64 :: EnableWriteMirroring(void)
{
    C64_BUS_BRIDGE = 0x01 | bus_mode_values[cfg->get_value(CFG_BUS_MODE)];
}
#endif

int C64 :: isMP3RamDrive(int drvNo)
{
    uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
    uint8_t RealDrvType = reu[0xbb0e + drvNo];
    if ((reu[0xbb0e] == 0x03) && (reu[0xbb0f] == 0xA9) && (reu[0xbb10] == 0x06) && (reu[0xbb11] == 0x8D))
    {
        RealDrvType = reu[0x798e + drvNo];
        if (RealDrvType > 0x83)
            RealDrvType = 0;
    }
    
    uint8_t ramBase = reu[0x7dc7 + drvNo] ;
    int drvType = 0;
    if (RealDrvType == 0x81) drvType = 1541;
    if (RealDrvType == 0x82) drvType = 1571;
    if (RealDrvType == 0x83) drvType = 1581;
    if (RealDrvType == 0x84) drvType = DRVTYPE_MP3_DNP;
    if (RealDrvType == 0xA4) drvType = DRVTYPE_MP3_DNP;
    if (ramBase > 0x40) drvType = 0;
    return drvType;
}

int C64 :: getSizeOfMP3NativeRamdrive(int devNo)
{
    uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
    uint8_t DskDrvBaseL = reu[0xbafe + devNo];
    uint8_t DskDrvBaseH = reu[0xbafe + 4 + devNo];
    uint16_t dskDrvBase = (((uint16_t) DskDrvBaseH) << 8) | DskDrvBaseL;
    uint8_t noTracks = reu[dskDrvBase + 0x84];
    return ((uint32_t) noTracks) << 16;
}
