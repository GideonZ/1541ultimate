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

#include "menu.h"
#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "c64.h"
#include "flash.h"
#include <string.h>
#include "menu.h"
#include "userinterface.h"

/* other external references */
extern BYTE _binary_bootcrt_65_start;
extern BYTE _binary_cmd_test_rom_65_start;

cart_def boot_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM }; 
cart_def cmd_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

// static pointer
C64   *c64;

static inline WORD le2cpu(WORD p)
{
	WORD out = (p >> 8) | (p << 8);
	return out;
}

/* Configuration */
char *cart_mode[] = { "None",
                      "Final Cart III",
                      "Action Replay V4.2 PAL",
                      "Action Replay V6.0 PAL",
                      "Retro Replay V3.8p PAL",
                      "SuperSnapshot V5.22 PAL",
                      "TAsm / CodeNet PAL",
                      "Action Replay V5.0 NTSC",
                      "Retro Replay V3.8a NTSC",
                      "SuperSnapshot V5.22 NTSC",
                      "TAsm / CodeNet NTSC",
                      "Epyx Fastloader",
                      "Custom 8K ROM",
                      "Custom 16K ROM",
/*
                      "Custom System 3 ROM",
                      "Custom Ocean V1 ROM",
                      "Custom Ocean V2/T2 ROM",
                      "Custom Final III ROM",
*/
                      "Custom Retro Replay ROM",
                      "Custom Snappy ROM"
                   };

cart_def cartridges[] = { { 0x00,               0x000000, 0x00000,  0x00 | CART_REU | CART_ETH },
                          { FLASH_ID_FINAL3,    0x000000, 0x10000,  0x04 },
                          { FLASH_ID_AR5PAL,    0x000000, 0x08000,  0x07 },
                          { FLASH_ID_AR6PAL,    0x000000, 0x08000,  0x07 },
                          { FLASH_ID_RR38PAL,   0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_SS5PAL,    0x000000, 0x10000,  0x05 | CART_REU },
                          { FLASH_ID_TAR_PAL,   0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_AR5NTSC,   0x000000, 0x08000,  0x07 },
                          { FLASH_ID_RR38NTSC,  0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_SS5NTSC,   0x000000, 0x10000,  0x05 | CART_REU },
                          { FLASH_ID_TAR_NTSC,  0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { FLASH_ID_EPYX,      0x000000, 0x02000,  0x0A },
                          { 0x00,               0x000000, 0x02000,  0x01 | CART_REU | CART_ETH },
                          { 0x00,               0x000000, 0x04000,  0x02 | CART_REU | CART_ETH },
/*
                          { 0x00,               0x000000, 0x80000,  0x08 | CART_REU },
                          { 0x00,               0x000000, 0x80000,  0x09 | CART_REU },
                          { 0x00,               0x000000, 0x80000,  0x0B | CART_REU },
                          { 0x00,               0x000000, 0x10000,  0x04 },
*/
                          { 0x00,               0x000000, 0x10000,  0x06 | CART_REU | CART_ETH },
                          { 0x00,               0x000000, 0x10000,  0x05 | CART_REU }
 };
                          
#define CFG_C64_CART     0xC1
#define CFG_C64_CUSTOM   0xC2
#define CFG_C64_REU_EN   0xC3
#define CFG_C64_REU_SIZE 0xC4
#define CFG_C64_ETH_EN   0xC5
#define CFG_C64_SWAP_BTN 0xC6
#define CFG_C64_DMA_ID   0xC7
#define CFG_C64_MAP_SAMP 0xC8

char *reu_size[] = { "128 KB", "256 KB", "512 KB", "1 MB", "2 MB", "4 MB", "8 MB", "16 MB" };    
char *en_dis2[] = { "Disabled", "Enabled" };
char *buttons[] = { "Reset|Menu|Freezer", "Freezer|Menu|Reset" };

struct t_cfg_definition c64_config[] = {
    { CFG_C64_CART,     CFG_TYPE_ENUM,   "Cartridge",                    "%s", cart_mode,  0, 15, 4 },
    { CFG_C64_CUSTOM,   CFG_TYPE_STRING, "Custom Cart ROM",              "%s", NULL,       1, 31, (int)"cart.bin" },
    { CFG_C64_REU_EN,   CFG_TYPE_ENUM,   "RAM Expansion Unit",           "%s", en_dis2,    0,  1, 0 },
    { CFG_C64_REU_SIZE, CFG_TYPE_ENUM,   "REU Size",                     "%s", reu_size,   0,  7, 4 },
    { CFG_C64_MAP_SAMP, CFG_TYPE_ENUM,   "Map Ultimate Audio $DF20-DFFF","%s", en_dis2,    0,  1, 0 },
    { CFG_C64_DMA_ID,   CFG_TYPE_VALUE,  "DMA Load Mimics ID:",          "%d", NULL,       8, 31, 8 },
    { CFG_C64_SWAP_BTN, CFG_TYPE_ENUM,   "Button order",                 "%s", buttons,    0,  1, 1 },
//    { CFG_C64_ETH_EN,   CFG_TYPE_ENUM,   "Ethernet CS8900A",        "%s", en_dis2,     0,  1, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

#define MENU_C64_RESET      0x6401
#define MENU_C64_REBOOT     0x6402
//#define MENU_C64_TRACE    0x6581
#define MENU_C64_SAVEREU    0x6403
#define MENU_C64_SAVEFLASH  0x6404
#define MENU_C64_RUNCMDCART 0x6405
#define MENU_C64_BOOTFPGA   0x6408

extern BYTE _binary_chars_bin_start;

    
C64 :: C64()
{
    flash = get_flash();
    register_store(0x43363420, "C64 and cartridge settings", c64_config);

    // char_set = new BYTE[CHARSET_SIZE];
    // flash->read_image(FLASH_ID_CHARS, (void *)char_set, CHARSET_SIZE);
    char_set = (BYTE *)&_binary_chars_bin_start;
    keyb = new Keyboard(this, &CIA1_DPB, &CIA1_DPA);

    if(C64_CLOCK_DETECT == 0)
        printf("No PHI2 clock detected.. Stand alone mode.\n");
	else
		main_menu_objects.append(this);
	
    C64_STOP_MODE = STOP_COND_FORCE;
    C64_MODE = MODE_NORMAL;
	C64_STOP = 0;
	stopped = false;
    C64_MODE = C64_MODE_RESET;
}
    
C64 :: ~C64()
{
	if(stopped) {
	    restore_io();
	    resume();
	    stopped = false;
	}

    delete keyb;
//    delete[] char_set;
}

void C64 :: effectuate_settings(void)
{
    // this function will set the parameters that can be switched while the machine is running
    // init_cartridge is called only at reboot, and will cause the C64 to reset.
	C64_SWAP_CART_BUTTONS = cfg->get_value(CFG_C64_SWAP_BTN);

    cart_def *def;
	int cart = cfg->get_value(CFG_C64_CART);
	def = &cartridges[cart];
    set_emulation_flags(def);
}

void C64 :: set_emulation_flags(cart_def *def)
{
    C64_REU_ENABLE = 0;
	C64_SAMPLER_ENABLE = 0;

    if(def->type & CART_REU) {
        if(cfg->get_value(CFG_C64_REU_EN)) {
        	printf("Enabling REU!!\n");
        	C64_REU_ENABLE = 1;
            C64_REU_SIZE = cfg->get_value(CFG_C64_REU_SIZE);
        }
        if(CAPABILITIES & CAPAB_SAMPLER) {
            printf("Sampler found in FPGA... IO map: ");
            if(cfg->get_value(CFG_C64_MAP_SAMP)) {
                printf("Enabled!\n");
            	C64_SAMPLER_ENABLE = 1;
            } else {
                printf("disabled.\n");
            }
        }
    }
    C64_ETHERNET_ENABLE = 0;
    if(def->type & CART_ETH) {
        if(cfg->get_value(CFG_C64_ETH_EN)) {
            C64_ETHERNET_ENABLE = 1;
        }
    }
}

bool C64 :: exists(void)
{
    return (C64_CLOCK_DETECT != 0);
}
    
int  C64 :: fetch_task_items(IndexedList<PathObject*> &item_list)
{
	item_list.append(new ObjectMenuItem(this, "Reset C64", MENU_C64_RESET));
	item_list.append(new ObjectMenuItem(this, "Reboot C64", MENU_C64_REBOOT));
//    item_list.append(new ObjectMenuItem(this, "Boot Alternate FPGA", MENU_C64_BOOTFPGA));
//    item_list.append(new ObjectMenuItem(this, "Save SID Trace", MENU_C64_TRACE));
    item_list.append(new ObjectMenuItem(this, "Save REU Memory", MENU_C64_SAVEREU));
//    item_list.append(new ObjectMenuItem(this, "Run Command Cart", MENU_C64_RUNCMDCART));  /* temporary item */
   
//    item_list.append(new ObjectMenuItem(this, "Save Flash", MENU_C64_SAVEFLASH));
	return 3;
}
		
void C64 :: determine_d012(void)
{
    BYTE b;
    // pre-condition: C64 is already in the stopped state, and IRQ registers have already been saved
    //                C64 IRQn to SD-IRQ is not enabled.
    
    VIC_REG(26) = 0x01; // interrupt enable = raster only
    VIC_REG(25) = 0x81; // clear raster interrupts
    
    // poll until VIC interrupt occurs

    vic_d012 = 0;
    vic_d011 = 0;

    b = 0;
    while(!(VIC_REG(25) & 0x81)) {
        if(!ITU_TIMER) {
            ITU_TIMER = 200;
            b++;
            if(b == 40)
                break;
        }
    }

    vic_d012 = VIC_REG(18); // d012
    vic_d011 = VIC_REG(17); // d011
    
    VIC_REG(26) = 0x00; // disable interrutps
    VIC_REG(25) = 0x81; // clear raster interrupt
}

void C64 :: stop(bool do_raster)
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

    if(do_raster) {
        // wait maximum for 25 ms (that is 1/40 of a second; there should be a bad line in this time frame
        ITU_TIMER = 200; // 1 ms
    
    	// request to stop the c-64
    	C64_STOP = 1;
        //printf(" condition set.\n");
        
        for(b=0;b<25;) {
            if(C64_STOP & C64_HAS_STOPPED) {  // has the C64 stopped already?
    //            VIC_REG(48) = 0;  // switch to slow mode (C128)
            	// enter ultimax mode, so we can be sure that we can access IO space!
                C64_MODE    = MODE_ULTIMAX;

                printf("Ultimax set.. Now reading registers..\n");
                raster     = VIC_REG(18);
                raster_hi  = VIC_REG(17);
                vic_irq_en = VIC_REG(26);
                vic_irq    = VIC_REG(25);
                stop_mode = 1;
                printf("Mode=1\n");
                break;
            }
            if(!ITU_TIMER) {
                ITU_TIMER = 200; // 1 ms
                b++;
            }
        }
    }
    
    //printf("Raster failed.\n");
    
    // If that fails, the screen must be blanked or so, so we try to break upon a safe R/Wn sequence
    if(!stop_mode) {
        C64_STOP_MODE = STOP_COND_RWSEQ;

    	// request to stop the c-64
    	C64_STOP = 1; // again, in case we had to stop without trying raster first

        ITU_TIMER = 200; // 1 ms
    
        for(w=0;w<100;) { // was 1500
            if(C64_STOP & C64_HAS_STOPPED) {
                stop_mode = 2;
                break;
            }
//            printf("#%b^%b ", ITU_TIMER, w);
            if(!ITU_TIMER) {
                ITU_TIMER = 200; // 1 ms
                w++;
            }
        }
    }
//    printf("SM=%d.", stop_mode);

    // If that fails, the CPU must have disabled interrupts, and be in a polling read loop
    // since no write occurs!
    if(!stop_mode) {
        C64_STOP_MODE = STOP_COND_FORCE;

        // wait until it is stopped (it always will!)
        while(!(C64_STOP & C64_HAS_STOPPED))
            ;

        stop_mode = 3;
    }

    if(do_raster) {  // WRONG parameter actually, but this function is only called in two places:
                     // entering the menu, and dma load. menu calls it with true, dma load with false.... :-S
                     // TODO: Clean up
        switch(stop_mode) {
            case 1:
                printf("Frozen on Bad line. Raster = %02x. VIC Irq Enable: %02x. Vic IRQ: %02x\n", raster, vic_irq_en, vic_irq);
                if(vic_irq_en & 0x01) { // Raster interrupt enabled
                    determine_d012();
                    printf("Original d012/11 content = %02x %02X.\n", vic_d012, vic_d011);
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

void C64 :: resume(void)
{
    int dummy=0;
    BYTE rast_lo;
    BYTE rast_hi;
    
    if(!raster && !raster_hi) {
        rast_lo = 0;
        rast_hi = 0;
    } else {
        rast_lo = raster - 1;
        rast_hi = raster_hi & 0x80;
        if(!raster)
            rast_hi = 0;
    }

    // either resume in mode 0 or in mode 3  (never on R/Wn sequence of course!)
    if (stop_mode == 1) {
        // this can only occur when we exit from the menu, so we are still
        // in ultimax mode, so we can see the VIC here.
        if(vic_irq & 0x01) { // was raster flag set when we stopped? then let's set it again
            VIC_REG(26) = 0x81;
            VIC_REG(18) = rast_lo;
            VIC_REG(17) = rast_hi;
            while((VIC_REG(18) != rast_lo)&&((VIC_REG(17)&0x80) != rast_hi)) {
                if(!ITU_TIMER) {
                    ITU_TIMER = 200;
                    dummy++;
                    if(dummy == 40)
                        break;
                }
            }
            VIC_REG(18) = vic_d012;
            VIC_REG(17) = vic_d011;
        } else {
            while((VIC_REG(18) != rast_lo)&&((VIC_REG(17)&0x80) != rast_hi)) {
                if(!ITU_TIMER) {
                    ITU_TIMER = 200;
                    dummy++;
                    if(dummy == 40)
                        break;
                }
            }
            VIC_REG(18) = vic_d012;
            VIC_REG(17) = vic_d011;
            VIC_REG(25) = 0x81; // clear raster irq
        }
        VIC_REG(26) = vic_irq_en;

        raster     = VIC_REG(18);
        vic_irq_en = VIC_REG(26);
        vic_irq    = VIC_REG(25);

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

void C64 :: reset(void)
{
    C64_MODE = C64_MODE_RESET;
    ITU_TIMER = 20;
    while(ITU_TIMER)
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
void C64 :: backup_io(void)
{
	int i;
//	char *scr = (char *)SCREEN1_ADDR;

	// enter ultimax mode, as this might not have taken place already!
    C64_MODE    = MODE_ULTIMAX;

	// back up VIC registers
	for(i=0;i<NUM_VICREGS;i++)
		vic_backup[i] = VIC_REG(i); 
	
	// now we can turn off the screen to avoid flicker
	VIC_CTRL	= 0;
	BORDER		= 0; // black
	BACKGROUND	= 0; // black for later
    SID_VOLUME  = 0;
	
    // have a look at the timers.
    // These printfs introduce some delay.. if you remove this, some programs won't resume well. Why?!
    printf("CIA1 registers: ");
    for(i=0;i<13;i++) {
        printf("%b ", CIA1_REG(i));
    } printf("\n");

	// back up 3 kilobytes of RAM to do our thing 
	for(i=0;i<BACKUP_SIZE;i++) {
		ram_backup[i] = MEM_LOC(i);
	}

	// now copy our own character map into that piece of ram at 0800
	for(i=0;i<CHARSET_SIZE;i++) {
		CHAR_DEST(i) = char_set[i];
	}
	
	// backup CIA registers
	cia_backup[0] = CIA2_DDRA;
	cia_backup[1] = CIA2_DPA;
	cia_backup[2] = CIA1_DDRA;
	cia_backup[3] = CIA1_DDRB;
	cia_backup[4] = CIA1_DPA;
}

void C64 :: init_io(void)
{
    printf("Init IO.\n");
    
    // set VIC bank to 0
	CIA2_DDRA &= 0xFC; // set bank bits to input
//  CIA2_DPA  |= 0x03; // don't touch!

    // enable keyboard
	CIA1_DDRA  = 0xFF; // all out
	CIA1_DDRB  = 0x00; // all in
	
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
}    


void C64 :: freeze(void)
{
    stop();

    // turn off button interrupts on SD-CPU
//    GPIO_IMASK  &= ~BUTTONS;

//	dump_hex((void *)C64_MEMORY_BASE, 0x400);

    backup_io();
    init_io();
    
    stopped = true;
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

void C64 :: restore_io(void)
{
    int i;

    // disable screen
    VIC_CTRL = 0;

    // restore memory
    for(i=0;i<BACKUP_SIZE;i++) {
        MEM_LOC(i) = ram_backup[i];
    }

    // restore the cia registers
    CIA2_DDRA = cia_backup[0];
//    CIA2_DPA  = cia_backup[1]; // don't touch!
	CIA1_DDRA = cia_backup[2];
	CIA1_DDRB = cia_backup[3];
	CIA1_DPA  = cia_backup[4];

    printf("Set CIA1 %b %b %b %b %b\n", cia_backup[0], cia_backup[1], cia_backup[2], cia_backup[3], cia_backup[4]);

    // restore vic registers
    for(i=0;i<NUM_VICREGS;i++) {
        VIC_REG(i) = vic_backup[i]; 
    }        

//    restore_cia();  // Restores the interrupt generation

    SID_VOLUME = 15;  // turn on volume. Unfortunately we could not know what it was set to.
    SID_DUMMY  = 0;   // clear internal charge on databus!
}

void C64 :: unfreeze(Event &e)
{
    int mode = e.param;
    cart_def *def = (cart_def *)e.object;
    
    keyb->wait_free();
    
	if(mode == 0) {
	    // bring back C64 in original state
	    restore_io();
	    // resume C64
	    resume();
	} else {
		VIC_REG(32) = 0; // black border
		VIC_REG(17) = 0; // screen off

        C64_STOP_MODE = STOP_COND_FORCE;
        C64_MODE = MODE_NORMAL;
		C64_MODE = C64_MODE_RESET;
		wait_ms(1);
    	C64_STOP = 0;
		set_cartridge(def);
		C64_MODE = C64_MODE_UNRESET;
	}
    stopped = false;
}

char *C64 :: get_screen(void)
{
    return (char *)C64_SCREEN;
}
    
char *C64 :: get_color_map(void)
{
    return (char *)C64_COLORRAM;
}

bool C64 :: is_accessible(void)
{
    return stopped;
}

Keyboard *C64 :: get_keyboard(void)
{
    return keyb;
}


void C64 :: set_cartridge(cart_def *def)
{
	if(!def) {
		int cart = cfg->get_value(CFG_C64_CART);
		def = &cartridges[cart];
	}
	
    printf("Setting cart mode %b. Reu enable flag: %b\n", def->type, cfg->get_value(CFG_C64_REU_EN));
    C64_CARTRIDGE_TYPE = def->type & 0x1F;
    push_event(e_cart_mode_change, NULL, def->type);
    
    set_emulation_flags(def);

    DWORD mem_addr = ((DWORD)C64_CARTRIDGE_RAM_BASE) << 16;
    if(def->type & CART_RAM) {
        printf("Copying %d bytes from array %p to mem addr %p\n", def->length, def->custom_addr, mem_addr); 
        memcpy((void *)mem_addr, def->custom_addr, def->length);
    } else if(def->id) {
        printf("Requesting copy from Flash, id = %b to mem addr %p\n", def->id, mem_addr);
        flash->read_image(def->id, (void *)mem_addr, def->length);
    } else if (def->length) { // not ram, not flash.. then it has to be custom
        *(BYTE *)(mem_addr+5) = 0; // disable previously started roms.

        char *n = cfg->get_string(CFG_C64_CUSTOM);
        printf("Now I need to load '%s' as cartridge.\n", n);

#ifndef _NO_FILE_ACCESS
        File *f = root.fopen(n, FA_READ);
		if(f) {
			printf("File: %p\n", f);
			UINT transferred;
			FRESULT res = f->read((void *)mem_addr, def->length, &transferred);
			if(res != FR_OK) {
				printf("Error loading file.. disabling cart.\n");
				C64_CARTRIDGE_TYPE = 0;
			}
			root.fclose(f);
		} else {
			printf("Open file failed.\n");
		}
#endif
    }
	// clear function RAM on the cartridge
    mem_addr -= 65536; // TODO: We know it, because we made the hardware, but the hardware should tell us!
    memset((void *)mem_addr, 0x00, 65536);
}

void C64 :: set_colors(int background, int border) {
    BORDER     = BYTE(border);
    BACKGROUND = BYTE(background);
}

void C64 :: init_cartridge()
{
    if(!cfg)
        return;

    C64_MODE = C64_MODE_RESET;

    int cart = cfg->get_value(CFG_C64_CART);
    set_cartridge(&cartridges[cart]);
    
    C64_MODE = C64_MODE_UNRESET;
}

void C64 :: cartridge_test(void)
{
    for(int i=0;i<19;i++) {
        printf("Setting mode %d\n", i);
        cfg->set_value(CFG_C64_CART, i);
        init_cartridge();
        wait_ms(4000);
    }
}

void C64 :: poll(Event &e)
{
#ifndef _NO_FILE_ACCESS
	File *f;
	PathObject *po;
	DWORD size;
	UINT transferred;
    t_flash_address addr;
	int run_code;

	if(e.type == e_dma_load) {
		f = (File *)e.object;
		run_code = e.param;
		dma_load(f, run_code);
        if (f) {
            root.fclose(f);
        }
//		f->close();
	} else if((e.type == e_object_private_cmd)&&(e.object == this)) {
		switch(e.param) {
		case MENU_C64_RESET:
			if(is_accessible()) { // we can't execute this yet
				push_event(e_unfreeze);
				push_event(e_object_private_cmd, this, MENU_C64_RESET); // rethrow event
			} else {
				reset();
			}
			break;
		case MENU_C64_REBOOT:
			if(is_accessible()) { // we can't execute this yet
				push_event(e_unfreeze);
				push_event(e_object_private_cmd, this, MENU_C64_REBOOT); // rethrow event
			} else {
				init_cartridge();
			}
			break;
//        case MENU_C64_TRACE:
//            printf("Trace end address: %8x.\n", SID_TRACE_END);
//            for(int i=0;i<32;i++) {
//                printf("%b ", SID_REGS(i));
//            }
//            po = user_interface->get_path();
//            f = root.fcreate("sidtrace.555", po);
//            if(f) {
//                printf("Opened file successfully.\n");
//                size = SID_TRACE_END - REU_MEMORY_BASE;
//                if(size > 1000000)
//                    size = 1000000;
//                printf("Writing %d bytes,...", size);
//                f->write((void *)REU_MEMORY_BASE, size, &transferred);
//                printf("written: %d...", transferred);
//                f->close();
//            } else {
//                printf("Couldn't open file..\n");
//            }
//            break;
        case MENU_C64_SAVEREU:
            po = user_interface->get_path();
            f = root.fcreate("memory.reu", po);
            if(f) {
                printf("Opened file successfully.\n");
                f->write((void *)REU_MEMORY_BASE, REU_MAX_SIZE, &transferred);
                printf("written: %d...", transferred);
                f->close();
            } else {
                printf("Couldn't open file..\n");
            }
            break;
        case MENU_C64_SAVEFLASH: // doesn't belong here, but i need it fast
            po = user_interface->get_path();
            f = root.fcreate("flash_dump.bin", po);
            if(f) {
                int pages = flash->get_number_of_pages();
                int page_size = flash->get_page_size();
                BYTE *page_buf = new BYTE[page_size];
                for(int i=0;i<pages;i++) {
                    flash->read_page(i, page_buf);
                    f->write(page_buf, page_size, &transferred);
                }
                f->close();
            } else {
                printf("Couldn't open file..\n");
            }
            break;
        case MENU_C64_BOOTFPGA:
            flash->get_image_addresses(FLASH_ID_CUSTOMFPGA, &addr);
            flash->reboot(addr.start);
            break;
        case MENU_C64_RUNCMDCART:
            cmd_cart.custom_addr = (void *)&_binary_cmd_test_rom_65_start;
            push_event(e_unfreeze, (void *)&cmd_cart, 1);
            break;
		default:
			printf("Unhandled C64 menu command %4x.\n", e.param);
		}
	}
#endif
}

// static member
int C64Event :: prepare_dma_load(File *f, const char *name, int len, BYTE run_code, WORD reloc)
{
    C64_POKE(0x162, run_code);

    for (int i=0; i < len; i++) {
        C64_POKE(0x165+i, name[i]);
    }
    C64_POKE(0x164, len);

    C64_POKE(2, 0x80); // initial boot cart handshake
    boot_cart.custom_addr = (void *)&_binary_bootcrt_65_start;
    push_event(e_unfreeze, (void *)&boot_cart, 1);
    return 0;
}

// static member
int C64Event :: perform_dma_load(File *f, BYTE run_code, WORD reloc)
{
    push_event(e_dma_load, f, run_code);
    return 0;
}



int C64 :: dma_load(File *f, BYTE run_code, WORD reloc)
{
	// First, check if we have file access
    UINT transferred;
	WORD load_address;
    if (f) {
        FRESULT res = f->read(&load_address, 2, &transferred);
        printf("Load address: %4x...", load_address);
        if(res != FR_OK) {
            printf("Can't read from file..\n");
            return -1;
        }
    }
    load_address = le2cpu(load_address); // make sure we can interpret the word correctly (endianness)

    printf("File load location: %4x\n", load_address);
    if(reloc)
    	load_address = reloc;

    int max_length = 65536 - int(load_address); // never exceed $FFFF

	// handshake with boot cart
	stop(false);
	C64_POKE(0x163, cfg->get_value(CFG_C64_DMA_ID));    // drive number for printout

	C64_POKE(2, 0x40);  // signal cart ready for DMA load

    if ( !(run_code & RUNCODE_REAL_BIT) ) {

        int timeout = 0;
        while(C64_PEEK(2) != 0x01) {
            resume();
            timeout++;
            if(timeout == 60) {
                init_cartridge();
                return -1;
            }
            wait_ms(25);
            printf("_");
            stop(false);
        }
        printf("Now loading...");

        /* Now actually load the file */
        f->read((BYTE *)(C64_MEMORY_BASE + load_address), max_length, &transferred);
        WORD end_address = load_address + transferred;
        printf("DMA load complete: $%4x-$%4x Run Code: %b\n", load_address, end_address, run_code);

        C64_POKE(2, 0); // signal DMA load done
        C64_POKE(0x0162, run_code);
        C64_POKE(0x00BA, cfg->get_value(CFG_C64_DMA_ID));    // fix drive number

        C64_POKE(0x002D, end_address);
        C64_POKE(0x002E, end_address >> 8);
        C64_POKE(0x002F, end_address);
        C64_POKE(0x0030, end_address >> 8);
        C64_POKE(0x0031, end_address);
        C64_POKE(0x0032, end_address >> 8);
        C64_POKE(0x00AE, end_address);
        C64_POKE(0x00AF, end_address >> 8);

        C64_POKE(0x0090, 0x40); // Load status
        C64_POKE(0x0035, 0);    // FRESPC
        C64_POKE(0x0036, 0xA0);
    }

    printf("Resuming..\n");
    resume();
	
	wait_ms(400);
	set_cartridge(NULL); // reset to default cart

    return 0;
}

bool C64 :: write_vic_state(File *f)
{
    UINT transferred;
    BYTE mode = C64_MODE;
    FRESULT fres;
    C64_MODE = 0;

    // find bank
    BYTE bank = 3 - (cia_backup[1] & 0x03);
    DWORD addr = C64_MEMORY_BASE + (DWORD(bank) << 14);
    if(bank == 0) {
        fres = f->write((BYTE *)C64_MEMORY_BASE, 0x0400, &transferred);
        if(fres != FR_OK)
            goto fail;
        fres = f->write(screen_backup, 0x0400, &transferred);
        if(fres != FR_OK)
            goto fail;
        fres = f->write(ram_backup, 0x0800, &transferred);
        if(fres != FR_OK)
            goto fail;
        fres = f->write((BYTE *)(C64_MEMORY_BASE + 0x1000), 0x3000, &transferred);
        if(fres != FR_OK)
            goto fail;
    } else {        
        fres = f->write((BYTE *)addr, 16384, &transferred);
        if(fres != FR_OK)
            goto fail;
    }    
    fres = f->write(color_backup, 0x0400, &transferred);
    if(fres != FR_OK)
        goto fail;
    fres = f->write(vic_backup, NUM_VICREGS, &transferred);   
    if(fres != FR_OK)
        goto fail;
    
    C64_MODE = mode;
    return true;

fail:
    C64_MODE = mode;
    return false;
}
    
