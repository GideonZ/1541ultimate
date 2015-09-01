#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "itu.h"
#include "dump_hex.h"
#include "c1541.h"
#include "c64.h"
#include "disk_image.h"
#include "userinterface.h"
#include "filemanager.h"

const char *en_dis[] = { "Disabled", "Enabled" };
const char *yes_no[] = { "No", "Yes" };
const char *rom_sel[] = { "CBM 1541", "1541 C", "1541-II", "Load from file" };
const char *ram_board[] = { "Off", "$8000-$BFFF (16K)",
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
#define CFG_C1541_C64RESET  0xD8
#define CFG_C1541_GCRALIGN  0xDA
#define CFG_C1541_STOPFREEZ 0xDB

struct t_cfg_definition c1541_config[] = {
    { CFG_C1541_POWERED,   CFG_TYPE_ENUM,   "1541 Drive",                 "%s", en_dis,     0,  1, 1 },
    { CFG_C1541_BUS_ID,    CFG_TYPE_VALUE,  "1541 Drive Bus ID",          "%d", NULL,       8, 11, 8 },
    { CFG_C1541_ROMSEL,    CFG_TYPE_ENUM,   "1541 ROM Select",            "%s", rom_sel,    0,  3, 2 },
    { CFG_C1541_ROMFILE,   CFG_TYPE_STRING, "1541 ROM File",              "%s", NULL,       1, 36, (int)"1541.rom" },
    { CFG_C1541_RAMBOARD,  CFG_TYPE_ENUM,   "1541 RAM BOard",             "%s", ram_board,  0,  6, 0 },
    { CFG_C1541_SWAPDELAY, CFG_TYPE_VALUE,  "1541 Disk swap delay",       "%d00 ms", NULL,  1, 10, 1 },
    { CFG_C1541_C64RESET,  CFG_TYPE_ENUM,   "1541 Resets when C64 resets","%s", yes_no,     0,  1, 1 },
    { CFG_C1541_STOPFREEZ, CFG_TYPE_ENUM,   "1541 Freezes in menu",       "%s", yes_no,     0,  1, 1 },
//    { CFG_C1541_GCRTWEAK,  CFG_TYPE_VALUE,  "GCR Image Save Tweak",       "%d", NULL,      -4,  4, 0 },
    { CFG_C1541_GCRALIGN,  CFG_TYPE_ENUM,   "GCR Save Align Tracks",      "%s", yes_no,     0,  1, 1 },
    
//    { CFG_C1541_LASTMOUNT, CFG_TYPE_ENUM,   "Load last mounted disk",  "%s", yes_no,     0,  1, 0 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

extern uint8_t _1541_bin_start;
extern uint8_t _1541c_bin_start;
extern uint8_t _1541_ii_bin_start;
extern uint8_t _sounds_bin_start;

//--------------------------------------------------------------
// C1541 Drive Class
//--------------------------------------------------------------
C1541 :: C1541(volatile uint8_t *regs, char letter) : SubSystem((letter == 'A')?SUBSYSID_DRIVE_A : SUBSYSID_DRIVE_B)
{
    registers  = regs;
    mount_file = NULL;
    drive_letter = letter;
    current_rom = e_rom_unset;
	flash = get_flash();
    fm = FileManager :: getFileManager();

    char buffer[32];
    c1541_config[0].def = (letter == 'A')?1:0;   // drive A is default 8, drive B is default 9, etc
    c1541_config[1].def = 8 + int(letter - 'A'); // only drive A is by default ON.
        
    uint32_t mem_address = ((uint32_t)registers[C1541_MEM_ADDR]) << 16;
    memory_map = (volatile uint8_t *)mem_address;
    printf("C1541 Memory address: %p\n", mem_address);

    write_skip = 0;
    
	if(flash) {
	    void *audio_address = (void *)(((uint32_t)registers[C1541_AUDIO_ADDR]) << 16);
	    printf("C1541 Audio address: %p, loading... \n", audio_address);
//	    flash->read_image(FLASH_ID_SOUNDS, audio_address, 0x4800);
        memcpy(audio_address, &_sounds_bin_start, 0x4800);
	}    
    drive_name = "Drive ";
    drive_name += letter;

    gcr_image = new GcrImage();
    bin_image = new BinImage(drive_name.c_str());
    
    sprintf(buffer, "1541 Drive %c Settings", letter);    
    register_store((uint32_t)regs, buffer, c1541_config);

    disk_state = e_no_disk;
    iec_address = 8 + int(letter - 'A');
    ram = e_ram_none;
    large_rom = false;

    taskHandle = 0;
}

C1541 :: ~C1541()
{
	if(gcr_image)
		delete gcr_image;
	if(bin_image)
		delete bin_image;

	if(mount_file)
		fm->fclose(mount_file);

	// turn the drive off on destruction
    drive_power(false);

    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }
}

void C1541 :: effectuate_settings(void)
{
	if(!lock(this->drive_name.c_str()))
		return;

	printf("Effectuate 1541 settings:\n");
    ram = ram_modes[cfg->get_value(CFG_C1541_RAMBOARD)];
    set_ram(ram);

    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
    set_sw_address(cfg->get_value(CFG_C1541_BUS_ID));

    t_1541_rom rom = rom_modes[cfg->get_value(CFG_C1541_ROMSEL)];
    if((rom != current_rom)||
       (rom == e_rom_custom)) { // && ( check if the name has changed

        set_rom(rom, cfg->get_string(CFG_C1541_ROMFILE));
    }

    if(registers[C1541_POWER] != cfg->get_value(CFG_C1541_POWERED)) {
        drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);
    }

    unlock();
}


void C1541 :: init(void)
{
    printf("Init C1541. Cfg = %p\n", cfg);
    
    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	*(param++) = (uint32_t)gcr_image->dummy_track;
        *(param++) = 0x01450010;
    }
	registers[C1541_SENSOR] = SENSOR_LIGHT;
    registers[C1541_INSERTED] = 0;
    disk_state = e_no_disk;

	xTaskCreate( C1541 :: run, (const char *)(this->drive_name.c_str()), configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &taskHandle );
    effectuate_settings();
}

int  C1541 :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
	int items = 1;
    char buffer[32];
    sprintf(buffer, "Reset 1541 Drive %c", drive_letter);
	item_list.append(new Action(buffer, getID(), MENU_1541_RESET, 0));

	if(disk_state != e_no_disk) {
        sprintf(buffer, "Remove disk from Drive %c", drive_letter);
		item_list.append(new Action(buffer, getID(), MENU_1541_REMOVE, 0));
		items++;

		if (fm->is_path_writable(path))
        {
            sprintf(buffer, "Save disk in drive %c as D64", drive_letter);
    		item_list.append(new Action(buffer, getID(), MENU_1541_SAVED64, 0));
    		items++;
    
            sprintf(buffer, "Save disk in drive %c as G64", drive_letter);
    		item_list.append(new Action(buffer, getID(), MENU_1541_SAVEG64, 0));
    		items++;
    	}
	} else {
        sprintf(buffer, "Insert blank disk in drive %c", drive_letter);
		item_list.append(new Action(buffer, getID(), MENU_1541_BLANK, 0));
		items++;
    }	    
	return items;
}

void C1541 :: drive_power(bool on)
{
    registers[C1541_POWER] = on?1:0;
    if(on)
        drive_reset();
    registers[C1541_ANYDIRTY] = 0;
}

void C1541 :: drive_reset(void)
{
    registers[C1541_RESET] = 1;
    wait_ms(1);
    uint8_t reg = 0;
    
    if(cfg->get_value(CFG_C1541_STOPFREEZ))
        reg |= 4;
    if(cfg->get_value(CFG_C1541_C64RESET))
        reg |= 2;

    registers[C1541_RESET] = reg;
}

void C1541 :: set_hw_address(int addr)
{
    registers[C1541_HW_ADDR] = uint8_t(addr & 3);
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

    memory_map[0x78] = uint8_t((addr & 0x1F) + 0x40);
    memory_map[0x77] = uint8_t((addr & 0x1F) + 0x20);
    iec_address = addr & 0x1F;
}

int  C1541 :: get_current_iec_address(void)
{
	if(registers[C1541_POWER]) // if powered, read actual address from its ram
		return int(memory_map[0x78] & 0x1F);

	return iec_address;
}

void C1541 :: set_rom(t_1541_rom rom, const char *custom_filename)
{
    large_rom = false;
    File *f = 0;
    FRESULT res;
    uint32_t transferred;
	int offset;
	
	current_rom = rom;
	printf("Initializing 1541 rom: ");
	if(!flash) {
		printf("no flash.\n");
		return;
	}

    switch(rom) {
        case e_rom_1541:
			printf("CBM1541\n");
//            flash->read_image(FLASH_ID_ROM1541, (void *)&memory_map[0xC000], 0x4000);
            memcpy((void *)&memory_map[0xC000], &_1541_bin_start, 0x4000);
            break;
        case e_rom_1541ii:
			printf("1541-II\n");
//            flash->read_image(FLASH_ID_ROM1541II, (void *)&memory_map[0xC000], 0x4000);
            memcpy((void *)&memory_map[0xC000], &_1541_ii_bin_start, 0x4000);
            break;
        case e_rom_1541c:
			printf("1541C\n");
//            flash->read_image(FLASH_ID_ROM1541C, (void *)&memory_map[0xC000], 0x4000);
            memcpy((void *)&memory_map[0xC000], &_1541c_bin_start, 0x4000);
            break;
        default: // custom
            res = fm->fopen((const char *)NULL, custom_filename, FA_READ, &f);
            printf("1541 rom file: %p (%d)\n", f, res);
			if(res == FR_OK) {
				uint32_t size = f->get_size();
				if (size > 0x8000)
					offset = 0x8000;
				else
					offset = 0x10000 - size;

//				flash->read_image(FLASH_ID_ROM1541II, (void *)&memory_map[0xC000], 0x4000);
                memcpy((void *)&memory_map[0xC000], &_1541_ii_bin_start, 0x4000);
				f->read((void *)&memory_map[offset], 0x8000, &transferred);
				fm->fclose(f);
				if(transferred > 0x4000) {
					large_rom = true;
				}
			} else {
				printf("C1541: Failed to open custom file.\n");
//				flash->read_image(FLASH_ID_ROM1541II, (void *)&memory_map[0xC000], 0x4000);
                memcpy((void *)&memory_map[0xC000], &_1541_ii_bin_start, 0x4000);
			}
    }
	if(!large_rom) // if rom <= 16K, then mirror it
		memcpy((void *)&memory_map[0x8000], (void *)&memory_map[0xC000], 0x4000);
	
    set_ram(ram); // use previous value
    drive_reset();
}

void C1541 :: set_ram(t_1541_ram ram_setting)
{
    ram = ram_setting;
    
    uint8_t bank_is_ram = uint8_t(ram);
    if(large_rom) {
        bank_is_ram &= 0x0F;
    }
    printf("1541 ram banking: %b\n", bank_is_ram);
    registers[C1541_RAMMAP] = bank_is_ram;
}    

void C1541 :: check_if_save_needed(SubsysCommand *cmd)
{
    printf("## Checking if disk change is needed: %c %d %d\n", drive_letter, registers[C1541_ANYDIRTY], disk_state);
	if(!((registers[C1541_ANYDIRTY])||(write_skip))) {
        return;
    }
    if(disk_state == e_no_disk) {
        return;
    }
	if(cmd->user_interface->popup("About to remove a changed disk. Save?", BUTTON_YES|BUTTON_NO) == BUTTON_NO) {
	    return;
    }
	cmd->mode = (disk_state == e_gcr_disk) ? 1 : 0;
	save_disk_to_file(cmd);
}

void C1541 :: remove_disk(void)
{
    registers[C1541_INSERTED] = 0;
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(100 * cfg->get_value(CFG_C1541_SWAPDELAY));

    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	*(param++) = (uint32_t)gcr_image->dummy_track;
        *(param++) = 0x01450010;
    }

    registers[C1541_SENSOR] = SENSOR_LIGHT;
    disk_state = e_no_disk;
}

void C1541 :: insert_disk(bool protect, GcrImage *image)
{
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(150);

    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];

    uint32_t rotation_speed = 2500000; // 1/8 track time => 10 ns steps
    for(int i=0;i<C1541_MAXTRACKS;i++) {
        uint32_t bit_time = rotation_speed / image->track_length[i];
    	// printf("%2d %08x %08x %d\n", i, image->track_address[i], image->track_length[i], bit_time);
    	*(param++) = (uint32_t)image->track_address[i];
        *(param++) = (image->track_length[i]-1) | (bit_time << 16);
        registers[C1541_DIRTYFLAGS + i/2] = 0;
    }            
	registers[C1541_ANYDIRTY] = 0;

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
		fm->fclose(mount_file);
	}
	mount_file = file;
	remove_disk();

	printf("Loading...");
	bin_image->load(file);
	printf("Converting...");
	gcr_image->convert_disk_bin2gcr(bin_image, false);
	printf("Inserting...");
	insert_disk(protect, gcr_image);
	printf("Done\n");
	disk_state = e_d64_disk;
}

void C1541 :: mount_g64(bool protect, File *file)
{
	if(mount_file) {
		fm->fclose(mount_file);
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

void C1541 :: mount_blank()
{
	if(mount_file) {
		fm->fclose(mount_file);
	}
	mount_file = NULL;
	remove_disk();
    wait_ms(250);
	gcr_image->blank();
	printf("Inserting blank disk...");
	insert_disk(false, gcr_image);
	printf("Done\n");
	disk_state = e_disk_file_closed;
}

// static member
void C1541 :: run(void *a)
{
	C1541 *drv = (C1541 *)a;
	while(1) {
		if(drv->lock(drv->drive_name.c_str())) {
			drv->poll();
			drv->unlock();
		}
		vTaskDelay(50);
	}
}

void C1541 :: poll() // called under mutex
{
	int tr;
	int written;

//	printf("%02d ", registers[C1541_TRACK]);

	if(!mount_file) {
		return;
	}
	if (!mount_file->isValid()) {
        printf("C1541: File was invalidated..\n");
        disk_state = e_disk_file_closed;
        fm->fclose(mount_file);
        mount_file = NULL;
        return;
    }
    if((disk_state != e_gcr_disk)&&(disk_state != e_d64_disk)) {
        return;
    }

	if((registers[C1541_ANYDIRTY])||(write_skip)) {
		if(!write_skip) {
			registers[C1541_ANYDIRTY] = 0;  // clear flag once we didn't skip anything anymore
		}
		write_skip = 0;
		for(tr=0,written=0;tr<C1541_MAXTRACKS/2;tr++) {
			if(registers[C1541_DIRTYFLAGS + tr]) {
				if((!(registers[C1541_STATUS] & DRVSTAT_MOTOR)) || ((registers[C1541_TRACK] >> 1) != tr)) {
					registers[C1541_DIRTYFLAGS + tr] = 0;
					switch(disk_state) {
					case e_gcr_disk:
						printf("Writing back GCR track %d.0...\n", tr+1);
						gcr_image->write_track(tr*2, mount_file, cfg->get_value(CFG_C1541_GCRALIGN));
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
//                    printf("C1541 writeback: Skip: TR %d. CUR %d. ST: %b\n", tr, registers[C1541_TRACK] >> 1, registers[C1541_STATUS]);
					write_skip ++;
				}
			}
		}
	}
}

int C1541 :: executeCommand(SubsysCommand *cmd)
{
	bool g64;
	bool protect;
	uint8_t flags = 0;
	File *newFile = 0;
	FRESULT res;
	FileInfo info;

	switch(cmd->functionID) {
	case D64FILE_MOUNT_UL:
	case G64FILE_MOUNT_UL:
	case D64FILE_MOUNT_RO:
	case G64FILE_MOUNT_RO:
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case G64FILE_RUN:
	case G64FILE_MOUNT:
	case MENU_1541_MOUNT:
	case MENU_1541_MOUNT_GCR:
		fm->fstat(info, cmd->path.c_str(), cmd->filename.c_str());
        break;
	default:
		break;
	}

	switch(cmd->functionID) {
	case D64FILE_MOUNT_UL:
	case G64FILE_MOUNT_UL:
		protect = false;
		flags = FA_READ;
		break;
	case D64FILE_MOUNT_RO:
	case G64FILE_MOUNT_RO:
		protect = true;
		flags = FA_READ;
		break;
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case G64FILE_RUN:
	case G64FILE_MOUNT:
	case MENU_1541_MOUNT:
	case MENU_1541_MOUNT_GCR:
		protect = (info.attrib & AM_RDO);
        flags = (protect)?(FA_READ):(FA_READ | FA_WRITE);
        break;
	default:
		break;
	}


	SubsysCommand *c64_command;

	switch(cmd->functionID) {
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case D64FILE_MOUNT_UL:
	case D64FILE_MOUNT_RO:
		printf("Mounting disk.. %s\n", cmd->filename.c_str());
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), flags, &newFile);
		if(res == FR_OK) {
			check_if_save_needed(cmd);
            mount_d64(protect, newFile);
            if ((cmd->functionID == D64FILE_MOUNT_UL) ||
            	(cmd->functionID == D64FILE_MOUNT_RO)) {
            	unlink();
            }
            if(cmd->functionID == D64FILE_RUN) {
                c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,
                		C64_DRIVE_LOAD, RUNCODE_MOUNT_LOAD_RUN, "", "*");
                c64_command->execute();
            } else {
                c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,
                		C64_UNFREEZE, 0, "", "");
                c64_command->execute();
            }
		} else {
			printf("Error opening file.\n");
		}
		break;
	case G64FILE_RUN:
	case G64FILE_MOUNT:
	case G64FILE_MOUNT_UL:
	case G64FILE_MOUNT_RO:
		printf("Mounting disk.. %s\n", cmd->filename.c_str());
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), flags, &newFile);
		if(res == FR_OK) {
			check_if_save_needed(cmd);
            mount_g64(protect, newFile);
            if ((cmd->functionID == G64FILE_MOUNT_UL) ||
            	(cmd->functionID == G64FILE_MOUNT_RO)) {
            	unlink();
            }
            if(cmd->functionID == G64FILE_RUN) {
                c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,
                		C64_DRIVE_LOAD, RUNCODE_MOUNT_LOAD_RUN, "", "*");
                c64_command->execute();
            } else {
                c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,
                		C64_UNFREEZE, 0, "", "");
                c64_command->execute();
            }
		} else {
			printf("Error opening file.\n");
		}
		break;
	case MENU_1541_MOUNT:
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), flags, &newFile);
		mount_d64(protect, newFile);
		break;
	case MENU_1541_MOUNT_GCR:
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), flags, &newFile);
		mount_g64(protect, newFile);
		break;
	case MENU_1541_UNLINK:
		unlink();
		break;
	case MENU_1541_RESET:
	    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
	    drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);
		break;
	case MENU_1541_REMOVE:
        check_if_save_needed(cmd);
		if(mount_file) {
			fm->fclose(mount_file);
			mount_file = NULL;
		}
		remove_disk();
		break;
    case MENU_1541_BLANK:
        mount_blank();
        break;
    case MENU_1541_SAVED64:
    	cmd->mode = 0;
    	save_disk_to_file(cmd);
        break;
    case MENU_1541_SAVEG64:
    	cmd->mode = 1;
        save_disk_to_file(cmd);
        break;
	default:
		printf("Unhandled menu item for C1541.\n");
		return -1;
	}
	return 0;
}

void C1541 :: unlink(void)
{
	disk_state = e_disk_file_closed;
	if(mount_file) {
		fm->fclose(mount_file);
		mount_file = NULL;
	}
}

void C1541 :: save_disk_to_file(SubsysCommand *cmd)
{
    static char buffer[32];
	File *file = 0;
	FRESULT fres;
	int res;

	res = cmd->user_interface->string_box("Give name for image file..", buffer, 24);
	if(res > 0) {
		set_extension(buffer, (cmd->mode)?(char *)".g64":(char *)".d64", 32);
		fix_filename(buffer);
		fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW, &file);
		if(res == FR_OK) {
			if(cmd->mode) {
				cmd->user_interface->show_progress("Saving G64...", 84);
				gcr_image->save(file, (cfg->get_value(CFG_C1541_GCRALIGN)!=0), cmd->user_interface);
				cmd->user_interface->hide_progress();
			} else {
				cmd->user_interface->show_progress("Converting disk...", 2*bin_image->num_tracks);
				gcr_image->convert_disk_gcr2bin(bin_image, cmd->user_interface);
				cmd->user_interface->update_progress("Saving D64...", 0);
				bin_image->save(file, cmd->user_interface);
				cmd->user_interface->hide_progress();
			}
			fm->fclose(file);
		} else {
			cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
		}
	}
}
