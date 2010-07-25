#include <stdlib.h>
#include <string.h>

extern "C" {
	#include "itu.h"
	#include "dump_hex.h"
}
#include "c1541.h"
#include "disk_image.h"
#include "small_printf.h"
#include "spiflash.h"

//#include "fatfile.h"
#include "filemanager.h"

char *en_dis[] = { "Disabled", "Enabled" };
char *yes_no[] = { "No", "Yes" };
char *rom_sel[] = { "CBM 1541", "1541 C", "1541-II", "Load from file" };
char *ram_board[] = { "Off", "$8000-$BFFF (16K)",
							 "$4000-$7FFF (16K)",
							 "$4000-$BFFF (32K)",
							 "$2000-$3FFF ( 8K)",
							 "$2000-$7FFF (24K)",
							 "$2000-$BFFF (40K)" };

t_1541_ram ram_modes[] = { e_ram_none, e_ram_8000_BFFF, e_ram_4000_7FFF, e_ram_4000_BFFF,
						   e_ram_2000_3FFF, e_ram_2000_7FFF, e_ram_2000_BFFF };

t_1541_rom rom_modes[] = { e_rom_1541, e_rom_1541c, e_rom_1541ii, e_rom_custom };

#define CFG_C1541_POWERED   0xD1
#define CFG_C1541_BUS_ID    0xD2
#define CFG_C1541_ROMSEL    0xD3
#define CFG_C1541_ROMFILE   0xD4
#define CFG_C1541_RAMBOARD  0xD5
#define CFG_C1541_SWAPDELAY 0xD6
#define CFG_C1541_LASTMOUNT 0xD7

struct t_cfg_definition c1541_config[] = {
    { CFG_C1541_POWERED,   CFG_TYPE_ENUM,   "1541 Drive",              "%s", en_dis,     0,  1, 1 },
    { CFG_C1541_BUS_ID,    CFG_TYPE_VALUE,  "1541 Drive Bus ID",       "%d", NULL,       8, 11, 8 },
    { CFG_C1541_ROMSEL,    CFG_TYPE_ENUM,   "1541 ROM Select",         "%s", rom_sel,    0,  3, 2 },
    { CFG_C1541_ROMFILE,   CFG_TYPE_STRING, "1541 ROM File",           "%s", NULL,       0, 36, (int)"SdCard/Part0/1541.rom" },
    { CFG_C1541_RAMBOARD,  CFG_TYPE_ENUM,   "1541 RAM BOard",          "%s", ram_board,  0,  6, 0 },
    { CFG_C1541_SWAPDELAY, CFG_TYPE_VALUE,  "1541 Disk swap delay",    "%d00 ms", NULL,  1, 10, 1 },
//    { CFG_C1541_LASTMOUNT, CFG_TYPE_ENUM,   "Load last mounted disk",  "%s", yes_no,     0,  1, 0 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

#define MENU_1541_RESET  0x1501
#define MENU_1541_REMOVE 0x1502

//--------------------------------------------------------------
// C1541 Drive Class
//--------------------------------------------------------------

C1541 :: C1541(volatile BYTE *regs)
{
    registers  = regs;
    mount_file = NULL;

    cfg = config_manager.register_store((DWORD)regs, "1541 Drive Settings", c1541_config);

    DWORD mem_address = ((DWORD)registers[C1541_MEM_ADDR]) << 16;
    memory_map = (volatile BYTE *)mem_address;
    printf("C1541 Memory address: %p\n", mem_address);

    void *audio_address = (void *)(((DWORD)registers[C1541_AUDIO_ADDR]) << 16);
    printf("C1541 Audio address: %p, loading... \n", audio_address);
    flash.read(FLASH_ADDR_SOUNDS, 0x5000, audio_address);
    
    ram = ram_modes[cfg->get_value(CFG_C1541_RAMBOARD)];
    set_rom(rom_modes[cfg->get_value(CFG_C1541_ROMSEL)], cfg->get_string(CFG_C1541_ROMFILE));

    gcr_image = new GcrImage();
    bin_image = new BinImage();

    volatile ULONG *param = (volatile ULONG *)&registers[C1541_PARAM_RAM];
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	*(param++) = (ULONG)gcr_image->dummy_track;
        *(param++) = 16;
    }
	registers[C1541_SENSOR] = SENSOR_LIGHT;
    registers[C1541_INSERTED] = 0;

    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
    drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);

    disk_state = e_no_disk;

    create_menu_items();
}

C1541 :: ~C1541()
{
	if(gcr_image)
		delete gcr_image;
	if(bin_image)
		delete bin_image;

	if(mount_file)
		root.fclose(mount_file);

	// turn the drive off on destruction
    drive_power(false);
}

void C1541 :: create_menu_items(void)
{
	main_menu_static_items.append(new MenuItemGlobal(this, "Reset 1541 Drive", MENU_1541_RESET));
	main_menu_static_items.append(new MenuItemGlobal(this, "Remove 1541 Disk", MENU_1541_REMOVE));
}

void C1541 :: drive_power(bool on)
{
    registers[C1541_POWER] = on?1:0;
    if(on)
        drive_reset();    
}

void C1541 :: drive_reset(void)
{
    registers[C1541_RESET] = 1;
    wait_ms(1);
    registers[C1541_RESET] = 0;
}

void C1541 :: set_hw_address(int addr)
{
    registers[C1541_HW_ADDR] = BYTE(addr & 3);
    iec_address = 8 + (addr & 3);
}

void C1541 :: set_sw_address(int addr)
{
// EB3A: AD 00 18  LDA $1800       ; read port B
// EB3D: 29 60     AND #$60        ; isolate bits 5 & 6 (device number)
// EB3F: 0A        ASL
// EB40: 2A        ROL
// EB41: 2A        ROL             ; rotate to bit positions 0 & 1
// EB42: 2A        ROL
// EB43: 09 48     ORA #$48        ; add offset from 8 + $40 for TALK
// EB45: 85 78     STA $78         ; device number for TALK (send)
// EB47: 49 60     EOR #$60        ; erase bit 6, set bit 5
// EB49: 85 77     STA $77         ; device number + $20 for LISTEN

    memory_map[0x78] = BYTE((addr & 0x1F) + 0x40);
    memory_map[0x77] = BYTE((addr & 0x1F) + 0x20);
    iec_address = addr & 0x1F;
}

int  C1541 :: get_current_iec_address(void)
{
	if(registers[C1541_POWER]) // if powered, read actual address from its ram
		return int(memory_map[0x78] & 0x1F);

	return iec_address;
}

void C1541 :: set_rom(t_1541_rom rom, char *custom)
{
    large_rom = false;
    File *f;
    UINT transferred;
    FRESULT res;

	current_rom = rom;
	printf("Initializing 1541 rom: ");
    switch(rom) {
        case e_rom_1541:
			printf("CBM1541\n");
            flash.read(FLASH_ADDR_ROM1541, 0x4000, (void *)&memory_map[0xC000]);
            memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
            break;
        case e_rom_1541ii:
			printf("1541-II\n");
            flash.read(FLASH_ADDR_ROM1541II, 0x4000, (void *)&memory_map[0xC000]);
            memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
            break;
        case e_rom_1541c:
			printf("1541C\n");
            flash.read(FLASH_ADDR_ROM1541C, 0x4000, (void *)&memory_map[0xC000]);
            memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
            break;
        default: // custom
            f = root.fopen(custom, FA_READ);
            printf("1541 rom file: %p\n", f);
			if(f) {
				res = f->read((void *)&memory_map[0x8000], 0x8000, &transferred);
				if(res != FR_OK) {
					printf("Error loading file.. default to 1541ii.\n");
					flash.read(FLASH_ADDR_ROM1541II, 0x4000, (void *)&memory_map[0xC000]);
					memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
				}
				root.fclose(f);

				if (transferred > 0x4000) {
					large_rom = true;
				} else {
					memcpy((void *)&memory_map[0xC000], (void *)&memory_map[0x8000], 0x4000);
				}
			} else {
				printf("C1541: Failed to open custom file.\n");
				flash.read(FLASH_ADDR_ROM1541II, 0x4000, (void *)&memory_map[0xC000]);
				memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
			}
    }
    set_ram(ram); // use previous value
    drive_reset();
}

void C1541 :: set_ram(t_1541_ram ram_setting)
{
    ram = ram_setting;
    
    BYTE bank_is_ram = BYTE(ram);
    if(large_rom) {
        bank_is_ram &= 0x0F;
    }
    printf("1541 ram banking: %b\n", bank_is_ram);
    registers[C1541_RAMMAP] = bank_is_ram;
}    

void C1541 :: remove_disk(void)
{
    registers[C1541_INSERTED] = 0;
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(100 * cfg->get_value(CFG_C1541_SWAPDELAY));

    volatile ULONG *param = (volatile ULONG *)&registers[C1541_PARAM_RAM];
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	*(param++) = (ULONG)gcr_image->dummy_track;
        *(param++) = 16;
    }

    registers[C1541_SENSOR] = SENSOR_LIGHT;
    disk_state = e_no_disk;
}

void C1541 :: insert_disk(bool protect, GcrImage *image)
{
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(150);

    volatile ULONG *param = (volatile ULONG *)&registers[C1541_PARAM_RAM];

/*
	if(current_rom != e_rom_1541c) {
		printf("inserting half track for drive other than 1541-C...\n");
		*(param++) = (ULONG)gcr_image->dummy_track;
	    *(param++) = (ULONG)16;
	}
*/
    for(int i=0;i<C1541_MAXTRACKS;i++) {
//    	printf("%2d %08x %08x\n", i, image->track_address[i], image->track_length[i]);
    	*(param++) = (ULONG)image->track_address[i];
        *(param++) = image->track_length[i]-1;
    }            

    if(!protect) {
        registers[C1541_SENSOR] = SENSOR_LIGHT;
    }
    registers[C1541_INSERTED] = 1;
//    image->mounted_on = this;
    disk_state = e_alien_image;
}

void C1541 :: mount_d64(bool protect, File *file)
{
	if(mount_file) {
		root.fclose(mount_file);
	}
	mount_file = file;
	remove_disk();

	printf("Loading...");
	bin_image->load(file);
	printf("Converting...");
	gcr_image->convert_disk_bin2gcr(bin_image);
	printf("Inserting...");
	insert_disk(protect, gcr_image);
	printf("Done\n");
	disk_state = e_d64_disk;
}

void C1541 :: mount_g64(bool protect, File *file)
{
	if(mount_file) {
		root.fclose(mount_file);
	}
	mount_file = file;
	remove_disk();

	printf("Loading...");
	gcr_image->load(file);
	printf("Inserting...");
	insert_disk(protect, gcr_image);
	printf("Done\n");
	disk_state = e_gcr_disk;
}

void C1541 :: poll(Event &e)
{
	int tr;
	int written;
	static int skipped=0;
	bool protect;
	File *f;

	switch(e.type) {
	case e_mount_drv1:
		f = (File *)e.object;
		protect = (bool)e.param;
		mount_d64(protect, f);
		break;
	case e_mount_drv1_gcr:
		f = (File *)e.object;
		protect = (bool)e.param;
		mount_g64(protect, f);
		break;
	case e_unlink_drv1:
		disk_state = e_disk_file_closed;
		if(mount_file) {
			root.fclose(mount_file);
			mount_file = NULL;
		}
		break;
	case e_object_private_cmd:
		if(e.object == this) {
			switch(e.param) {
			case MENU_1541_RESET:
			    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
			    drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);
				break;
			case MENU_1541_REMOVE:
				if(mount_file) {
					root.fclose(mount_file);
					mount_file = NULL;
				}
				remove_disk();
				break;
			default:
				printf("Unhandled menu item for C1541.\n");
			}
		}
		break;
	default:
		break;
	}

//	printf("%02d ", registers[C1541_TRACK]);

	if(!mount_file) {
		registers[C1541_ANYDIRTY] = 0;  // clear flag so we won't come back here endlessly
		return;
	}
    if(!mount_file->node) {
        printf("C1541: File was invalidated..\n");
        disk_state = e_disk_file_closed;
        root.fclose(mount_file);
        mount_file = NULL;
    }

	if((registers[C1541_ANYDIRTY])||(skipped)) {
		if(!skipped) {
			registers[C1541_ANYDIRTY] = 0;  // clear flag once we didn't skip anything anymore
		}
		skipped = 0;
		for(tr=0,written=0;tr<C1541_MAXTRACKS/2;tr++) {
			if(registers[C1541_DIRTYFLAGS + tr]) {
				if((!(registers[C1541_STATUS] & DRVSTAT_MOTOR)) || ((registers[C1541_TRACK] >> 1) != tr)) {
					registers[C1541_DIRTYFLAGS + tr] = 0;
					switch(disk_state) {
					case e_gcr_disk:
						printf("Writing back GCR track %d.0...\n", tr+1);
						gcr_image->write_track(tr*2, mount_file);
						break;
					case e_d64_disk:
						printf("Writing back binary track %d...\n", tr+1);
						bin_image->write_track(tr, gcr_image, mount_file);
						break;
                    case e_disk_file_closed:
                        printf("Track %d cant be written back to closed file. Lost..\n", tr+1);
                        break;
					default:
						printf("Diskstate error, can't output track %d.\n", tr+1);
					}
					written++;
				} else {
					skipped ++;
				}
			}
		}
		if(written)
			if(mount_file->fs->sync() != FR_OK)
				printf("Error syncing file.\n");
	}
}
