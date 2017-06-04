#include "control_target.h"
#include "disk_image.h"
#include <string.h>

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

static Message c_message_identification     = { 19, true, (uint8_t *)"CONTROL TARGET V1.0" };
static Message c_status_invalid_params      = { 17, true, (uint8_t *)"81,INVALID PARAMS" };
static Message c_status_errors_on_track     = { 18, true, (uint8_t *)"82,ERRORS ON TRACK" };
static Message c_status_no_data             = { 19, true, (uint8_t *)"83,NOT IN DATA MODE" };

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
    switch(command->message[1]) {
        case CTRL_CMD_IDENTIFY:
		    *reply  = &c_message_identification;
            *status = &c_status_ok;
            break;            
        case CTRL_CMD_DECODE_TRACK:
        	decode_track(command, reply, status);
        	break;

        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command;
    }
}

void ControlTarget :: get_more_data(Message **reply, Message **status)
{
	*reply  = &c_message_empty;
	*status = &c_status_no_data;
}

void ControlTarget :: abort(void) {

}
