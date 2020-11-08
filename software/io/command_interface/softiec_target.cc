#include "softiec_target.h"
#include "dump_hex.h"
#include "c64.h"

#define SIEC_TARGET_DEBUG 0

// crate and register ourselves!
SoftIECTarget softIecTarget(5);

static Message c_message_identification     = { 24, true, (uint8_t *)"SOFTWARE IEC TARGET V0.1" };
static Message c_status_all_ok              = {  1, true, (uint8_t *)"\x00" };
static Message c_status_file_not_found      = {  1, true, (uint8_t *)"\x01" };
static Message c_status_save_error          = {  1, true, (uint8_t *)"\x02" };
static Message c_status_no_input_channel    = {  1, true, (uint8_t *)"\x03" };
static Message c_status_unknown_cmd_local   = {  1, true, (uint8_t *)"\x04" };

SoftIECTarget :: SoftIECTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
    input_channel = NULL;
    input_length = 0;
    startaddr = 0;
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
#if SIEC_TARGET_DEBUG
            printf("LOAD_SU command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_load_su(command, reply, status);
            break;
        case SOFTIEC_CMD_LOAD_EX:
#if SIEC_TARGET_DEBUG
            printf("LOAD_EX command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_load_ex(command, reply, status);
            break;
        case SOFTIEC_CMD_SAVE:
#if SIEC_TARGET_DEBUG
            printf("SAVE command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_save(command, reply, status);
            break;
        case SOFTIEC_CMD_OPEN:
#if SIEC_TARGET_DEBUG
            printf("OPEN command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_open(command, reply, status);
            break;
        case SOFTIEC_CMD_CLOSE:
#if SIEC_TARGET_DEBUG
            printf("CLOSE command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_close(command, reply, status);
            break;
        case SOFTIEC_CMD_CHKOUT:
#if SIEC_TARGET_DEBUG
            printf("CHKOUT command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_chkout(command, reply, status);
            break;
        case SOFTIEC_CMD_CHKIN:
#if SIEC_TARGET_DEBUG
            printf("CHKIN command:\n");
            dump_hex(command->message, command->length);
#endif
            cmd_chkin(command, reply, status);
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
    // 2: secondary address
    // 3: verify flag
    // 4,5 : load address
    // 6,7 : end address (for save)
    // 8- : name
//    *reply  = &data_message;
    *reply  = &c_message_empty;
    *status = &status_message;

    IecChannel *channel;

    command->message[command->length] = 0; // make it a null-terminated string
    channel = iec_if.get_data_channel(0); // corresponds to ATN byte $60

    data_message.length = 0;
    int result = channel->ext_open_file((const char *)&command->message[8]);
    if (result) {
        startaddr = get_start_addr(channel);
        data_message.length = 2;
        data_message.message[0] = (uint8_t)(startaddr & 0xFF);
        data_message.message[1] = (uint8_t)(startaddr >> 8);

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
    // 2: secondary address
    // 3: verify flag
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

void SoftIECTarget :: cmd_open(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: secondary address
    // 3: unused
    *reply  = &c_message_empty;
    *status = &c_message_empty;

    IecChannel *channel;

    command->message[command->length] = 0; // make it a null-terminated string
    channel = iec_if.get_data_channel((int)command->message[2]); // also works for command channel ;-)

    channel->ext_open_file((const char *)&command->message[4]);

    input_channel = NULL;
}

void SoftIECTarget :: cmd_chkin(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: unused
    // 3: secondary address
    *reply  = &data_message;
    *status = &c_message_empty;

    input_channel = iec_if.get_data_channel((int)command->message[2]); // also works for command channel ;-)
    input_channel->talk();
    prepare_data(32); // we don't know yet how serious the client is about reading, so let's give him a little
}

void SoftIECTarget :: prepare_data(int count)
{
    data_message.last_part = false;

    uint8_t data;
    int len = 0;
    for(int i=0; i < count; i++) {
        int res = input_channel->prefetch_data(data);
        if ((res == IEC_OK) || (res == IEC_LAST)) {
            data_message.message[i] = data;
            len++;
        }
        if ((res == IEC_LAST) || (res == IEC_NO_FILE)) { // the latter is actually an error
            data_message.last_part = true;
            break;
        }
        if (res == IEC_BUFFER_END) {
            break; // simply break and wait for pop (when get more data is called)
        }
    }
    data_message.length = len;
    // the number of bytes that we prepared for reading is stored here, such that these bytes can be popped
    // whenever get_more_data is called.
    input_length = len;
#if SIEC_TARGET_DEBUG > 1
    dump_hex(data_message.message, len);
#endif
}

void SoftIECTarget :: get_more_data(Message **reply, Message **status)
{
    if (input_channel) {
        for(int i=0; i<input_length; i++) {
            input_channel->pop_data(); // how about passing the number of bytes, that would be way more efficient.
        }
        prepare_data(256);
        *reply = &data_message;
        *status = &c_status_all_ok;
    } else {
        *reply = &c_message_empty;
        *status = &c_status_no_input_channel;
    }
}

void SoftIECTarget :: abort(int a)
{
#if SIEC_TARGET_DEBUG
    printf("Abrt(%d)\n", a);
#endif
    if (input_channel) {
        int bytes = (a < input_length) ? a : input_length;
        for(int i=0; i<bytes; i++) {
            input_channel->pop_data(); // how about passing the number of bytes, that would be way more efficient.
        }
        input_channel->reset_prefetch();
        input_channel = NULL;
    }
}

void SoftIECTarget :: cmd_chkout(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: secondary address
    // 3: unused
    // 4 -: data
    *reply  = &c_message_empty;
    *status = &c_status_ok;

    input_channel = NULL;

    IecChannel *channel = iec_if.get_data_channel((int)command->message[2]); // also works for command channel ;-)
    for(int i=4; i<command->length; i++) {
        channel->push_data(command->message[i]);
    }
    channel->push_command(0); // end
}

void SoftIECTarget :: cmd_close(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: secondary address
    // 3: unused
    *reply  = &c_message_empty;
    *status = &c_status_ok;

    IecChannel *channel = iec_if.get_data_channel((int)command->message[2]); // also works for command channel ;-)
    channel->ext_close_file();
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
    uint16_t addr = startaddr;

    C64 *c64 = C64 :: getMachine();
    if (!cmd_if.is_dma_active()) {
        c64->stop(false);
    }

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
        addr ++;
        if (!addr) {
            break; // wrap!
        }
    } while(res2 != IEC_NO_FILE);

    if (!cmd_if.is_dma_active()) {
        c64->resume();
    }
    return (error == 0);
}

uint16_t SoftIECTarget :: do_load(IecChannel *chan, uint16_t addr)
{
    volatile uint8_t *dest = (volatile uint8_t *)C64_MEMORY_BASE;
    dest += addr;

    C64 *c64 = C64 :: getMachine();
    if (!cmd_if.is_dma_active()) {
        c64->stop(false);
    }

#if SIEC_TARGET_DEBUG
    printf("Loading %04x-", addr);
#endif
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
        if (!addr) {
            break; // wrap!
        }
    } while(res2 != IEC_NO_FILE);

    if (!cmd_if.is_dma_active()) {
        c64->resume();
    }
#if SIEC_TARGET_DEBUG
    printf("%04x\n", addr);
#endif
    return addr;
}

bool SoftIECTarget :: do_save(IecChannel *chan, uint16_t start, uint16_t end)
{
    volatile uint8_t *dest = (volatile uint8_t *)C64_MEMORY_BASE;
    dest += start;

    chan->push_data(start & 0xFF);
    chan->push_data(start >> 8);

    C64 *c64 = C64 :: getMachine();
    if (!cmd_if.is_dma_active()) {
        c64->stop(false);
    }
    bool success = true;
    for(uint16_t i=start; i != end; i++) {
        int res = chan->push_data(*(dest++));
        if (res != IEC_OK) {
            success = false;
        }
    }

    if (!cmd_if.is_dma_active()) {
        c64->resume();
    }
    return success;
}

