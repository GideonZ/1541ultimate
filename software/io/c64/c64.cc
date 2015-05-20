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
extern uint8_t _binary_bootcrt_65_start;

cart_def boot_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM }; 

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
                          { FLASH_ID_EPYX,      0x000000, 0x02000,  0x0E },
                          { 0x00,               0x000000, 0x02000,  0x01 | CART_REU | CART_ETH },
                          { 0x00,               0x000000, 0x04000,  0x02 | CART_REU | CART_ETH },
/*
                          { 0x00,               0x000000, 0x80000,  0x08 | CART_REU },
                          { 0x00,               0x000000, 0x80000,  0x0A | CART_REU },
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
#define CFG_C64_ALT_KERN 0xC9
#define CFG_C64_KERNFILE 0xCA
#define CFG_C64_TIMING   0xCB
#define CFG_C64_PHI2_REC 0xCC

char *reu_size[] = { "128 KB", "256 KB", "512 KB", "1 MB", "2 MB", "4 MB", "8 MB", "16 MB" };    
char *en_dis2[] = { "Disabled", "Enabled" };
char *buttons[] = { "Reset|Menu|Freezer", "Freezer|Menu|Reset" };
char *timing1[] = { "20ns", "40ns", "60ns", "80ns", "100ns", "120ns", "140ns", "160ns" };

struct t_cfg_definition c64_config[] = {
    { CFG_C64_CART,     CFG_TYPE_ENUM,   "Cartridge",                    "%s", cart_mode,  0, 15, 4 },
    { CFG_C64_CUSTOM,   CFG_TYPE_STRING, "Custom Cart ROM",              "%s", NULL,       1, 31, (int)"cart.bin" },
    { CFG_C64_ALT_KERN, CFG_TYPE_ENUM,   "Alternate Kernal",             "%s", en_dis2,    0,  1, 0 },
    { CFG_C64_KERNFILE, CFG_TYPE_STRING, "Alternate Kernal File",        "%s", NULL,       1, 36, (int)"kernal.rom" },
    { CFG_C64_REU_EN,   CFG_TYPE_ENUM,   "RAM Expansion Unit",           "%s", en_dis2,    0,  1, 0 },
    { CFG_C64_REU_SIZE, CFG_TYPE_ENUM,   "REU Size",                     "%s", reu_size,   0,  7, 4 },
    { CFG_C64_MAP_SAMP, CFG_TYPE_ENUM,   "Map Ultimate Audio $DF20-DFFF","%s", en_dis2,    0,  1, 0 },
    { CFG_C64_DMA_ID,   CFG_TYPE_VALUE,  "DMA Load Mimics ID:",          "%d", NULL,       8, 31, 8 },
    { CFG_C64_SWAP_BTN, CFG_TYPE_ENUM,   "Button order",                 "%s", buttons,    0,  1, 1 },
    { CFG_C64_TIMING,   CFG_TYPE_ENUM,   "CPU Addr valid after PHI2",    "%s", timing1,    0,  7, 3 },
    { CFG_C64_PHI2_REC, CFG_TYPE_ENUM,   "PHI2 edge recovery",           "%s", en_dis2,    0,  1, 1 },
//    { CFG_C64_ETH_EN,   CFG_TYPE_ENUM,   "Ethernet CS8900A",        "%s", en_dis2,     0,  1, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

#define MENU_C64_RESET      0x6401
#define MENU_C64_REBOOT     0x6402
//#define MENU_C64_TRACE    0x6581
#define MENU_C64_SAVEREU    0x6403
#define MENU_C64_SAVEFLASH  0x6404
#define MENU_C64_BOOTFPGA   0x6408
#define MENU_C64_HARD_BOOT  0x6409

extern uint8_t _binary_chars_bin_start;

    
C64 :: C64()
{
    flash = get_flash();
    register_store(0x43363420, "C64 and cartridge settings", c64_config);

    // char_set = new BYTE[CHARSET_SIZE];
    // flash->read_image(FLASH_ID_CHARS, (void *)char_set, CHARSET_SIZE);
    char_set = (uint8_t *)&_binary_chars_bin_start;
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
        if(getFpgaCapabilities() & CAPAB_SAMPLER) {
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

    C64_PHI2_EDGE_RECOVER = cfg->get_value(CFG_C64_PHI2_REC);
    C64_TIMING_ADDR_VALID = cfg->get_value(CFG_C64_TIMING);
}

bool C64 :: exists(void)
{
    return (C64_CLOCK_DETECT != 0);
}
    
int  C64 :: fetch_task_items(IndexedList<Action *> &item_list)
{
	item_list.append(new Action("Reset C64", C64 :: execute, this, (void *)MENU_C64_RESET));
	item_list.append(new Action("Reboot C64", C64 :: execute, this, (void *)MENU_C64_REBOOT));
    //item_list.append(new Action("Boot Alternate FPGA", C64 :: execute, this, (void *)MENU_C64_BOOTFPGA));
    //item_list.append(new Action("Save SID Trace", C64 :: execute, this, (void *)MENU_C64_TRACE));
    item_list.append(new Action("Save REU Memory", C64 :: execute, this, (void *)MENU_C64_SAVEREU));
    item_list.append(new Action("Hard System Reboot", C64 :: execute, this, (void *)MENU_C64_HARD_BOOT));
    item_list.append(new Action("Save Flash", C64 :: execute, this, (void *)MENU_C64_SAVEFLASH));
	return 4;
}
		
void C64 :: determine_d012(void)
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

void C64 :: resume(void)
{
    int dummy=0;
    uint8_t rast_lo;
    uint8_t rast_hi;
    
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
    
    push_event(e_freeze, this);
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
    push_event(e_unfreeze, this);
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

    uint32_t mem_addr = ((uint32_t)C64_CARTRIDGE_RAM_BASE) << 16;
    if(def->type & CART_RAM) {
        printf("Copying %d bytes from array %p to mem addr %p\n", def->length, def->custom_addr, mem_addr); 
        memcpy((void *)mem_addr, def->custom_addr, def->length);
    } else if(def->id) {
        printf("Requesting copy from Flash, id = %b to mem addr %p\n", def->id, mem_addr);
        flash->read_image(def->id, (void *)mem_addr, def->length);
    } else if (def->length) { // not ram, not flash.. then it has to be custom
        *(uint8_t *)(mem_addr+5) = 0; // disable previously started roms.

        char *n = cfg->get_string(CFG_C64_CUSTOM);
        printf("Now loading '%s' as cartridge.\n", n);

#ifndef _NO_FILE_ACCESS
        FILE *f = fopen(n, "rb");
		if(f) {
			printf("File: %p\n", f);
			UINT transferred;
			transferred = fread((void *)mem_addr, 1, def->length, f);
			if(transferred < 4096) {
				printf("Error loading file.. disabling cart.\n");
				C64_CARTRIDGE_TYPE = 0;
			}
			fclose(f);
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
    BORDER     = uint8_t(border);
    BACKGROUND = uint8_t(background);
}

void C64 :: init_cartridge()
{
    if(!cfg)
        return;

    C64_MODE = C64_MODE_RESET;
    C64_KERNAL_ENABLE = 0;

#ifndef _NO_FILE_ACCESS
    if (cfg->get_value(CFG_C64_ALT_KERN)) {
        char *n = cfg->get_string(CFG_C64_KERNFILE);
        printf("Now loading '%s' as KERNAL ROM.\n", n);

        FILE *f = fopen(n, "rb");
		if(f) {
			UINT transferred;
            uint8_t *temp = new uint8_t[8192];
			transferred = fread(temp, 1, 8192, f);
            C64_KERNAL_ENABLE = 1;
			if (transferred != 8192) {
				printf("Error loading file.. [%d bytes read] disabling custom kernal.\n", transferred);
                C64_KERNAL_ENABLE = 0;
			}
			fclose(f);
            // BYTE *src = (BYTE *)&_binary_kernal_sx_251104_04_bin_start;
            uint8_t *src = temp;
            uint8_t *dst = (uint8_t *)(C64_KERNAL_BASE+1);
            for(int i=0;i<8192;i++) {
                *(dst) = *(src++);
                dst += 2;
            }
            dump_hex((void *)(C64_KERNAL_BASE + 0x3FD0), 48);
            delete[] temp;
		} else {
			printf("Open file failed.\n");
		}
    }
#endif

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
	FILE *f;
	uint32_t size;
    t_flash_address addr;
	int run_code;
    LONG reu_size;
    
	if(e.type == e_dma_load) {
		f = (FILE *)e.object;
		run_code = e.param;
		dma_load(f, run_code);
        if (f) {
            fclose(f);
        }
	} else if((e.type == e_object_private_cmd)&&(e.object == this)) {
		switch(e.param) {
		case C64_EVENT_MAX_REU:
            C64_REU_SIZE = 7;
            C64_REU_ENABLE = 1;
            break;
        case C64_EVENT_AUDIO_ON:
            C64_SAMPLER_ENABLE = 1;
            break;
		default:
			execute(e.param);
		}
	}
#endif
}

// static member
void C64 :: execute(void *obj, void *param)
{
	((C64 *)obj)->execute((int)param);
}

void C64 :: execute(int command)
{
	FILE *f;
	CachedTreeNode *po;
	UINT transferred;
	int ram_size;

	switch(command) {
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
    case MENU_C64_SAVEREU:
//        po = user_interface->get_path(); FIXME
        ram_size = 128 * 1024;
        ram_size <<= cfg->get_value(CFG_C64_REU_SIZE);
        f = fopen("memory.reu", "wb");
        if(f) {
            printf("Opened file successfully.\n");
            transferred = fwrite((void *)REU_MEMORY_BASE, 1, ram_size, f);
            printf("written: %d...", transferred);
            fclose(f);
        } else {
            printf("Couldn't open file..\n");
        }
        break;
    case MENU_C64_SAVEFLASH: // doesn't belong here, but i need it fast
//        po = user_interface->get_path(); FIXME
        f = fopen("flash_dump.bin", "wb");
        if(f) {
            int pages = flash->get_number_of_pages();
            int page_size = flash->get_page_size();
            uint8_t *page_buf = new uint8_t[page_size];
            for(int i=0;i<pages;i++) {
                flash->read_page(i, page_buf);
                transferred = fwrite(page_buf, 1, page_size, f);
            }
            delete[] page_buf;
            fclose(f);
        } else {
            printf("Couldn't open file..\n");
        }
        break;
	case MENU_C64_HARD_BOOT:
		flash->reboot(0);
		break;
	default:
		break;
	}
}

// static member
int C64Event :: prepare_dma_load(FILE *f, const char *name, int len, uint8_t run_code, WORD reloc)
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
int C64Event :: perform_dma_load(FILE *f, uint8_t run_code, WORD reloc)
{
    push_event(e_dma_load, f, run_code);
    return 0;
}



int C64 :: dma_load(FILE *f, uint8_t run_code, WORD reloc)
{
	// First, check if we have file access
    UINT transferred;
	WORD load_address;
    if (f) {
        transferred = fread(&load_address, 1, 2, f);
        printf("Load address: %4x...", load_address);
        if(transferred != 2) {
            printf("Can't read from file..\n");
            return -1;
        }
    }
    load_address = le2cpu(load_address); // make sure we can interpret the word correctly (endianness)
    int block = 510;

    printf("File load location: %4x\n", load_address);
    if(reloc)
    	load_address = reloc;

    int max_length = 65536 - int(load_address); // never exceed $FFFF

	// handshake with boot cart
	stop(false);
	C64_POKE(0x163, cfg->get_value(CFG_C64_DMA_ID));    // drive number for printout

	C64_POKE(2, 0x40);  // signal cart ready for DMA load

	uint8_t dma_load_buffer[512];

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
        uint8_t *dest = (uint8_t *)(C64_MEMORY_BASE + load_address);

        /* Now actually load the file */
        int total_trans = 0;
        while (max_length > 0) {
        	transferred = fread(dma_load_buffer, 1, block, f);
        	total_trans += transferred;
        	for (int i=0;i<transferred;i++) {
        		*(dest++) = dma_load_buffer[i];
        	}
        	if (transferred < block) {
        		break;
        	}
        	max_length -= transferred;
        	block = 512;
        }
        WORD end_address = load_address + total_trans;
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

bool C64 :: write_vic_state(FILE *f)
{
    UINT transferred;
    uint8_t mode = C64_MODE;
    C64_MODE = 0;

    // find bank
    uint8_t bank = 3 - (cia_backup[1] & 0x03);
    uint32_t addr = C64_MEMORY_BASE + (uint32_t(bank) << 14);
    if(bank == 0) {
        fwrite((uint8_t *)C64_MEMORY_BASE, 1, 0x0400, f);
        fwrite(screen_backup, 1, 0x0400, f);
        fwrite(ram_backup, 1, 0x0800, f);
        fwrite((uint8_t *)(C64_MEMORY_BASE + 0x1000), 1, 0x3000, f);
    } else {        
        fwrite((uint8_t *)addr, 1, 16384, f);
    }    
    fwrite(color_backup, 1, 0x0400, f);
    fwrite(vic_backup, 1, NUM_VICREGS, f);
    
    C64_MODE = mode;
    return true;
}
    
