#include "softiec_target.h"
#include "dump_hex.h"
#include "c64.h"

// crate and register ourselves!
SoftIECTarget softIecTarget(5);

static Message c_message_identification     = { 24, true, (uint8_t *)"SOFTWARE IEC TARGET V0.1" };
static Message c_message_first_message      = { 28,false, (uint8_t *)"THIS IS THE FIRST MESSAGE..." };
static Message c_message_second_message     = { 11, true, (uint8_t *)"SECOND MSG!" };
static Message c_status_all_ok              = {  1, true, (uint8_t *)"\x00" };
static Message c_status_file_not_found      = {  1, true, (uint8_t *)"\x01" };
static Message c_status_save_error          = {  1, true, (uint8_t *)"\x02" };
static Message c_status_unknown_cmd_local   = {  1, true, (uint8_t *)"\x03" };

SoftIECTarget :: SoftIECTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
}
   

SoftIECTarget :: ~SoftIECTarget()
{
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    

void SoftIECTarget :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the target

    *reply  = &c_message_empty;
    *status = &c_status_unknown_cmd_local;

    switch(command->message[1]) {
        case SOFTIEC_CMD_IDENTIFY:
		    *reply  = &c_message_identification;
            *status = &c_status_ok;
            break;
        case SOFTIEC_CMD_LOAD_SU:
            printf("LOAD_SU command:\n");
            dump_hex(command->message, command->length);
            cmd_load_su(command, reply, status);
            break;
        case SOFTIEC_CMD_LOAD_EX:
            printf("LOAD_EX command:\n");
            dump_hex(command->message, command->length);
            cmd_load_ex(command, reply, status);
            break;
        case SOFTIEC_CMD_SAVE:
            printf("SAVE command:\n");
            cmd_save(command, reply, status);
            break;
        case SOFTIEC_CMD_OPEN:
            printf("OPEN command:\n");
            dump_hex(command->message, command->length);
            break;
        case SOFTIEC_CMD_CLOSE:
            printf("CLOSE command:\n");
            dump_hex(command->message, command->length);
            break;
        case SOFTIEC_CMD_CHKOUT:
            printf("CHKOUT command:\n");
            dump_hex(command->message, command->length);
            break;
        case SOFTIEC_CMD_CHKIN:
            printf("CHKIN command:\n");
            dump_hex(command->message, command->length);
            *reply  = &c_message_first_message;
            *status = &c_status_ok;
            break;
        default:
            printf("Unknown command:\n");
            dump_hex(command->message, command->length);
            *reply  = &c_message_empty;
            *status = &c_status_file_not_found;
            break;
    }
}

void SoftIECTarget :: cmd_load_su(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: verify flag
    // 3: secondary address
    // 4,5 : load address
    // 6,7 : end address (for save)
    // 8- : name
    *reply  = &c_message_empty;
    *status = &status_message;

    IecChannel *channel;

    command->message[command->length] = 0; // make it a null-terminated string
    channel = iec_if.get_data_channel(0); // corresponds to ATN byte $60

    int result = channel->ext_open_file((const char *)&command->message[8]);
    if (result) {
        startaddr = get_start_addr(channel);
        if (!command->message[2]) {
            startaddr = (((uint16_t)(command->message[5])) << 8) | command->message[4];
        }
        *status = &c_status_all_ok; // What if the first two bytes cannot be read?
    } else {
        *status = &c_status_file_not_found;
    }
}

void SoftIECTarget :: cmd_load_ex(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: verify flag
    // 3: secondary address
    *reply  = &c_message_empty;
    *status = &status_message;

    IecChannel *channel;

    channel = iec_if.get_data_channel(0); // corresponds to ATN byte $60

    status_message.length = 1;
    status_message.message[0] = 0;
    if (command->message[3]) {
        if (!do_verify(channel, startaddr)) {
            status_message.message[0] = 0x80; // verify error
        }
    } else {
        uint16_t endAddr = do_load(channel, startaddr);
        status_message.length = 3;
        status_message.message[1] = (uint8_t)(endAddr & 0xFF);
        status_message.message[2] = (uint8_t)(endAddr >> 8);
    }
    channel->ext_close_file();
}


void SoftIECTarget :: cmd_save(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: verify flag
    // 3: secondary address
    // 4,5 : load address
    // 6,7 : end address (for save)
    // 8- : name
    *reply  = &c_message_empty;
    *status = &c_status_all_ok;

    IecChannel *channel;

    dump_hex(command->message, command->length);
    command->message[command->length] = 0; // make it a null-terminated string
    channel = iec_if.get_data_channel(1); // corresponds to ATN byte $61

    int result = channel->ext_open_file((const char *)&command->message[8]);
    if (result) {
        uint16_t startaddr = (((uint16_t)(command->message[5])) << 8) | command->message[4];
        uint16_t endaddr = (((uint16_t)(command->message[7])) << 8) | command->message[6];
        if (!do_save(channel, startaddr, endaddr)) {
            *status = &c_status_save_error;
        }
        channel->ext_close_file();
    } else {
        *status = &c_status_save_error;
    }
}

uint16_t SoftIECTarget :: get_start_addr(IecChannel *chan)
{
    uint8_t addr_lo, addr_hi;
    chan->prefetch_data(addr_lo);
    chan->prefetch_data(addr_hi);
    chan->pop_data();
    chan->pop_data();
    uint16_t startaddr = (((uint16_t)addr_hi) << 8) | addr_lo;
    return startaddr;
}

bool SoftIECTarget :: do_verify(IecChannel *chan, uint16_t startaddr)
{
    volatile uint8_t *dest = (volatile uint8_t *)C64_MEMORY_BASE;
    dest += startaddr;

    C64 *c64 = C64 :: getMachine();
    c64->stop(false);

    uint8_t data;
    int res1, res2;
    int error = 0;
    do {
        res1 = chan->prefetch_data(data);
        res2 = chan->pop_data();
        if (res2 == IEC_READ_ERROR) {
            error = 1;
            break;
        }
        if (*(dest++) != data) {
            error = 1;
            break;
        }
    } while(res2 != IEC_NO_FILE);

    c64->resume();
    return (error == 0);
}

uint16_t SoftIECTarget :: do_load(IecChannel *chan, uint16_t addr)
{
    volatile uint8_t *dest = (volatile uint8_t *)C64_MEMORY_BASE;
    dest += addr;

    C64 *c64 = C64 :: getMachine();
    c64->stop(false);

    uint8_t data;
    int res1, res2;
    int error = 0;
    do {
        res1 = chan->prefetch_data(data);
        res2 = chan->pop_data();
        if (res2 == IEC_READ_ERROR) {
            error = 1;
            break;
        }
        *(dest++) = data;
        addr ++;
    } while(res2 != IEC_NO_FILE);

    c64->resume();
    return addr;
}

bool SoftIECTarget :: do_save(IecChannel *chan, uint16_t start, uint16_t end)
{
    volatile uint8_t *dest = (volatile uint8_t *)C64_MEMORY_BASE;
    dest += start;

    chan->push_data(start & 0xFF);
    chan->push_data(start >> 8);

    C64 *c64 = C64 :: getMachine();
    c64->stop(false);

    bool success = true;
    for(uint16_t i=start; i != end; i++) {
        int res = chan->push_data(*(dest++));
        if (res != IEC_OK) {
            success = false;
        }
    }

    c64->resume();
    return success;
}

void SoftIECTarget :: get_more_data(Message **reply, Message **status)
{
	*reply  = &c_message_second_message;
	*status = &c_message_empty;
}

void SoftIECTarget :: abort(int a)
{
    printf("SoftIECTarget with Abort = %d.\n", a);
}
