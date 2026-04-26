#include <stdlib.h>
#include <string.h>

#include "itu.h"
#include "dump_hex.h"
#include "c1581.h"
#include "c64.h"
#include "userinterface.h"
#include "filemanager.h"

static const char *yes_no[]  = { "No", "Yes" };

#define CFG_C1581_POWERED   0xC1
#define CFG_C1581_BUS_ID    0xC2
#define CFG_C1581_SWAPDELAY 0xC3
#define CFG_C1581_LASTMOUNT 0xC4
#define CFG_C1581_C64RESET  0xC5
#define CFG_C1581_STOPFREEZ 0xC6
#define CFG_C1581_ROMFILE   0xC7

const struct t_cfg_definition c1581_config[] = {
    { CFG_C1581_POWERED,   CFG_TYPE_ENUM,   "1581 Drive",                 "%s", en_dis,     0,  1, 1 },
    { CFG_C1581_BUS_ID,    CFG_TYPE_VALUE,  "1581 Drive Bus ID",          "%d", NULL,       8, 11, 8 },
    { CFG_C1581_SWAPDELAY, CFG_TYPE_VALUE,  "1581 Disk swap delay",       "%d00 ms", NULL,  1, 10, 1 },
    { CFG_C1581_C64RESET,  CFG_TYPE_ENUM,   "1581 Resets when C64 resets","%s", yes_no,     0,  1, 1 },
    { CFG_C1581_STOPFREEZ, CFG_TYPE_ENUM,   "1581 Freezes in menu",       "%s", yes_no,     0,  1, 1 },
    { CFG_C1581_ROMFILE,   CFG_TYPE_STRING, "1581 ROM File",              "%s", NULL,       1, 32, (int)"1581.rom" },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

uint16_t crc16_ccitt(uint16_t crc, const uint8_t *buf, int len);

//--------------------------------------------------------------
// C1541 Drive Class
//--------------------------------------------------------------

C1581 :: C1581(volatile uint8_t *regs, char letter) : SubSystem(SUBSYSID_DRIVE_C)
{
    registers  = regs;
    wd177x = (volatile wd177x_t *)(regs + 0x1800);

    mount_file = NULL;
    drive_letter = letter;
	flash = get_flash();
    fm = FileManager :: getFileManager();
    
    // Create local copy of configuration definition, since the default differs.
    int size = sizeof(c1581_config);
    int elements = size / sizeof(t_cfg_definition);
    local_config_definitions = new t_cfg_definition[elements];
    memcpy(local_config_definitions, c1581_config, size);

    char buffer[32];
    local_config_definitions[0].def = (letter == 'A')?1:0;   // drive A is default 8, drive B is default 9, etc
    local_config_definitions[1].def = 8 + int(letter - 'A'); // only drive A is by default ON.
        
	// Initialize drive variables
	step_dir = 0;
	track = 0;
	sector = 0;
	side = 0;

	drive_name = "Drive ";
	drive_name += letter;

	disk_state = e_no_disk;
	iec_address = 8 + int(letter - 'A');
	taskHandle = 0;

	uint32_t mem_address = ((uint32_t)registers[C1541_MEM_ADDR]) << 16;
    memory_map = (volatile uint8_t *)mem_address;
    printf("C1581 Memory address: %p\n", mem_address);

    if (mem_address) {
		sprintf(buffer, "1581 Drive %c Settings", letter);
		register_store((uint32_t)regs, buffer, local_config_definitions);
		sprintf(buffer, "1581 Drive %c", drive_letter);
		taskItemCategory = TasksCollection :: getCategory(buffer, SORT_ORDER_DRIVES + drive_letter - 'A');

		cmdQueue = xQueueCreate(8, sizeof(t_1581_cmd));
		// Do NOT enable the interrupt here, because the "this" pointer is not yet set.
    }
}

C1581 :: ~C1581()
{
    ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) & ~2);

    if(mount_file)
		fm->fclose(mount_file);

	// turn the drive off on destruction
    drive_power(false);

    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }
    vQueueDelete(cmdQueue);
}

void C1581 :: effectuate_settings(void)
{
	if(!lock(this->drive_name.c_str())) {
	    printf("** ERROR ** Effectuate drive settings could not be executed, as the drive was locked.\n");
        return;
	}

	printf("Effectuate 1581 settings:\n");

    set_hw_address(cfg->get_value(CFG_C1581_BUS_ID));
    set_sw_address(cfg->get_value(CFG_C1581_BUS_ID));

    uint32_t transferred = 0;
    fm->load_file("/flash", cfg->get_string(CFG_C1581_ROMFILE), (uint8_t *)&memory_map[0x8000], 0x8000, &transferred);

    drive_reset(0);

    if(registers[C1541_POWER] != cfg->get_value(CFG_C1581_POWERED)) {
        drive_power(cfg->get_value(CFG_C1581_POWERED) != 0);
    }
    if (transferred != 0x8000) {
        drive_power(false);
    }

    unlock();
}


void C1581 :: init(void)
{
    printf("Init C1581. Cfg = %p\n", cfg);
    
	registers[C1541_SENSOR] = SENSOR_LIGHT;
    registers[C1541_INSERTED] = 0;
    registers[C1541_DISKCHANGE] = 0;
    disk_state = e_no_disk;

	xTaskCreate( C1581 :: run, (const char *)(this->drive_name.c_str()), configMINIMAL_STACK_SIZE, this, PRIO_FLOPPY, &taskHandle );
    effectuate_settings();
    ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) | 2);
}


void C1581 :: create_task_items(void)
{
    myActions.reset   = new Action("Reset",        getID(), MENU_1541_RESET, 0);
    myActions.remove  = new Action("Remove Disk",  getID(), MENU_1541_REMOVE, 0);
    myActions.turnon  = new Action("Turn On",      getID(), MENU_1541_TURNON, 0);
    myActions.turnoff = new Action("Turn Off",     getID(), MENU_1541_TURNOFF, 0);

    taskItemCategory->append(myActions.turnon);
    taskItemCategory->append(myActions.turnoff);
    taskItemCategory->append(myActions.reset);
    taskItemCategory->append(myActions.remove);
}

void C1581 :: update_task_items(bool writablePath)
{
    myActions.remove->hide();
    myActions.reset->hide();
    myActions.turnon->hide();

    // don't show items for disabled drives
    if ((registers[C1541_POWER] & 1) == 0) {
        myActions.turnon->show();
        myActions.turnoff->hide();
        return;
    }

    myActions.turnoff->show();
    myActions.reset->show();

    if(disk_state != e_no_disk) {
        myActions.remove->show();
    }
}

void C1581 :: drive_power(bool on)
{
    registers[C1541_POWER] = on?1:0;
    if(on)
        drive_reset(1);
}

bool C1581 :: get_drive_power()
{
	return registers[C1541_POWER];
}

void C1581 :: drive_reset(uint8_t doit)
{
    registers[C1541_RESET] = doit;
    wait_ms(1);
    uint8_t reg = 0;
    
    if(cfg->get_value(CFG_C1581_STOPFREEZ))
        reg |= 4;
    if(cfg->get_value(CFG_C1581_C64RESET))
        reg |= 2;

    registers[C1541_RESET] = reg;
}

void C1581 :: set_hw_address(int addr)
{
    registers[C1541_HW_ADDR] = uint8_t(addr & 3);
    iec_address = 8 + (addr & 3);
}

void C1581 :: set_sw_address(int addr)
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

/*
    memory_map[0x78] = uint8_t((addr & 0x1F) + 0x40);
    memory_map[0x77] = uint8_t((addr & 0x1F) + 0x20);
*/
    iec_address = addr & 0x1F;
}

int  C1581 :: get_current_iec_address(void)
{
	return iec_address;
}

int  C1541 :: get_effective_iec_address(void)
{
	if(registers[C1541_POWER]) // if powered, read actual address from its ram
		return int(memory_map[0x78] & 0x1F);
	return iec_address;
}

void C1581 :: remove_disk(void)
{
    registers[C1541_INSERTED] = 0;
    registers[C1541_DISKCHANGE] = 1;
    vTaskDelay(20 * cfg->get_value(CFG_C1581_SWAPDELAY));

	if(mount_file) {
		fm->fclose(mount_file);
		mount_file = NULL;
	}

    registers[C1541_SENSOR] = SENSOR_LIGHT;
    disk_state = e_no_disk;
}

void C1581 :: mount_d81(bool protect, File *file)
{
	remove_disk();

	mount_file = file;
	if (!file) {
	    return;
	}

    registers[C1541_SENSOR] = SENSOR_DARK;
    vTaskDelay(20 * cfg->get_value(CFG_C1581_SWAPDELAY));
    registers[C1541_SENSOR] = protect ? SENSOR_DARK : SENSOR_LIGHT;
    registers[C1541_DISKCHANGE] = 1;
    registers[C1541_INSERTED] = 1;
	disk_state = e_d64_disk;
}

extern "C" {
	uint8_t c1581_irq(void)
	{
		return c1581_C->IrqHandler();
	}
}

uint8_t C1581 :: IrqHandler()
{
	t_1581_cmd cmd;
	cmd = wd177x->command;
    BaseType_t retVal = pdFALSE;

	xQueueSendFromISR(cmdQueue, &cmd, &retVal);
	wd177x->irq_ack = 1;
	return retVal;
}

// static member
void C1581 :: run(void *a)
{
	C1581 *drv = (C1581 *)a;
	drv->task();
}

void C1581 :: task()
{
	t_1581_cmd cmd;

	while(true) {
		if (xQueueReceive(cmdQueue, &cmd, 200) == pdTRUE) {
			handle_wd177x_command(cmd);
		}

		if (mount_file) {
			if (!mount_file->isValid()) {
				printf("C1581: File was invalidated..\n");
				remove_disk();
			}
		}
	}
}

static const uint8_t c_step_times[] = { 6, 12, 20, 30 }; // ms per step

void C1581 :: do_step(t_1581_cmd cmd)
{
    if (step_dir == 0) {
    	if (track != 0) {
    		track --;
    	}
    } else {
    	if (track != 0xFF) {
    		track ++;
    	}
    }

    wd177x->step_time = c_step_times[cmd & 3];
    wd177x->stepper_track = track;

    if (cmd & WD_CMD_UPDATE_BIT) {
        if (step_dir == 0) {
        	if (wd177x->track != 0) {
        		wd177x->track --;
        	}
        } else {
        	if (wd177x->track != 0xFF) {
        		wd177x->track ++;
        	}
        }
    }
    registers[C1541_DISKCHANGE] = 0;
}

#define printf(...)
#define dump_hex_relative(...)

void C1581 :: handle_wd177x_command(t_1581_cmd& cmd)
{
	printf("1581 Command: %b\n", cmd);
	int sectors_found;
	uint16_t crc;
	uint32_t offset, dummy;
	FRESULT res;

	switch (cmd >> 4) {
	case WD_CMD_RESTORE:
		// Restore (go to track 0)
		wd177x->track = 0;
        track = 0;
        wd177x->step_time = c_step_times[cmd & 3];
        wd177x->stepper_track = 0;
        step_dir = 1;
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) { // if h-bit is clear, do spinup
        	wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
        }
        //wd177x->status_set = WD_STATUS_TRACK_00; // track 0 reached
        registers[C1541_DISKCHANGE] = 0;
        // no data transfer
        wd177x->status_clear = WD_STATUS_BUSY;
        break;

	case WD_CMD_SEEK:
		// Seek; requested track is in data register
		track = wd177x->datareg;
		wd177x->track = track;
        wd177x->step_time = c_step_times[cmd & 3];
        wd177x->stepper_track = track;
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) { // if h-bit is clear, do spinup
        	wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
        }
        // no data transfer
        wd177x->status_clear = WD_STATUS_BUSY;
        break;

	case WD_CMD_STEP:
	case WD_CMD_STEP+1:
		// Step Command
		if ((cmd & WD_CMD_SPINUP_BIT) == 0) { // if h-bit is clear, do spinup
			wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
		}
		do_step(cmd);
		// no data transfer
		wd177x->status_clear = WD_STATUS_BUSY;
		break;

	case WD_CMD_STEP_IN:
	case WD_CMD_STEP_IN+1:
		// Step IN Command
		if ((cmd & WD_CMD_SPINUP_BIT) == 0) { // if h-bit is clear, do spinup
			wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
		}
		step_dir = 1;
		do_step(cmd);
		// no data transfer
		wd177x->status_clear = WD_STATUS_BUSY;
		break;

	case WD_CMD_STEP_OUT:
	case WD_CMD_STEP_OUT+1:
		// Step OUT Command
		if (cmd) { // if h-bit is set, do spinup
			wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
		}
		step_dir = 0;
		do_step(cmd & WD_CMD_UPDATE_BIT);
		// no data transfer
		wd177x->status_clear = WD_STATUS_BUSY;
		break;

	case WD_CMD_READ_SECTOR:
	case WD_CMD_READ_SECTOR+1:
		// Read Sector Command
		sector = wd177x->sector;
		side = 1 - ((registers[C1541_STATUS] >> 1) & 1);
		offset = ((track << 1) + side) * C1581_BYTES_PER_TRACK + (sector-1) * C1581_DEFAULT_SECTOR_SIZE;
		printf("C1581: Read Sector H/T/S: %d/%d/%d  (offset: %5x) Actual track: %d\n", side, wd177x->track, sector, offset, track);

		if (mount_file) {
			res = mount_file->seek(offset);
			if (res == FR_OK) {
				res = mount_file->read(buffer, C1581_DEFAULT_SECTOR_SIZE, &dummy);
				if (res == FR_OK) {
					printf("Sector read OK:\n");
					//dump_hex_relative(buffer, 32);
				} else {
					printf("-> Image read error.\n");
				}
			} else {
				printf("-> Seek error\n");
			}
		} else {
			printf("-> No mount file.\n");
		}

		wd177x->dma_len = C1581_DEFAULT_SECTOR_SIZE;
		wd177x->dma_addr = (uint32_t)buffer;
		wd177x->dma_mode = 1; // read
        // data transfer, so we are not yet done
		break;

	case WD_CMD_WRITE_SECTOR:
	case WD_CMD_WRITE_SECTOR+1:
		if (wd177x->dma_mode == 3) {
			printf("Write sector completion. dma_len = %d\n", wd177x->dma_len);
			//dump_hex_relative(buffer, 16);
			wd177x->dma_mode = 0;

			if (mount_file) {
				offset = ((track << 1) + side) * C1581_BYTES_PER_TRACK + (sector-1) * C1581_DEFAULT_SECTOR_SIZE;
				res = mount_file->seek(offset);
				if (res == FR_OK) {
					res = mount_file->write(buffer, C1581_DEFAULT_SECTOR_SIZE, &dummy);
					if (res == FR_OK) {
						printf("Sector write OK.\n");
					} else {
						printf("-> Image write error.\n");
					}
				} else {
					printf("-> Seek error\n");
				}
			} else {
				printf("-> No mount file.\n");
			}

			wd177x->status_clear = WD_STATUS_BUSY;
		} else {
			// Write Sector Command
			sector = wd177x->sector;
			side = 1 - ((registers[C1541_STATUS] >> 1) & 1);
			offset = ((track << 1) + side) * C1581_BYTES_PER_TRACK + (sector-1) * C1581_DEFAULT_SECTOR_SIZE;
			printf("C1581: Write Sector H/T/S: %d/%d/%d. Offset: %6x\n", side, track, sector, offset);
			memset(buffer, 0x77, C1581_DEFAULT_SECTOR_SIZE);
			wd177x->dma_len = C1581_DEFAULT_SECTOR_SIZE;
			wd177x->dma_addr = (uint32_t)buffer;
			wd177x->dma_mode = 2; // write
			// data transfer, so we are not yet done
		}
		break;

	case WD_CMD_READ_ADDR:
		// Read Address Command
		side = 1 - ((registers[C1541_STATUS] >> 1) & 1);

        sector ++;
        if (sector > C1581_DEFAULT_SECTORS_PER_TRACK)
            sector = 1;

		buffer[0] = track;
		buffer[1] = side;
		buffer[2] = sector;
		buffer[3] = 2; // sector length = 512
		crc = crc16_ccitt(0xb230, buffer, 4);
		buffer[4] = (crc >> 8);
		buffer[5] = crc & 0xFF;
		dump_hex_relative(buffer, 6);

		wd177x->dma_len = 6;
		wd177x->dma_addr = (uint32_t)buffer;
		wd177x->dma_mode = 1; // read
		break;

	case WD_CMD_READ_TRACK:
		// Read Track Command
		printf("C1581: Read Track command not implemented.");
        wd177x->status_clear = 1;
        break;

	case WD_CMD_WRITE_TRACK:
		if (wd177x->dma_mode == 3) {
			side = 1 - ((registers[C1541_STATUS] >> 1) & 1);
			offset = ((track << 1) + side) * C1581_BYTES_PER_TRACK;

			sectors_found = decode_write_track(buffer, binbuf, C1581_RAW_BYTES_BETWEEN_INDEX_PULSES);
			printf("Write track completion. Offset = %6x. Found %d sectors!\n", offset, sectors_found);
			//dump_hex_relative(buffer, 800);

			if (mount_file) {
				res = mount_file->seek(offset);
				if (res == FR_OK) {
					res = mount_file->write(binbuf, C1581_BYTES_PER_TRACK, &dummy);
					if (res == FR_OK) {
						printf("Track clear OK.\n");
					} else {
						printf("-> Image write error.\n");
					}
				} else {
					printf("-> Seek error\n");
				}
			} else {
				printf("-> No mount file.\n");
			}

			wd177x->dma_mode = 0;
			wd177x->status_clear = WD_STATUS_BUSY;
		} else {
			// Write Track Command
			printf("Write track %d %d\n", track, wd177x->track);
			wd177x->dma_len = C1581_RAW_BYTES_BETWEEN_INDEX_PULSES;
			wd177x->dma_addr = (uint32_t)buffer;
			wd177x->dma_mode = 2; // write
		}
		break;

	case WD_CMD_ABORT:
		wd177x->dma_mode = 0; // Stop DMA
        wd177x->status_clear = WD_STATUS_BUSY;
        break;
	}
}

int C1581 :: decode_write_track(uint8_t *inbuf, uint8_t *outbuf, int len)
{
	int sectors_found = 0;
	int rpos = 0;
	int wpos = 0;
	uint8_t sector = 1;
	uint8_t max_sector;
	int sec_size;
	memset(outbuf, 0x55, C1581_BYTES_PER_TRACK);
	while (rpos < len) {
		if(inbuf[rpos] == 0xFE) { // header
			if (inbuf[rpos+4] < 4) {
				sec_size = 1 << (7 + inbuf[rpos+4]);
				max_sector = 40 >> inbuf[rpos+4]; // 40, 20, 10, 5
			} else {
				printf("Illegal sector size.\n");
				return sectors_found;
			}
			printf("Sector header: Track %d/%d Sector: %d of %d bytes\n", inbuf[rpos+1], inbuf[rpos+2], inbuf[rpos+3], sec_size);
			if (inbuf[rpos+5] != 0xF7) {
				printf("Expected Header to end with CRC bytes!\n");
				return sectors_found;
			}
			sector = inbuf[rpos+3];
			rpos += 16;
		} else if (inbuf[rpos] == 0xFB) { // data
			if ((sector >= 1) && (sector <= max_sector)) {
				if (inbuf[rpos + 1 + sec_size] != 0xF7) {
					printf("Expected Data to end with CRC bytes!\n");
					return sectors_found;
				}
				memcpy(outbuf + sec_size * (sector-1), &inbuf[rpos+1], sec_size);
				sectors_found ++;
			} else {
				printf("Skipped sector #%d - outside of valid range.\n", sector);
			}
			rpos += sec_size;
			rpos += 16;
		} else {
			rpos ++;
		}
	}
	return sectors_found;
}


int C1581 :: executeCommand(SubsysCommand *cmd)
{
	File *newFile = 0;
	FRESULT res;
	FileInfo info(32);

	SubsysCommand *c64_command;
	char drv[2] = { 0,0 };

	switch(cmd->functionID) {
	case D64FILE_MOUNT:
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_OPEN_EXISTING | FA_READ | FA_WRITE, &newFile);
		mount_d81(false, newFile);
		break;
	case D64FILE_MOUNT_RO:
		res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_OPEN_EXISTING | FA_READ , &newFile);
		mount_d81(true, newFile);
		break;
	case MENU_1541_RESET:
	    set_hw_address(cfg->get_value(CFG_C1581_BUS_ID));
	    drive_power(cfg->get_value(CFG_C1581_POWERED) != 0);
		break;
	case MENU_1541_TURNON:
	    drive_power(1);
	    break;
	case MENU_1541_TURNOFF:
        drive_power(0);
	    break;
	case MENU_1541_REMOVE:
		remove_disk();
		break;
    	
	default:
		printf("Unhandled menu item for C1581.\n");
		return -1;
	}
	return 0;
}

static const uint16_t crc16tab[256]= {
	0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
	0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
	0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
	0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
	0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
	0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
	0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
	0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
	0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
	0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
	0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
	0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
	0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
	0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
	0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
	0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
	0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
	0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
	0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
	0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
	0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
	0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
	0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
	0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
	0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
	0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
	0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
	0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
	0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
	0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
	0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
	0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

uint16_t crc16_ccitt(uint16_t crc, const uint8_t *buf, int len)
{
	for(int counter = 0; counter < len; counter++) {
		crc = (crc<<8) ^ crc16tab[(crc>>8) ^ *(buf++)];
	}
	return crc;
}

