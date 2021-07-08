#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>

#include "itu.h"
#include "dump_hex.h"
#include "c1541.h"
#include "c64.h"
#include "disk_image.h"
#include "userinterface.h"
#include "filemanager.h"
#include "file_ram.h"

#ifndef CLOCK_FREQ
#define CLOCK_FREQ 50000000
#warning "Clock frequency should be set in makefile, using -DCLOCK_FREQ=nnnnn. Now 50 MHz is assumed."
#endif

static const char *yes_no[] = { "No", "Yes" };

static const char *drive_types[] = { "1541", "1571", "1581" };

#define CFG_C1541_POWERED   0xD1
#define CFG_C1541_BUS_ID    0xD2
#define CFG_C1541_ROMFILE0  0xD4
#define CFG_C1541_RAMBOARD  0xD5
#define CFG_C1541_SWAPDELAY 0xD6
#define CFG_C1541_LASTMOUNT 0xD7
#define CFG_C1541_C64RESET  0xD8
#define CFG_C1541_DRIVETYPE 0xD9
#define CFG_C1541_GCRALIGN  0xDA
#define CFG_C1541_STOPFREEZ 0xDB
#define CFG_C1541_ROMFILE1  0xDC
#define CFG_C1541_ROMFILE2  0xDD
#define CFG_C1541_EXTRARAM  0xDE

const struct t_cfg_definition c1541_config[] = {
    { CFG_C1541_POWERED,   CFG_TYPE_ENUM,   "Drive",                      "%s", en_dis,     0,  1, 1 },
    { CFG_C1541_DRIVETYPE, CFG_TYPE_ENUM,   "Drive Type",                 "%s", drive_types,0,  2, 0 },
    { CFG_C1541_BUS_ID,    CFG_TYPE_VALUE,  "Drive Bus ID",               "%d", NULL,       8, 11, 8 },
    { CFG_C1541_ROMFILE0,  CFG_TYPE_STRFUNC,"ROM for 1541 mode",          "%s", (const char **)C1541 :: list_roms,  1, 32, (int)"1541.rom" },
    { CFG_C1541_ROMFILE1,  CFG_TYPE_STRFUNC,"ROM for 1571 mode",          "%s", (const char **)C1541 :: list_roms,  1, 32, (int)"1571.rom" },
    { CFG_C1541_ROMFILE2,  CFG_TYPE_STRFUNC,"ROM for 1581 mode",          "%s", (const char **)C1541 :: list_roms,  1, 32, (int)"1581.rom" },
    { CFG_C1541_EXTRARAM,  CFG_TYPE_ENUM,   "Extra RAM",                  "%s", en_dis,     0,  1, 0 },
    { CFG_C1541_SWAPDELAY, CFG_TYPE_VALUE,  "Disk swap delay",            "%d00 ms", NULL,  1, 10, 1 },
    { CFG_C1541_C64RESET,  CFG_TYPE_ENUM,   "Resets when C64 resets",     "%s", yes_no,     0,  1, 1 },
    { CFG_C1541_STOPFREEZ, CFG_TYPE_ENUM,   "Freezes in menu",            "%s", yes_no,     0,  1, 1 },
    { CFG_C1541_GCRALIGN,  CFG_TYPE_ENUM,   "GCR Save Align Tracks",      "%s", yes_no,     0,  1, 1 },
    
//    { CFG_C1541_LASTMOUNT, CFG_TYPE_ENUM,   "Load last mounted disk",  "%s", yes_no,     0,  1, 0 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};


//--------------------------------------------------------------
// C1541 Drive Class
//--------------------------------------------------------------

C1541 :: C1541(volatile uint8_t *regs, char letter) : SubSystem((letter == 'A')?SUBSYSID_DRIVE_A : SUBSYSID_DRIVE_B)
{
    registers  = regs;
    mount_file = NULL;
    drive_letter = letter;
	flash = get_flash();
    fm = FileManager :: getFileManager();
    current_drive_type = e_dt_unset;
    
    // Create local copy of configuration definition, since the default differs.
    int size = sizeof(c1541_config);
    int elements = size / sizeof(t_cfg_definition);
    local_config_definitions = new t_cfg_definition[elements];
    memcpy(local_config_definitions, c1541_config, size);

    char buffer[32];
    local_config_definitions[0].def = (letter == 'A')?1:0;   // drive A is default 8, drive B is default 9, etc
    local_config_definitions[2].def = 8 + int(letter - 'A'); // only drive A is by default ON.
        
    uint32_t mem_address = ((uint32_t)registers[C1541_MEM_ADDR]) << 16;
    memory_map = (volatile uint8_t *)mem_address;

    audio_address = (uint8_t *)(((uint32_t)registers[C1541_AUDIO_ADDR]) << 16);
    if (registers[C1541_DRIVETYPE] & 0x80) {
        audio_address += 0x8000;
    }
    printf("C1541 Memory address: %p\n", mem_address);
    printf("C1541 Audio address: %p\n", audio_address);

    drive_name = "Drive ";
    drive_name += letter;

    gcr_image = new GcrImage();
    gcr_image_up_to_date = true;
    bin_image = new BinImage(drive_name.c_str(), 70); // Maximum size for .d71
    mfm_controller = new WD177x(regs + C1541_WD177X, regs, 1 + (letter - 'A'));
    gcr_ram_file = new FileRAM(gcr_image->gcr_data, GCRIMAGE_MAXSIZE, false); // How cool is this?!
    write_skip = 0;
    
    sprintf(buffer, "Drive %c Settings", letter);
    register_store((uint32_t)regs, buffer, local_config_definitions);
    sprintf(buffer, "Drive %c", drive_letter);
    taskItemCategory = TasksCollection :: getCategory(buffer, SORT_ORDER_DRIVES + drive_letter - 'A');

    disk_state = e_no_disk;
    iec_address = 8 + int(letter - 'A');
    dummy_track = NULL;

    taskHandle = 0;

    registers[C1541_DRIVETYPE] = 2;
    multi_mode = ((registers[C1541_DRIVETYPE] & 3) == 2);

    if (!multi_mode) {
        cfg->disable(CFG_C1541_ROMFILE1);
        cfg->disable(CFG_C1541_ROMFILE2);
    }
}

C1541 :: ~C1541()
{
	if(gcr_image)
		delete gcr_image;
	if(bin_image)
		delete bin_image;
	if(dummy_track)
	    delete[] dummy_track;
	if (mfm_controller) {
	    delete mfm_controller;
	}
	if (gcr_ram_file) {
	    gcr_ram_file->close(); // not via FileManager; it is not a public file
	}
	if(mount_file)
		fm->fclose(mount_file);

	// turn the drive off on destruction
    drive_power(false);

    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }
}

// private static member can only be read through getter
C1541* C1541 :: last_mounted_drive;

C1541* C1541 :: get_last_mounted_drive() {
    return last_mounted_drive;
}

void C1541 :: effectuate_settings(void)
{
	if(!lock(this->drive_name.c_str())) {
	    printf("** ERROR ** Effectuate drive settings could not be executed, as the drive was locked.\n");
        return;
	}

	printf("Effectuate 1541 settings:\n");

	uint8_t mir = cfg->get_value(CFG_C1541_EXTRARAM);
    registers[C1541_RAMMAP] = mir ? 0x80 : 0x00;

    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
    set_sw_address(cfg->get_value(CFG_C1541_BUS_ID));

    current_drive_type = e_dt_unset;
    uint8_t dt = cfg->get_value(CFG_C1541_DRIVETYPE);
    FRESULT res = set_drive_type((t_drive_type) dt);

    if(registers[C1541_POWER] != cfg->get_value(CFG_C1541_POWERED)) {
        drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);
    }
    if (res != FR_OK) {
        drive_power(false);
    }

    unlock();
}


void C1541 :: init(void)
{
    printf("Init C1541. Cfg = %p\n", cfg);
    
    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];
    uint32_t rotation_speed = (CLOCK_FREQ / 20); // 2 (half clocks) * 1/8 (bytes) * clocks per track. 300 RPM = 5 RPS. (5 * 8 / 2) = 20
    uint32_t bit_time = rotation_speed / GCRIMAGE_DUMMYTRACKLEN;

    dummy_track = new uint8_t[GCRIMAGE_DUMMYTRACKLEN];
    memset(dummy_track, 0, GCRIMAGE_DUMMYTRACKLEN);

    // Fill parameter memory with valid pointers; all pointing to an empty dummy track.
    for(int i=0;i<256;i++) {
    	*(param++) = (uint32_t)dummy_track;
        *(param++) = (bit_time << 16) | 0x100;
    }

    registers[C1541_SENSOR] = SENSOR_LIGHT;
    registers[C1541_INSERTED] = 0;
    disk_state = e_no_disk;

    mfm_controller->init();
    effectuate_settings();

	xTaskCreate( C1541 :: run, (const char *)(this->drive_name.c_str()), configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &taskHandle );
}


void C1541 :: create_task_items(void)
{
    myActions.reset   = new Action("Reset",        getID(), MENU_1541_RESET, 0);
    myActions.remove  = new Action("Remove Disk",  getID(), MENU_1541_REMOVE, 0);
    myActions.saved64 = new Action("Save as Binary",  getID(), MENU_1541_SAVED64, 0);
    myActions.saveg64 = new Action("Save as GCR",  getID(), MENU_1541_SAVEG64, 0);
    myActions.blank   = new Action("Insert Blank", getID(), MENU_1541_BLANK, 0);
    myActions.turnon  = new Action("Turn On",      getID(), MENU_1541_TURNON, 0);
    myActions.turnoff = new Action("Turn Off",     getID(), MENU_1541_TURNOFF, 0);
    myActions.mode41  = new Action("Switch to 1541", getID(), MENU_1541_SET_MODE, 0);
    myActions.mode71  = new Action("Switch to 1571", getID(), MENU_1541_SET_MODE, 1);
    myActions.mode81  = new Action("Switch to 1581", getID(), MENU_1541_SET_MODE, 2);

    taskItemCategory->append(myActions.turnon);
    taskItemCategory->append(myActions.turnoff);
    taskItemCategory->append(myActions.reset);
    taskItemCategory->append(myActions.mode41);
    taskItemCategory->append(myActions.mode71);
    taskItemCategory->append(myActions.mode81);
    taskItemCategory->append(myActions.remove);
    taskItemCategory->append(myActions.saved64);
    taskItemCategory->append(myActions.saveg64);
    taskItemCategory->append(myActions.blank);
}

void C1541 :: update_task_items(bool writablePath, Path *p)
{
    myActions.remove->hide();
    myActions.saved64->hide();
    myActions.saveg64->hide();
    myActions.blank->hide();
    myActions.reset->hide();
    myActions.turnon->hide();
    if (multi_mode) {
        myActions.mode41->show();
        myActions.mode71->show();
        myActions.mode81->show();
    } else {
        myActions.mode41->hide();
        myActions.mode71->hide();
        myActions.mode81->hide();
    }

    // don't show items for disabled drives
    if ((registers[C1541_POWER] & 1) == 0) {
        myActions.turnon->show();
        myActions.turnoff->hide();
        return;
    }

    myActions.turnoff->show();
    myActions.reset->show();

    switch(current_drive_type) {
        case e_dt_1541:
            myActions.mode41->hide();
            break;
        case e_dt_1571:
            myActions.mode71->hide();
            break;
        case e_dt_1581:
            myActions.mode81->hide();
            break;
    }

    if(disk_state == e_no_disk) {
        if (current_drive_type != e_dt_1581) {
            // Mounting a blank disk on a 1581 is not possible; a file is always required
            myActions.blank->show();
        }
    } else { // there is a disk in the drive
        myActions.remove->show();
        if (current_drive_type != e_dt_1581) {
            myActions.saved64->show();
            myActions.saveg64->show();
        }
    }
    if(writablePath) {
        myActions.saved64->enable();
        myActions.saveg64->enable();
    } else {
        myActions.saved64->disable();
        myActions.saveg64->disable();
    }
}

void C1541 :: drive_power(bool on)
{
    registers[C1541_POWER] = on?1:0;
    if(on)
        drive_reset(1);
    registers[C1541_DIRTYFLAGS] = 0x80;
}

bool C1541 :: get_drive_power() 
{
	return registers[C1541_POWER];
}

void C1541 :: drive_reset(uint8_t doit)
{
    registers[C1541_RESET] = doit;
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
/*
	if(registers[C1541_POWER]) // if powered, read actual address from its ram
		return int(memory_map[0x78] & 0x1F);
*/
	return iec_address;
}

bool C1541 :: check_if_save_needed(SubsysCommand *cmd)
{
    if ((disk_state == e_gcr_disk) && (!gcr_image_up_to_date)) {
        return (cmd->user_interface->popup("Not all tracks were saved. Save disk?", BUTTON_YES|BUTTON_NO) == BUTTON_YES);
    }

    printf("## Checking if disk change is needed: %c %d %d\n", drive_letter, registers[C1541_DIRTYFLAGS], disk_state);
    if(!((registers[C1541_DIRTYFLAGS] & 0x80)||(write_skip)||(are_mfm_dirty_bits_set()))) {
        return false;
    }
    if(disk_state == e_no_disk) {
        return false;
    }
    if(cmd->user_interface == NULL || !cmd->user_interface->is_available()) {
        return false;
    }
	if(cmd->user_interface->popup("About to remove a changed disk. Save?", BUTTON_YES|BUTTON_NO) == BUTTON_NO) {
	    return false;
    }
	return true;
}

bool C1541 :: save_if_needed(SubsysCommand *cmd)
{
    if (check_if_save_needed(cmd)) {
        cmd->mode = (disk_state == e_gcr_disk) ? 1 : 0;
        if (are_mfm_dirty_bits_set()) {
            cmd->mode = 1; // force GCR save when MFM tracks have been written
        }
        return save_disk_to_file(cmd);
    }
    return true; // not needed, so success
}

void C1541 :: remove_disk(void)
{
    if (registers[C1541_INSERTED]) {
        registers[C1541_SOUNDS] = 2; // play remove sound
    }
    if(mount_file) {
        fm->fclose(mount_file);
    }
    mount_file = NULL;
    clear_mfm_dirty_bits();
    mfm_controller->set_file(NULL);
    mfm_controller->set_track_update_callback(NULL, NULL);

    registers[C1541_INSERTED] = 0;
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(100 * cfg->get_value(CFG_C1541_SWAPDELAY));

    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];
    uint32_t rotation_speed = (CLOCK_FREQ / 20); // 2 (half clocks) * 1/8 (bytes) * clocks per track. 300 RPM = 5 RPS. (5 * 8 / 2) = 20
    uint32_t bit_time = rotation_speed / GCRIMAGE_DUMMYTRACKLEN;

    for(int i=0;i < 256;i++) { // clear entire track parameter ram with safe values
        *(param++) = (uint32_t)dummy_track;
        *(param++) = (bit_time << 16) | 0x100;
    }

    registers[C1541_SENSOR] = SENSOR_LIGHT;
    registers[C1541_DISKCHANGE] = 1;
    disk_state = e_no_disk;
    printf("Disk removed. Parameters cleared.\n");
}

void C1541 :: insert_disk(bool protect, GcrImage *image)
{
    registers[C1541_SENSOR] = SENSOR_DARK;
    wait_ms(150);

    uint32_t rotation_speed = (CLOCK_FREQ / 20); // 2 (half clocks) * 1/8 (bytes) * clocks per track. 300 RPM = 5 RPS. (5 * 8 / 2) = 20

    // side 0
    volatile uint32_t *param = (volatile uint32_t *)&registers[C1541_PARAM_RAM];
    for(int i=0; i < GCRIMAGE_FIRSTTRACKSIDE1; i++) {
        GcrTrack *tr = &image->tracks[i];
        uint32_t bit_time = rotation_speed / tr->track_length;
        if (tr->track_address) {
            printf("Side0: %2d %08x %08x %d\n", i, tr->track_address, tr->track_length, bit_time);
            *(param++) = (uint32_t)tr->track_address;
            *(param++) = (tr->track_length-1) | (bit_time << 16);
        } else {
            param += 2;
        }
        registers[C1541_DIRTYFLAGS + i/2] = 0;
    }            

    // Side 1
    param = (volatile uint32_t *)&registers[C1541_PARAM_RAM + 0x400]; // side 1
    for(int i=GCRIMAGE_FIRSTTRACKSIDE1; i < GCRIMAGE_MAXHDRTRACKS; i++) {
        GcrTrack *tr = &image->tracks[i];
        uint32_t bit_time = rotation_speed / tr->track_length;
        if (tr->track_address) {
            printf("Side1: %2d %08x %08x %d\n", i, tr->track_address, tr->track_length, bit_time);
            *(param++) = (uint32_t)tr->track_address;
            *(param++) = (tr->track_length-1) | (bit_time << 16);
        } else {
            param += 2;
        }
        registers[C1541_DIRTYFLAGS + i/2] = 0;
    }

    registers[C1541_DIRTYFLAGS] = 0x80;

    if(!protect) {
        registers[C1541_SENSOR] = SENSOR_LIGHT;
    }

    map_gcr_image_to_mfm();

    registers[C1541_INSERTED] = 1;
    registers[C1541_DISKCHANGE] = 1;
    registers[C1541_SOUNDS] = 1; // play insert sound
    disk_state = e_alien_image;
    last_mounted_drive = this;
}

void C1541 :: map_gcr_image_to_mfm(void)
{
    MfmDisk *mfm = mfm_controller->get_disk();
    mfm->init(fmt_Clear);
    clear_mfm_dirty_bits();

    for(int z=0; z < 2; z++) {
        for(int i=0; i < GCRIMAGE_FIRSTTRACKSIDE1; i++) {
            GcrTrack *gtr = &gcr_image->tracks[i + z*GCRIMAGE_FIRSTTRACKSIDE1];
            MfmTrack *mtr = mfm->GetTrack(i, z);
            uint8_t *gcr = gtr->track_address;
            if (!gcr || !mtr) {
                continue;
            }
            mtr->reservedSpace = gtr->track_length - MFM_TRACK_HEADER_SIZE;
            mtr->offsetInFile = (int)gcr - (int)gcr_image->gcr_data;
            mtr->offsetInFile += MFM_TRACK_HEADER_SIZE;

            if (gtr->track_is_mfm) {
                // explicit copy of parameters
                mtr->numSectors = *gcr;
                gcr += 2;
                if (mtr->numSectors > WD_MAX_SECTORS_PER_TRACK) {
                    mtr->numSectors = WD_MAX_SECTORS_PER_TRACK;
                }
                int mfmSize = 0;
                int secSize;
                for (int s=0; s < mtr->numSectors; s++) {
                    mtr->sectors[s].track = *(gcr++);
                    mtr->sectors[s].side = *(gcr++);
                    mtr->sectors[s].sector = *(gcr++);
                    mtr->sectors[s].sector_size = *(gcr++);
                    gcr++; // skip error byte
                    if (mtr->sectors[s].sector_size > 3) { // nonsense; abort
                        mtr->numSectors = s;
                        break;
                    }
                    mfmSize += (1 << (7 + mtr->sectors[s].sector_size));
                    // skip error byte?
                }
                mtr->actualDataSize = mfmSize;
            } else { // was not an mfm track, but might become one later
                // Number of sectors and actualData size remain zero at this point.
            }
        }
    }

    // Now link the RamFile to map to the GCR data block, so that the MFM can read / write the buffer
    mfm_controller->set_file(gcr_ram_file);

    // And also link the updater function for the track header information
    mfm_controller->set_track_update_callback(this, mfm_update_callback);
}

void C1541 :: mount_d64(bool protect, File *file, int mode)
{
    registers[C1541_RESET] = 0; // do not reset, but also don't freeze
	remove_disk();
	mount_file = file;

	if (!file) {
	    return;
	}
	if (mode == 1581) {
	    mfm_controller->format_d81();
	    mfm_controller->set_file(mount_file);
	    mfm_controller->set_track_update_callback(NULL, NULL);
	    registers[C1541_SOUNDS] = 1; // play insert sound
	    registers[C1541_SENSOR] = (protect) ? SENSOR_DARK : SENSOR_LIGHT;
	    registers[C1541_INSERTED] = 1;
	    registers[C1541_DISKCHANGE] = 1;
	    disk_state = e_d81_disk;
	} else {
        printf("Loading...");
        bin_image->load(file);
        printf("Converting...");
        gcr_image->convert_disk_bin2gcr(bin_image, NULL);
        printf("Inserting...");
        insert_disk(protect, gcr_image);
        printf("Done\n");
        disk_state = e_d64_disk;
	}
    drive_reset(0); // do not reset, but restore the freeze
}

void C1541 :: mount_g64(bool protect, File *file)
{
    registers[C1541_RESET] = 0; // do not reset, but also don't freeze
    remove_disk();
	mount_file = file;

    if (!file) {
        return;
    }
	printf("Loading...");
	gcr_image->load(file);
	printf("Inserting...");
	insert_disk(protect, gcr_image);
	printf("Done\n");
	disk_state = e_gcr_disk;
	gcr_image_up_to_date = true;
	drive_reset(0); // do not reset, but restore the freeze
}

void C1541 :: mount_blank()
{
    registers[C1541_RESET] = 0; // do not reset, but also don't freeze
	remove_disk();
    wait_ms(250);
	gcr_image->blank();
	printf("Inserting blank disk...");
	insert_disk(false, gcr_image);
	printf("Done\n");
	disk_state = e_disk_file_closed;
    drive_reset(0); // do not reset, but restore the freeze
}

/*
void C1541 :: swap_disk()
{
    if (!mount_file) return;

    File *f;        
    char* path = strdup(mount_file->get_path());  // working on a copy of the current path
    char* type = path+strlen(path)-3;             // pointer to extension (last three chars)

    // Try to find the denominating character by searching backwards,
    // beginning with the last character before the extension dot, up
    // to the path separator. Determine the the position of the first
    // char that is either alphabetic or numeric and is preceeded and
    // followed by a non-alphabetic or non-numeric character
    // respectively.

    int index;
    bool found = false;
    
    for(index = strlen(path)-5; path[index] != '/'; index--) {
        
        if((found = (isalpha(path[index]) && !isalpha(path[index-1]) && !isalpha(path[index+1]))
            || (isdigit(path[index]) && !isdigit(path[index-1]) && !isdigit(path[index+1])))) {
               break;
           }
    }

    if(!found) {
        printf("Disk Swap: No denominating character found for %s\n", path);
        free(path);
        return;
    }
    
    char current = path[index];      // remember current value of denominator
    
    for (int i=0; path[i]; i++) {    // operate on upper case path from now on
        path[i] = toupper(path[i]);
    }
    current = toupper(current);

    char top = isalpha(current) ? 'A'-1 : '0'-1;  
    char bottom = isalpha(current) ? 'Z' : '9';

    SubsysCommand *cmd;
    int cmd_type = 0;
    FileInfo info(32);
    
    for (path[index]++; path[index] != current; path[index]++) {

        if (path[index] > bottom) { // continue from the top
            path[index] = top;
            continue;
        }

        if (fm->fstat(path, info) == FR_OK) { 

            printf("Disk Swap: %s -> %s\n", mount_file->get_path(), path);
            
            if (strncmp(type, "D64", 3) == 0) {
                cmd_type = D64FILE_MOUNT;
            }
            else if(strncmp(type, "G64", 3) == 0) {
                cmd_type = G64FILE_MOUNT;
            }
            else {
                printf("Disk Swap: Wrong type: %s\n", path);
                free(path);
                return;
            }
            
            // Write back dirty tracks before mounting new disk...
            while(((registers[C1541_DIRTYFLAGS] & 0x80)||(write_skip))) {
                printf(".");
                this->poll();
                vTaskDelay(50);
            }

            cmd = new SubsysCommand(cmd->user_interface, getID(),
                                    cmd_type, 0, 0, path);
            
            executeCommand(cmd);
            free(cmd);

            free(path);
            return;
        }
        else {
            // no immediate successor was found -> continue from the top
            if(path[index] > current) {
                path[index] = top;
            }
        }
    }
    printf("Disk Swap: No matching image found for %s\n", path);
    free(path);
}
*/

// static member
void C1541 :: run(void *a)
{
	C1541 *drv = (C1541 *)a;
	drv->task();
}

void C1541 :: task(void)
{
	t_wd177x_cmd mfmCommand;
	while(1) {
	    if ((registers[C1541_POWER] & 1) == 0) {
	        vTaskDelay(300);
	        continue;
	    }
	    BaseType_t hasCommand = mfm_controller->check_queue(mfmCommand);
	    if(lock(drive_name.c_str())) {
	        if (hasCommand == pdTRUE) {
	            mfm_controller->handle_wd177x_command(mfmCommand);
	        }
	        poll();
			unlock();
		}
	}
}

void C1541 :: poll() // called under mutex
{
	int tr;
	int written;
	int dummy;
	int drive_track;
	int secs;

	if (mount_file && !mount_file->isValid()) {
        printf("C1541: File was invalidated..\n");
        disk_state = e_disk_file_closed;
        fm->fclose(mount_file);
        mount_file = NULL;
    }

    drive_track = registers[C1541_TRACK] >> 1;
    if (registers[C1541_SIDE]) {
        drive_track += 64;
    }
    //printf("%c:%d\n", drive_letter, drive_track);

    // Check Dirty flags from GCR write engine
	if((registers[C1541_DIRTYFLAGS] & 0x80)||(write_skip)) {
		if(!write_skip) {
			registers[C1541_DIRTYFLAGS] = 0x80;  // clear flag once we didn't skip anything anymore
		}
		write_skip = 0;
		written = 0;

		for(int i=0; i < GCRIMAGE_MAXHDRTRACKS; i++) {
		    tr = (i >= GCRIMAGE_FIRSTTRACKSIDE1) ? i + 44 : i; // side 1 tracks 0x80 - ...
		    tr >>= 1;
		    if(registers[C1541_DIRTYFLAGS + tr] & 0x01) {
		        // in case a 1571 writes to side 1, the disk image is switched to double sided.
		        // This causes a blank disk, which can be single or double sided to be saved as .g71.
		        gcr_image->track_got_written_with_gcr(i);

		        if((!(registers[C1541_STATUS] & DRVSTAT_WRITEBUSY)) || (drive_track != tr)) {
					registers[C1541_DIRTYFLAGS + tr] = 0;
					switch(disk_state) {
					case e_gcr_disk:
						printf("Writing back GCR track %d.0...\n", tr+1);
						gcr_image_up_to_date &= gcr_image->write_track(i, mount_file, cfg->get_value(CFG_C1541_GCRALIGN));
						break;
					case e_d64_disk:
						printf("Writing back binary track %d...\n", tr+1);
						bin_image->write_track(i, gcr_image, mount_file, dummy, secs);
						break;
                    case e_disk_file_closed:
                        // printf("Track %d cant be written back to closed file. Lost..\n", tr+1);
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


    // Check dirty tracks from MFM engine. Note that for D81, there is no call back, sectors are written directly into .d81 file
	if (disk_state == e_gcr_disk) { // MFM tracks can only be written into GCR disks
        for(int side=0; side<2; side++) {
            uint32_t temp;
            for(int tr=0; tr < WD_MAX_TRACKS_PER_SIDE; tr++) {
                if ((tr & 31) == 0) {
                    temp = mfm_dirty_bits[side][tr >> 5];
                }
                if (temp & 1) {
                    if((!(registers[C1541_STATUS] & DRVSTAT_WRITEBUSY)) || (registers[C1541_TRACK] != tr)) {
                        printf("--> MFM Track Dirty bit set %d/%d\n", side, tr);
                        // Clear dirty bit
                        mfm_dirty_bits[side][tr >> 5] &= ~(1 << (tr & 31));

                        // Try updating the track inside of the mount file
                        // if mount_file is NULL, function will return false, gracefully
                        int ti = tr + (side ? GCRIMAGE_FIRSTTRACKSIDE1 : 0);
                        if (!gcr_image->write_track(ti, mount_file, false)) {
                            gcr_image_up_to_date = false;
                        }
                    }
                }
                temp >>= 1;
            }
        }
	}
}

void C1541 :: mfm_update_callback(void *obj, int physTrack, int physSide, MfmTrack *tr)
{
    printf("MFM Track update %d/%d '%s'.\n", physTrack, physSide, tr ? "Format" : "Sector Write");
    if (obj) {
        C1541 *drv = (C1541 *)obj;

        // Set MFM dirty bit
        drv->mfm_dirty_bits[physSide][physTrack >> 5] |= (1 << (physTrack & 31));
        drv->registers[C1541_MAN_WRITE] = 1; // cause 2 sec timer to start

        int ti = physTrack + (physSide ? GCRIMAGE_FIRSTTRACKSIDE1 : 0);
        GcrTrack *gtr = &(drv->gcr_image->tracks[ti]);
        if (!gtr->track_address) {
            return; // doesn't exist in image! (TODO: Log error?!)
        }

        if (!tr) {
            return; // Sector write only. Exit
        }

        gtr->track_used = true;
        gtr->track_is_mfm = true;

        if (physSide) { // was side 1 updated, then set image to double sided
            drv->gcr_image->set_ds(true);
        }
        uint8_t *gd = gtr->track_address;

        if (gd && (gtr->track_length >= MFM_TRACK_HEADER_SIZE)) {
            *(gd++) = (uint8_t)tr->numSectors;
            *(gd++) = 0; // version byte
/* It seems that in the g64conv.pl script, only one byte is used. Making it two bytes, and adding the error byte makes the header 150+6 = 156 (divisable by 4) */
            memcpy(gd, tr->sectors, MFM_TRACK_HEADER_SIZE - 2);
        }
    }
}

void C1541 :: clear_mfm_dirty_bits()
{
    for(int s=0;s<2;s++) {
        for(int w=0;w<3;w++) {
            mfm_dirty_bits[s][w] = 0;
        }
    }
}

bool C1541 :: are_mfm_dirty_bits_set()
{
    uint32_t orred = 0;
    for(int s=0;s<2;s++) {
        for(int w=0;w<3;w++) {
            orred |= mfm_dirty_bits[s][w];
        }
    }
    return (orred != 0);
}

FRESULT C1541 :: set_drive_type(t_drive_type drv)
{
    // Force 1541 mode, when the target is not multi-mode
    if (!multi_mode && (drv != e_dt_1541)) {
        return FR_INVALID_PARAMETER;
    }

    if (current_drive_type == drv) {
        return FR_OK; // do nothing, so success
    }

    registers[C1541_DRIVETYPE] = (uint8_t)drv;

    // Load the correct sound bank...
    const char *sounds =  (drv == e_dt_1571) ? "snds1571.bin" : (drv == e_dt_1581) ? "snds1581.bin" : "snds1541.bin";
    if (fm->load_file(ROMS_DIRECTORY, sounds, audio_address, 0xC000, NULL) != FR_OK) {
        // Silence upon failure to load
        memset(audio_address, 0, 0xC000);
    }

    // Load the correct ROM...
    uint8_t cfg_id_file = (drv == e_dt_1571) ? CFG_C1541_ROMFILE1 : (drv == e_dt_1581) ? CFG_C1541_ROMFILE2 : CFG_C1541_ROMFILE0;
    uint32_t transferred = 0;
    FRESULT res = fm->load_file(ROMS_DIRECTORY, cfg->get_string(cfg_id_file), (uint8_t *)&memory_map[0x8000], 0x8000, &transferred);
    if (transferred == 0x4000) {
        memcpy((void *)(memory_map + 0xC000), (const void *)(memory_map + 0x8000), 0x4000); // copy 16K if the file was 16K
        transferred <<= 1;
    }

    drive_reset(1);
    current_drive_type = drv;
    if (res == FR_OK) {
        if (transferred != 0x8000) {
            res = FR_INVALID_PARAMETER;
        }
    }
    return res;
}

FRESULT C1541 :: change_drive_type(t_drive_type drv,  UserInterface *ui)
{
    if (!multi_mode) {
        return FR_DENIED;
    }
    bool change = true;
    if (ui) {
        char buf[40];
        sprintf(buf, "Change drive type to %s?", drive_types[(int)drv]);
        int res = ui->popup(buf, BUTTON_CANCEL | BUTTON_OK);
        if (res != BUTTON_OK) {
            change = false;
        }
    }
    FRESULT res = FR_DENIED;
    if (change) {
        res = set_drive_type(drv);
    }
    return res;
}

int C1541 :: executeCommand(SubsysCommand *cmd)
{
	bool g64;
	bool protect;
	uint8_t flags = 0;
	File *newFile = 0;
	FRESULT res;
	FileInfo info(32);

    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), info);

	flags = FA_READ | FA_WRITE;
	protect = (cmd->functionID & MENU_1541_READ_ONLY);
	if (info.attrib & AM_RDO) {
	    protect = true;
	}
	if (protect || (cmd->functionID & MENU_1541_UNLINKED)) {
	    flags = FA_READ;
	}

	int cmdCode = cmd->functionID & MENU_1541_NO_FLAGS;



	SubsysCommand *c64_command;
	char drv[2] = { 0,0 };

	switch(cmdCode) {
	case MENU_1541_MOUNT_D64:
    case MENU_1541_MOUNT_G64:
        if (!save_if_needed(cmd)) {
            if(cmd->user_interface) {
                if (cmd->user_interface->popup("Disk could not be saved. Continue?", BUTTON_YES|BUTTON_NO) == BUTTON_NO) {
                    break;
                }
            } // else: No user interface: we just do it! Oops?
        }
	    res = FR_OK;
	    switch(cmd->mode) {
	        case 1541:
	            if ((current_drive_type != e_dt_1541) && (current_drive_type != e_dt_1571)) {
	                res = change_drive_type(e_dt_1541, cmd->user_interface);
	            }
	            break;
	        case 1571:
	            if (current_drive_type != e_dt_1571) {
	                res = change_drive_type(e_dt_1571, cmd->user_interface);
	            }
	            break;
	        case 1581:
	            if (current_drive_type != e_dt_1581) {
	                res = change_drive_type(e_dt_1581, cmd->user_interface);
	            }
	            break;
	        default:
	            printf("-> INTERNAL ERROR: Mount mode should be 1541, 1571 or 1581.\n");
	            res = FR_DENIED;
	    }
	    if (res == FR_OK) {
            res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), flags, &newFile);
            if (res == FR_OK) {
                if(cmdCode == MENU_1541_MOUNT_G64) {
                    mount_g64(protect, newFile);
                } else {
                    mount_d64(protect, newFile, cmd->mode);
                }
            } else {
                if (cmd->user_interface) {
                    cmd->user_interface->popup("Opening disk file failed.", BUTTON_OK);
                }
            }
	    }
		break;

	case MENU_1541_UNLINK:
		unlink();
		break;
	case MENU_1541_RESET:
	    set_hw_address(cfg->get_value(CFG_C1541_BUS_ID));
	    drive_power(cfg->get_value(CFG_C1541_POWERED) != 0);
		break;
	case MENU_1541_TURNON:
	    drive_power(1);
	    break;
	case MENU_1541_TURNOFF:
        drive_power(0);
	    break;
	case MENU_1541_SET_MODE:
	    set_drive_type((t_drive_type) cmd->mode);
	    break;
	case MENU_1541_REMOVE:
        if (!save_if_needed(cmd)) {
            if(cmd->user_interface) {
                if (cmd->user_interface->popup("Disk could not be saved. Continue?", BUTTON_YES|BUTTON_NO) == BUTTON_NO) {
                    break;
                }
            } // else: No user interface: we just do it! Oops?
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
    case FLOPPY_LOAD_DOS:
    	memcpy((void *)&memory_map[0x8000], (void*) cmd->buffer, 0x8000);
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
	mfm_controller->set_file(NULL);
}

bool C1541 :: save_disk_to_file(SubsysCommand *cmd)
{
    static char buffer[32] = {0};
    char errstr[40];
    File *file = 0;
	FRESULT fres;
	int res;
	bool success = true;
	const char *ext;

	// Note that when we get here, the GCR disk is leading. The BIN image is only used to store
	// the binary data that is converted FROM the GCR image. For this reason, whether the disk
	// is double sided or not, ONLY depends on the GCR image.
	if (cmd->mode) {
	    ext = gcr_image->is_double_sided() ? ".g71" : ".g64";
	} else {
	    ext = gcr_image->is_double_sided() ? ".d71" : ".d64";
	}

    set_extension(buffer, "", 32); // initially remove the extension
	res = cmd->user_interface->string_box("Give name for image file..", buffer, 24);
	if(res > 0) {
		set_extension(buffer, ext, 32);
		fix_filename(buffer);
		fres = fm->fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW, &file);
		if(fres == FR_OK) {
			if(cmd->mode) {
				cmd->user_interface->show_progress("Saving GCR...", gcr_image->double_sided ? 164 : 84);
				success = gcr_image->save(file, (cfg->get_value(CFG_C1541_GCRALIGN)!=0), cmd->user_interface);
				cmd->user_interface->hide_progress();
			} else {
				cmd->user_interface->show_progress("Converting disk...", 2*bin_image->num_tracks);
				int errors = gcr_image->convert_disk_gcr2bin(bin_image, cmd->user_interface);
				if (errors) {
	                cmd->user_interface->hide_progress();
				    sprintf(errstr, "Found %d errors while decoding.", errors);
				    cmd->user_interface->popup(errstr, BUTTON_OK);
	                cmd->user_interface->show_progress("Saving...", 100);
				} else {
				    cmd->user_interface->update_progress("Saving...", 0);
				}
				success = bin_image->save(file, cmd->user_interface);
				cmd->user_interface->hide_progress();
			}
			fm->fclose(file);
			return success;
		} else {
			cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
		}
	}
    return false;
}

void C1541 :: set_rom_config(int idx, const char *fname)
{
    uint8_t config_ids[] = { CFG_C1541_ROMFILE0, CFG_C1541_ROMFILE1, CFG_C1541_ROMFILE2 };
    cfg->set_string(config_ids[idx], fname);
    cfg->write();
    effectuate_settings();
}

void C1541 :: list_roms(ConfigItem *it, IndexedList<char *>& strings)
{
    Path p;
    p.cd(ROMS_DIRECTORY);
    IndexedList<FileInfo *>infos(16, NULL);
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->get_directory(&p, infos, NULL);
    if (fres != FR_OK) {
        return;
    }

    for(int i=0;i<infos.get_elements();i++) {
        FileInfo *inf = infos[i];
        if ((inf->size == 16384) || (inf->size == 32768)) {
            char *s = new char[1+strlen(inf->lfname)];
            strcpy(s, inf->lfname);
            strings.append(s);
        }
        delete inf;
    }
}

