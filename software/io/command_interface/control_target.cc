#include "control_target.h"
#include "disk_image.h"
#include <string.h>
#include "c64.h"
#include "tape_recorder.h"
#include "reu_preloader.h"
#if U64
#include "u64_config.h"
#include "u64.h"
#else
#include "audio_select.h"
#endif

extern REUPreloader *reu_preloader;

__inline uint32_t cpu_to_32le(uint32_t a)
{
#ifdef NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}

__inline uint16_t cpu_to_16le(uint16_t a)
{
#ifdef NIOS
	return a;
#else
	return (a >> 8) | (a << 8);
#endif
}

// crate and register ourselves!
ControlTarget ct1(4);

static Message c_message_identification     = { 19, true, (uint8_t *)"CONTROL TARGET V1.1" };
static Message c_status_invalid_params      = { 17, true, (uint8_t *)"81,INVALID PARAMS" };
static Message c_status_errors_on_track     = { 18, true, (uint8_t *)"82,ERRORS ON TRACK" };
static Message c_status_no_data             = { 19, true, (uint8_t *)"83,NOT IN DATA MODE" };
static Message c_status_reu_disabled        = { 18, true, (uint8_t *)"84,REU NOT ENABLED" };
static Message c_status_cannot_open_file    = { 28, true, (uint8_t *)"85,REU FILE CANNOT BE OPENED" };
static Message c_status_reu_not_saved       = { 31, true, (uint8_t *)"86,REU OFFSET > SIZE. NOT SAVED" };

ControlTarget :: ControlTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
}
   

ControlTarget :: ~ControlTarget()
{
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    
void ControlTarget :: decode_track(Message *command, Message **reply, Message **status)
{
	uint8_t *gcr, *bin;

	if (command->length != 14) {
		*reply = &c_message_empty;
		*status = &c_status_invalid_params;
		return;
	}

	int track = command->message[2];
	int maxSector = command->message[3];

	uint32_t gcrAddress = 0x01000000;
	uint32_t binAddress = 0x01000000;

	gcrAddress |= (uint32_t)command->message[4];
	gcrAddress |= ((uint32_t)command->message[5]) << 8;
	gcrAddress |= ((uint32_t)command->message[6]) << 16;

	binAddress |= (uint32_t)command->message[8];
	binAddress |= ((uint32_t)command->message[9]) << 8;
	binAddress |= ((uint32_t)command->message[10]) << 16;

	uint16_t trackLen = (uint16_t)command->message[12];
	trackLen |= ((uint16_t)command->message[13]) << 8;

	int secs = GcrImage :: convert_gcr_track_to_bin((uint8_t *)gcrAddress, track, trackLen, maxSector,
			(uint8_t *)binAddress, data_message.message + 1);

	data_message.message[0] = (uint8_t)secs;
	data_message.length = 1 + 2*secs;
	data_message.last_part = true;

	*reply  = &data_message;
	*status = &c_status_ok;

	if (secs == maxSector) {
		uint8_t *code = data_message.message + 2;
		for(int i=0;i<secs;i++) {
			if (*code) {
				*status = &c_status_errors_on_track;
				break;
			}
			code += 2;
		}
	} else {
		*status = &c_status_errors_on_track;
	}
}

void ControlTarget :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the DOS; the third and forth byte are the length
    // data follows after the 4th byte and is thus aligned for copy.    
    int retVal;
    uint32_t *pul;
    SubsysCommand *cmd;
    *reply  = &c_message_empty;
    *status = &c_status_unknown_command;

    switch(command->message[1]) {
        case CTRL_CMD_IDENTIFY:
		    *reply  = &c_message_identification;
            *status = &c_status_ok;
            break;            
        case CTRL_CMD_DECODE_TRACK:
        	decode_track(command, reply, status);
        	break;
        case CTRL_CMD_FREEZE:
            cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_PUSH_BUTTON, (int)0, "", "");
            cmd->execute();
            *status = &c_message_empty;
            break;
        case CTRL_CMD_REBOOT:
            cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_REBOOT, (int)0, "", "");
            cmd->execute();
            *status = &c_message_empty;
            break;
        case CTRL_CMD_FINISH_CAPTURE:
            printf("Finish tape capture command!\n");
            cmd = new SubsysCommand(NULL, SUBSYSID_TAPE_RECORDER, MENU_REC_FINISH, 0, "", "");
            cmd->execute();
            *reply  = &c_message_empty;
            *status = &c_status_ok;
            break;
        case CTRL_CMD_ENABLE_DISK_A: {
            if (c1541_A) {
                c1541_A->drive_power(true);
            }
            *reply = &c_message_empty;
            *status = &c_status_ok;
            break;
        }
        case CTRL_CMD_DISABLE_DISK_A: {
            if (c1541_A) {
                c1541_A->drive_power(false);
            }
            *reply = &c_message_empty;
            *status = &c_status_ok;
            break;
        }
        case CTRL_CMD_ENABLE_DISK_B: {
            if (c1541_B) {
                c1541_B->drive_power(true);
            }
            *reply = &c_message_empty;
            *status = &c_status_ok;
            break;
        }
        case CTRL_CMD_DISABLE_DISK_B: {
            if (c1541_B) {
                c1541_B->drive_power(false);
            }
            *reply = &c_message_empty;
            *status = &c_status_ok;
            break;
        }
        case CTRL_CMD_DISK_A_POWER: {
            bool drivepower = false;

            if (c1541_A) {
                drivepower = c1541_A->get_drive_power();
            }

            if(drivepower == false)
            {
                sprintf((char*) data_message.message,"off");
            }
            else
            {
                sprintf((char*) data_message.message,"on ");
            }
                
            data_message.length = 3;
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
            break;
        }
        case CTRL_CMD_DISK_B_POWER: {
            bool drivepower = false;

            if (c1541_B) {
                drivepower = c1541_B->get_drive_power();
            }

            if(drivepower == false)
            {
                sprintf((char*) data_message.message,"off");
            }
            else
            {
                sprintf((char*) data_message.message,"on ");
            }
                
                
            data_message.length = 3;
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
            break;
        }
#ifdef U64
        case CTRL_CMD_U64_SAVEMEM:
            printf("U64 Save C64 Memory\n");
            save_u64_memory(command);
            *reply  = &c_message_empty;
            *status = &c_status_ok;
            break;
#endif
        case CTRL_CMD_EASYFLASH:
            if (command->length < 3)
            {
                *status = &c_status_unknown_command;
                *reply = &c_message_empty;
            }
            else
            {
                unsigned char subcommand = command->message[2];
                switch (subcommand)
                {
                    case 0:
                    {
                        if (command->length < 5)
                        {
                            *status = &c_status_unknown_command;
                            *reply = &c_message_empty;
                            break;
                        }

                        uint32_t mem_addr = ((uint32_t)C64_CARTRIDGE_RAM_BASE) << 16;
                        unsigned char bank = command->message[3];
                        unsigned char baseAddr = command->message[4];
                        mem_addr += (bank & 0x38) * 8192;
                        if (baseAddr & 0x20) mem_addr += 512*1024;
                        for (int i=0; i<65536; i++)
                            *(char*)(mem_addr+i) = 0xff;
                        *reply  = &c_message_empty;
                        *status = &c_status_ok;
                        break;
                    }

                    default:
                        *reply = &c_message_empty;
                        *status = &c_status_unknown_command;
                }
                *reply  = &c_message_empty;
                *status = &c_status_ok;
            }
            break;

        case CTRL_CMD_LOAD_REU:
        case CTRL_CMD_SAVE_REU:
            *reply  = &data_message;
            *status = &c_status_ok;
            if (command->message[1] == CTRL_CMD_LOAD_REU) {
                retVal = reu_preloader->LoadREU((char *)data_message.message + 4);
            } else {
                retVal = reu_preloader->SaveREU((char *)data_message.message + 4);
            }
            pul = (uint32_t *)data_message.message;
            data_message.last_part = true;
            *pul = cpu_to_32le(retVal);
            strupr((char *)data_message.message + 4);
            data_message.length = 4 + strlen((char *)data_message.message + 4);

            if (retVal == -3) {
                *status = &c_status_reu_not_saved;
            } else if (retVal == -2) {
                *status = &c_status_reu_disabled;
            } else if (retVal == -1) {
                *status = &c_status_cannot_open_file;
            }
            break;

        case CTRL_CMD_GET_HWINFO: {
        	
            if (command->length < 3)
            {
                *status = &c_status_unknown_command;
                *reply = &c_message_empty;
            }
            else
            {
                int device = command->message[2];
                int frmt = command->message[3];
#ifndef U64
                if (device == 0) {
                    unsigned char* data = (unsigned char*) data_message.message;
                    strcpy((char*) data, "1541 ULTIMATE II");
                    data_message.length = strlen((char*) data);
    
                    *status = &c_status_ok;
                    data_message.last_part = true;
                    *reply = &data_message;
                }
                else if (device == 1) {
                    unsigned char* data = (unsigned char*) data_message.message;
                    unsigned char el = !!ioRead8(SID_ENABLE_LEFT);
                    unsigned char er = !!ioRead8(SID_ENABLE_RIGHT);
                    unsigned char bl = ioRead8(SID_BASE_LEFT);
                    unsigned char br = ioRead8(SID_BASE_RIGHT);

                    data[0] = el+er;
                    data_message.length = 1 + data[0] * 5;
                    if (el)
                        {
                            data[1] = bl << 4;
                            data[2] = 0xd0 | (bl >> 4);
                            data[3] = 0;
                            data[4] = 0;
                            data[5] = 1;
                        }
                        if (er)
                        {
                            data[1+5*el] = br << 4;
                            data[2+5*el] = 0xd0 | (br >> 4);
                            data[3+5*el] = 0;
                            data[3+5*el] = 0;
                            data[4+5*el] = 1;
                        }
                        *status = &c_status_ok;
                        data_message.last_part = true;
                        *reply = &data_message;
                }
                else {
                    *reply = &c_message_empty;
                    *status = &c_status_unknown_command;
                }
#else // U64
                if (device == 0) {
                    char* data = (char*) data_message.message;
                    strcpy(data, "ULTIMATE 64");
                    data_message.length = strlen(data);
                    *status = &c_status_ok;
                    data_message.last_part = true;
                    *reply = &data_message;
                }
                else if (device == 1) {
               	    unsigned char* data = (unsigned char*) data_message.message;
                
               	    data[0] = 2 + (C64_SID1_EN_BAK ? 1 : 0) + (C64_SID2_EN_BAK ? 1 : 0);
               	    data_message.length = 11 + (C64_SID1_EN_BAK ? 5 : 0) + (C64_SID2_EN_BAK ? 5 : 0);

                    unsigned int base, mask;
                    base = C64_EMUSID1_BASE_BAK; mask = C64_EMUSID1_MASK_BAK;
                    data[1] = base << 4;
                    data[2] = 0xd0 | (base >> 4);
                    data[3] = 0;
                    data[4] = 0;
                    data[5] = ((mask == 0xFE) ? 2 : 3) | (true ? 128 : 0);
                    
                    base = C64_EMUSID2_BASE_BAK; mask = C64_EMUSID2_MASK_BAK;
                    data[6] = base << 4;
                    data[7] = 0xd0 | (base >> 4);
                    data[8] = 0;
                    data[9] = 0;
                    data[10] = ((mask == 0xFE) ? 2 : 3) | (true ? 128 : 0);
    
                    if ( C64_SID1_EN_BAK )
                    {
                    	base = C64_SID1_BASE_BAK; mask = C64_SID1_MASK_BAK;
                        data[11] = base << 4;
                        data[12] = 0xd0 | (base >> 4);
                        data[13] = 0;
                        data[14] = 0;
                        if ( C64_SID1_EN_BAK == 4 )
                        {
                           data[13] = data[11] | (C64_STEREO_ADDRSEL_BAK ? 0x20: 0);
                           data[14] = data[12] | (C64_STEREO_ADDRSEL_BAK ? 0: 1);
                        }
                        data[15] = ((mask == 0xFE) ? 4 : 5) | (true ? 128 : 0);
                    }
                    uint8_t ofsTmp = C64_SID1_EN_BAK ? 5 : 0;
                    
                    if ( C64_SID2_EN_BAK )
                    {
                        base = C64_SID2_BASE_BAK; mask = C64_SID2_MASK_BAK;
                        data[11+ofsTmp] = base << 4;
                        data[12+ofsTmp] = 0xd0 | (base >> 4);
                        data[13+ofsTmp] = 0;
                        data[14+ofsTmp] = 0;
                        if ( C64_SID2_EN_BAK == 4 )
                        {
                           data[13+ofsTmp] = data[11+ofsTmp] | (C64_STEREO_ADDRSEL_BAK ? 0x20: 0);
                           data[14+ofsTmp] = data[12+ofsTmp] | (C64_STEREO_ADDRSEL_BAK ? 0: 1);
                        }
                        data[15+ofsTmp] = ((mask == 0xFE) ? 4 : 5) | (true ? 128 : 0);
                    }
    
                    *status = &c_status_ok;
                    data_message.last_part = true;
                    *reply = &data_message;
                }
                else {
                    *reply = &c_message_empty;
                    *status = &c_status_unknown_command;
                }
#endif
            }
            break;
        }
    
    }
}

#ifdef U64
void ControlTarget :: save_u64_memory(Message *command)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f;

    FRESULT res = fm->fopen("/Usb1", "c64_memory.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
    if(res == FR_OK) {
        printf("Opened file successfully.\n");

        uint8_t *src = (uint8_t *)U64_RAM_BASE;
        uint8_t *dest = new uint8_t[65536];
        memcpy(dest, src, 65536);
        uint32_t bytes_written;

        f->write(dest, 0x10000, &bytes_written);
        printf("written: %d...", bytes_written);
        fm->fclose(f);
        delete[] dest;
    }
}
#endif

void ControlTarget :: get_more_data(Message **reply, Message **status)
{
	*reply  = &c_message_empty;
	*status = &c_status_no_data;
}

void ControlTarget :: abort(void) {

}
