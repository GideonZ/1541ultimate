#include <stdlib.h>
#include <string.h>

#include "itu.h"
#include "dump_hex.h"
#include "wd177x.h"
#include "filemanager.h"

uint16_t crc16_ccitt(uint16_t crc, const uint8_t *buf, int len);


WD177x :: WD177x(volatile uint8_t *wd, volatile uint8_t *drv, int irq)
{
    wd177x = (volatile wd177x_t *)wd;
    drive = (volatile mmdrive_regs_t *)drv;
    irqNr = irq;
    mount_file = NULL;
    fm = FileManager :: getFileManager();
    track_updater = NULL;
    track_update_object = NULL;
    
	// Initialize drive variables
	step_dir = 0;
	sectIndex = 0;
	sectSize = 128;
	taskHandle = 0;
	offset = 0;
    cmdQueue = xQueueCreate(8, sizeof(t_wd177x_cmd));
}

WD177x :: ~WD177x()
{
    deinstall_high_irq(irqNr);

    if(mount_file)
		fm->fclose(mount_file);

    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }
    vQueueDelete(cmdQueue);
}

static uint8_t my_irq(void *context)
{
    return ((WD177x *)context)->IrqHandler();
}


void WD177x :: init(void)
{
    printf("Init WD177x...\n");
    wd177x->command = 3; // enable index pulse; inverted
    
	// xTaskCreate( WD177x :: run, "WD177x", configMINIMAL_STACK_SIZE, this, PRIO_FLOPPY, &taskHandle );
    // Runs from the drive task now

	while(wd177x->irq_ack & 0x80) {
		printf("Hey, fifo valid??\n");
		wd177x->irq_ack = 1;
	}

	install_high_irq(irqNr, my_irq, this);
}

void WD177x :: wait_head_settle(void)
{
    for (int i=0; i < 500; i++) {
        if (!(wd177x->stepper_track))
            break;
        vTaskDelay(1);
    }
}
/*
void WD177x :: remove_disk(void)
{
    drive[C1541_INSERTED] = 0;
    drive[C1541_DISKCHANGE] = 1;
    vTaskDelay(20 * cfg->get_value(CFG_C1581_SWAPDELAY));

	if(mount_file) {
		fm->fclose(mount_file);
		mount_file = NULL;
	}

    drive[C1541_SENSOR] = SENSOR_LIGHT;
}

void WD177x :: mount_d81(bool protect, File *file)
{
	remove_disk();

	mount_file = file;
	if (!file) {
	    return;
	}

    drive[C1541_SENSOR] = SENSOR_DARK;
    vTaskDelay(20 * cfg->get_value(CFG_C1581_SWAPDELAY));
    drive[C1541_SENSOR] = protect ? SENSOR_DARK : SENSOR_LIGHT;
    drive[C1541_DISKCHANGE] = 1;
    drive[C1541_INSERTED] = 1;
}
*/

uint8_t WD177x :: IrqHandler()
{
	t_wd177x_cmd cmd;
	cmd = wd177x->command;
	// upon read of the irq_ack register (offset 6), we get the 9th bit of the command, which indicates whether a dma write transfer was completed
	cmd |= ((uint16_t)wd177x->irq_ack) << 8;

	BaseType_t retVal = pdFALSE;

	xQueueSendFromISR(cmdQueue, &cmd, &retVal);
	wd177x->irq_ack = 1;
	return retVal;
}

// static member
void WD177x :: run(void *a)
{
	WD177x *drv = (WD177x *)a;
	drv->task();
}

BaseType_t WD177x :: check_queue(t_wd177x_cmd& cmd)
{
    return xQueueReceive(cmdQueue, &cmd, 50);
}

void WD177x :: task()
{
	t_wd177x_cmd cmd;

	while(true) {
		if (xQueueReceive(cmdQueue, &cmd, 200) == pdTRUE) {
			handle_wd177x_command(cmd);
		}

		if (mount_file) {
			if (!mount_file->isValid()) {
				printf("WD177x: File was invalidated..\n");
				fm->fclose(mount_file);
				mount_file = NULL;
				drive->inserted = 0;
				drive->diskchange = 1;
				//remove_disk();
			}
		}
	}
}

static const uint8_t c_step_times[] = { 6, 12, 20, 30 }; // ms per step

void WD177x :: do_step(t_wd177x_cmd cmd)
{
    // start from current position
    uint8_t track = drive->track;

    if (step_dir == 0) {
    	if (track != 0) {
    		track --;
    	}
    } else {
    	if (track < 83) {
    		track ++;
    	}
    }

    wd177x->step_time = c_step_times[cmd & 3];
    // Set new position
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
    drive->diskchange = 0;
}

#define printf(...)
#define dump_hex_relative(...)

void WD177x :: handle_wd177x_command(t_wd177x_cmd& cmd)
{
	printf("WD177x Command: %3x\n", cmd);
	uint16_t crc;
	uint32_t dummy;
	FRESULT res;
	MfmSector sectAddr;
	uint8_t t;

	if(cmd & WD_CMD_DMA_DONE) {
	    handle_wd177x_completion(cmd);
	    return;
	}

    wd177x->status_clear = WD_STATUS_RNF;
    switch ((cmd >> 4) & 0x0F) {
	case WD_CMD_RESTORE:
		// Restore (go to track 0)
		wd177x->track = 0;
        wd177x->step_time = c_step_times[cmd & 3];
        wd177x->stepper_track = 0;
        step_dir = 1;
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) { // if h-bit is clear, do spinup
        	wd177x->status_set = WD_STATUS_SPINUP_OK; // report spinup complete
        }
        //wd177x->status_set = WD_STATUS_TRACK_00; // track 0 reached
        drive->diskchange = 0;
        // no data transfer
        wd177x->status_clear = WD_STATUS_BUSY;
        break;

	case WD_CMD_SEEK:
		// Seek; requested track is in data register
	    t = wd177x->datareg;
	    printf("Seek %d. Current track = %d\n", t, drive->track);
	    wd177x->track = t;
        wd177x->step_time = c_step_times[cmd & 3];
        wd177x->stepper_track = t;
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
	    wait_head_settle();
	    // Read Sector Command
        sectAddr.sector = wd177x->sector;
        sectAddr.track  = wd177x->track;
        sectAddr.side   = drive->side;

		sectIndex = disk.GetSector(drive->track, drive->side, sectAddr, offset, sectSize);
		if (sectIndex >= 0) { // OK!
		    printf("WD177x: Read Sector H/T/S: %d/%d/%d  (offset: %5x) Actual track: %d\n", drive->side, wd177x->track, wd177x->sector, offset, drive->track);

            if (mount_file) {
                res = mount_file->seek(offset);
                if (res == FR_OK) {
                    res = mount_file->read(buffer, sectSize, &dummy);
                    if (res == FR_OK) {
                        printf("Sector read OK:\n");
                        //dump_hex_relative(buffer, sectSize);
                    } else {
                        printf("-> Image read error.\n");
                    }
                } else {
                    printf("-> Seek error\n");
                }
            } else {
                printf("-> No mount file.\n");
            }
		} else {
		    printf("WD177x: Read Sector: Sector %d/%d/%d not found in MFM definition on track %d! Setting RNF\n", sectAddr.track, sectAddr.side, sectAddr.sector, drive->track);
		    //disk.DumpFormat(true);
		    wd177x->status_set = WD_STATUS_RNF;
		    wd177x->status_clear = WD_STATUS_BUSY;
		    break; // end!
		}

		wd177x->dma_len  = (uint16_t)sectSize;
		wd177x->dma_addr = (uint32_t)buffer;
		wd177x->dma_mode = 1; // read
        // data transfer, so we are not yet done
		break;

	case WD_CMD_WRITE_SECTOR:
	case WD_CMD_WRITE_SECTOR+1:
        wait_head_settle();
        // Write Sector Command
        sectAddr.sector = wd177x->sector;
        sectAddr.track  = wd177x->track;
        sectAddr.side   = drive->side;
        printf("H/T/S: %d %d %d\n", sectAddr.side, sectAddr.track, sectAddr.sector);
        sectIndex = disk.GetSector(drive->track, drive->side, sectAddr, offset, sectSize);
        if (sectIndex >= 0) { // OK!
            printf("WD177x: Write Sector H/T/S: %d/%d/%d. Offset: %6x\n", sectAddr.side, sectAddr.track, sectAddr.sector, offset);
            memset(buffer, 0x77, sectSize);
            wd177x->dma_len = sectSize;
            wd177x->dma_addr = (uint32_t)buffer;
            wd177x->dma_mode = 2; // write
        } else {
            printf("WD177x: Write Sector %d/%d/%d: Sector not found on track %d in MFM definition! Setting RNF.\n",
                    sectAddr.track, sectAddr.side, sectAddr.sector, drive->track);
            wd177x->status_set = WD_STATUS_RNF;
            wd177x->status_clear = WD_STATUS_BUSY;
        }
        // data transfer, so we are not yet done
		break;

	case WD_CMD_READ_ADDR:
	    wait_head_settle();
	    // Read Address Command
        sectIndex = disk.GetAddress(drive->track, drive->side, sectIndex, sectAddr);

        if (sectIndex < 0) {
            wd177x->status_set = WD_STATUS_RNF;
            wd177x->status_clear = WD_STATUS_BUSY;
            break;
        }

		buffer[0] = sectAddr.track;
		buffer[1] = sectAddr.side;
		buffer[2] = sectAddr.sector;
		buffer[3] = sectAddr.sector_size;
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
		printf("WD177x: Read Track command not implemented.");
        wd177x->status_clear = 1;
        break;

    case WD_CMD_WRITE_TRACK:
        // Write Track Command
        printf("Write track Pos: %d Reg: %d\n", drive->track, wd177x->track);
        wd177x->dma_len = MFM_RAW_BYTES_BETWEEN_INDEX_PULSES;
        wd177x->dma_addr = (uint32_t)buffer;
        wd177x->dma_mode = 2; // write
		break;

	case WD_CMD_ABORT:
		wd177x->dma_mode = 0; // Stop DMA
        wd177x->status_clear = WD_STATUS_BUSY;
        break;
	}
}

void WD177x :: handle_wd177x_completion(t_wd177x_cmd& cmd)
{
    int sectors_found;
    uint32_t dummy;
    FRESULT res;
    MfmTrack newTrack;

    switch ((cmd >> 4) & 0x0F) {
    case WD_CMD_WRITE_SECTOR:
    case WD_CMD_WRITE_SECTOR+1:
        printf("Write sector completion. dma_len = %d\n", wd177x->dma_len);
        //dump_hex_relative(buffer, sectSize);
        wd177x->dma_mode = 0;

        if (mount_file) {
            res = mount_file->seek(offset);
            if (res == FR_OK) {
                res = mount_file->write(buffer, sectSize, &dummy);
                if (res == FR_OK) {
                    printf("Sector write OK. %d/%d bytes written to offset %6x.\n", dummy, sectSize, offset);
                } else {
                    printf("-> Image write error.\n");
                }
            } else {
                printf("-> Seek error\n");
            }
        } else {
            printf("-> No mount file.\n");
        }
        // Complete by doing the administration on the drive side
        // Passing a NULL pointer as newTrack signals that only the sector data was updated.
        if(track_updater) {
            track_updater(track_update_object, drive->track, drive->side, NULL);
        }
        break;

    case WD_CMD_WRITE_TRACK:
        decode_write_track(buffer, binbuf, newTrack);
        // FIXME: Just continue when there was an error?

        wait_head_settle(); // late, but early enough
        disk.UpdateTrack(drive->track, drive->side, newTrack, offset);

        printf("Write track completion. Offset = %6x. Found %d sectors! (Size: %04x)\n", offset, newTrack.numSectors, newTrack.actualDataSize);

        if (mount_file) {
            res = mount_file->seek(offset);
            if (res == FR_OK) {
                res = mount_file->write(binbuf, newTrack.actualDataSize, &dummy);
                if (res == FR_OK) {
                    printf("Track format OK.\n");
                } else {
                    printf("-> Image write error.\n");
                }
            } else {
                printf("-> Seek error\n");
            }
        } else {
            printf("-> No mount file.\n");
        }
        // Complete by doing the administration on the drive side
        if(track_updater) {
            track_updater(track_update_object, drive->track, drive->side, &newTrack);
        }
        wd177x->dma_mode = 0;
        break;
    default:
        printf("Unrecognized completion..\n");
        wd177x->status_clear = WD_STATUS_BUSY;
    }
}


void WD177x :: set_track_update_callback(void *obj, track_update_callback_t func)
{
    track_update_object = obj;
    track_updater = func;
}

#define ERR_WD_ILLEGAL_SECTOR_SIZE -100
#define ERR_WD_NO_CRC_AFTER_HEADER -101
#define ERR_WD_NO_CRC_AFTER_DATA   -102
#define ERR_WD_NO_SPACE_ON_TRACK   -103

int WD177x :: decode_write_track(uint8_t *inbuf, uint8_t *outbuf, MfmTrack& newTrack)
{
	int rpos = 0;
	int wpos = 0;
	int sec_size;
	memset(outbuf, 0x55, MFM_BYTES_PER_TRACK);
	memset(&newTrack, 0, sizeof(newTrack)); // zero out
	// newTrack.numSectors = 0; // not required when whole structure is cleared

	int len = MFM_RAW_BYTES_BETWEEN_INDEX_PULSES;
	while (rpos < len) {
		if(inbuf[rpos] == 0xFE) { // header
			if (inbuf[rpos+4] < 4) {
				sec_size = 1 << (7 + inbuf[rpos+4]);
			} else {
				printf("Illegal sector size.\n");
				return ERR_WD_ILLEGAL_SECTOR_SIZE;
			}
			printf("Sector header: Track %d/%d Sector: %d of %d bytes\n", inbuf[rpos+1], inbuf[rpos+2], inbuf[rpos+3], sec_size);
			if (inbuf[rpos+5] != 0xF7) {
				printf("Expected Header to end with CRC bytes!\n");
				return ERR_WD_NO_CRC_AFTER_HEADER;
			}
			// Store header
			newTrack.sectors[newTrack.numSectors].track  = inbuf[rpos+1];
            newTrack.sectors[newTrack.numSectors].side   = inbuf[rpos+2];
            newTrack.sectors[newTrack.numSectors].sector = inbuf[rpos+3];
            newTrack.sectors[newTrack.numSectors].sector_size = inbuf[rpos+4];
			rpos += 16;
		} else if (inbuf[rpos] == 0xFB) { // data
            if (inbuf[rpos + 1 + sec_size] != 0xF7) {
                printf("Expected Data to end with CRC bytes!\n");
                return ERR_WD_NO_CRC_AFTER_DATA;
            }
            if (wpos + sec_size > MFM_BYTES_PER_TRACK) {
                printf("Sector doesn't fit into buffer.\n");
                return ERR_WD_NO_SPACE_ON_TRACK;
            }
            memcpy(outbuf, &inbuf[rpos+1], sec_size);
            outbuf += sec_size;
            wpos += sec_size;
            newTrack.actualDataSize = wpos;
            newTrack.numSectors++;
			rpos += sec_size;
			rpos += 16;
		} else {
			rpos ++;
		}
	}
	return 0; // OK
}

void WD177x :: format_d81(int tracks)
{
    disk.init(fmt_D81, tracks);
}

void WD177x :: set_file(File *f)
{
    mount_file = f;
}

MfmDisk *WD177x :: get_disk(void)
{
    return &disk;
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

