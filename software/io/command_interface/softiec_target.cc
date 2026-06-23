#include "softiec_target.h"
#include "dump_hex.h"
#include "c64.h"
#include <string.h>

#define SIEC_TARGET_DEBUG 0

// crate and register ourselves!
SoftIECTarget softIecTarget(5);

static Message c_message_identification     = { 24, true, (uint8_t *)"SOFTWARE IEC TARGET V1.0" };
static Message c_status_all_ok              = {  1, true, (uint8_t *)"\x00" };
static Message c_status_file_not_found      = {  1, true, (uint8_t *)"\x01" };
static Message c_status_save_error          = {  1, true, (uint8_t *)"\x02" };
static Message c_status_no_input_channel    = {  1, true, (uint8_t *)"\x03" };
static Message c_status_unknown_cmd_local   = {  1, true, (uint8_t *)"\x04" };
static Message c_iec_module_not_loaded      = {  1, true, (uint8_t *)"\x05" };
static Message c_status_invalid_params      = {  1, true, (uint8_t *)"\x06" };
static Message c_status_invalid_name        = {  1, true, (uint8_t *)"\x07" };
static Message c_status_invalid_part        = {  1, true, (uint8_t *)"\x08" };
static Message c_status_invalid_dir         = {  1, true, (uint8_t *)"\x09" };

SoftIECTarget :: SoftIECTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
    input_channel = NULL;
    input_length = 0;
    startaddr = 0;
    total_prepared = 0;
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

    if (!iec_drive) {
        *status = &c_iec_module_not_loaded;
        return;
    }

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
        case SOFTIEC_CMD_ADD_PARTITION:
            cmd_add_partition(command, reply, status);
            break;
        case SOFTIEC_CMD_DEL_PARTITION:
            cmd_del_partition(command, reply, status);
            break;
        case SOFTIEC_CMD_GET_FATNAME:
            cmd_get_fatname(command, reply, status);
            break;
        case SOFTIEC_CMD_GET_IECNAME:
            cmd_get_iecname(command, reply, status);
            break;
        default:
            printf("Unknown command:\n");
            dump_hex(command->message, command->length);
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
    *reply  = &data_message;
    *status = &status_message;

    IecChannel *channel;

    command->message[command->length] = 0; // make it a null-terminated string
    channel = iec_drive->get_data_channel(0); // corresponds to ATN byte $60

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

    channel = iec_drive->get_data_channel(0); // corresponds to ATN byte $60

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
    channel = iec_drive->get_data_channel(1); // corresponds to ATN byte $61

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
    channel = iec_drive->get_data_channel((int)command->message[2]); // also works for command channel ;-)

    channel->ext_open_file((const char *)&command->message[4]);

    input_channel = NULL;
}

void SoftIECTarget :: cmd_chkin(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: unused
    // 3: secondary address
    *status = &c_message_empty;

    input_channel = iec_drive->get_data_channel((int)command->message[2]); // also works for command channel ;-)
    input_channel->talk();
    total_prepared = 0;
    prepare_data(32); // we don't know yet how serious the client is about reading, so let's give him a little
    *reply  = &direct_message;
}

void SoftIECTarget :: prepare_data(int count)
{
    int res = input_channel->prefetch_more(count, direct_message.message, direct_message.length);
    direct_message.last_part = (res != IEC_OK);
    // the number of bytes that we prepared for reading is stored here, such that these bytes can be popped
    // whenever get_more_data is called.
    input_length = direct_message.length;
#if SIEC_TARGET_DEBUG > 1
//    dump_hex(data_message.message, len);
    total_prepared += input_length;
    printf("Prepared %d/%d bytes. Last = %d (Total: %d)\n", input_length, count, direct_message.last_part, total_prepared);
#endif
}

void SoftIECTarget :: get_more_data(Message **reply, Message **status)
{
    if (input_channel) {
#if SIEC_TARGET_DEBUG > 1
        printf("Popping %d bytes.\n", input_length);
#endif
        input_channel->pop_more(input_length);
        prepare_data(256);
        *reply = &direct_message;
        *status = &c_status_all_ok;
    } else {
        *reply = &c_message_empty;
        *status = &c_status_no_input_channel;
    }
}

void SoftIECTarget :: abort(int a)
{
    if (input_channel) {
        int bytes = (a < input_length) ? a : input_length;
#if SIEC_TARGET_DEBUG
        printf("Pop(%d:%d)\n", bytes, a);
#endif
        input_channel->pop_more(bytes);
        input_channel->reset_prefetch();
        input_channel = NULL;
    } else {
#if SIEC_TARGET_DEBUG
        printf("Abrt(%d)\n", a);
#endif
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

    if(input_length) {
        abort(input_length);
    }

    input_channel = NULL;

    IecChannel *channel = iec_drive->get_data_channel((int)command->message[2]); // also works for command channel ;-)
    switch(command->message[2] & 0xF0) {
    case 0xF0: // open
        command->message[command->length] = 0; // make it a null-terminated string
        channel->ext_open_file((const char *)&command->message[4]);
        input_channel = NULL;
        break;
    case 0xE0: // close
        channel->ext_close_file();
        break;
    default:
        for(int i=4; i<command->length; i++) {
            channel->push_data(command->message[i]);
        }
        channel->push_command(0); // end
    }
}

void SoftIECTarget :: cmd_close(Message *command, Message **reply, Message **status)
{
    // 0, 1: TARGET, COMMAND
    // 2: secondary address
    // 3: unused
    *reply  = &c_message_empty;
    *status = &c_status_ok;

    IecChannel *channel = iec_drive->get_data_channel((int)command->message[2]); // also works for command channel ;-)
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
    uint8_t *src;
    int res1, res2;
    int error = 0, len = 0;
    int maxbytes = 0x10000 - addr;
    do {
        int bytes_now = (maxbytes > 512) ? 512 : maxbytes;
        res1 = chan->prefetch_more(bytes_now, src, len);
        if (!len) {
            break;
        }

        memcpy((void *)dest, src, len);
        dest += len;
        addr += len;
        maxbytes -= len;

        res2 = chan->pop_more(len);
        if (res2 == IEC_READ_ERROR) {
            error = 1;
            break;
        }
    } while((res1 != IEC_LAST) && (res2 != IEC_NO_FILE));

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

void SoftIECTarget :: cmd_add_partition(Message *command, Message **reply, Message **status)
{
    // Example 05 20 03 'NAME' ':' '/this/is/a/path' [00]  => 3:NAME => /this/is/a/path
    //         00 01 02 03456  07  089abcdef0123456   17
    if (command->length < 6) {
        *status = &c_status_invalid_params;
        return; 
    }
    int index = command->message[2];
    if (command->message[command->length - 1] == 0) {
        command->length--; // strip last 00
    }
    const char *path = NULL;
    // Copy entire string to ident, then split on colon
    mstring ident((const char *)command->message, 3, command->length-1);
    if (!ident.split(':', &path)) {
        *status = &c_status_invalid_params;
        return; 
    }
    iec_drive->add_partition(index, path, ident.c_str());
    *status = &c_status_all_ok;
}

void SoftIECTarget :: cmd_del_partition(Message *command, Message **reply, Message **status)
{
    if (command->length != 3) {
        *status = &c_status_invalid_params;
        return; 
    }
    int index = command->message[2];
    iec_drive->get_file_system()->RemovePartition(index);
    *status = &c_status_all_ok;
}

// Reuse from iec_channel.cc
    FRESULT resolve_directory_path(FileManager *fm, IecPartition *partition,
                                      mstring path, mstring& full_path,
                                      mstring *relative_path = NULL);
//

void SoftIECTarget :: cmd_get_fatname(Message *command, Message **reply, Message **status)
{
    // 05 22 02 IECNAME => What would the filename be if we try to open IEC name on channel 2?
    // We'll copy some code sequence here
    if ((command->length < 4) || (command->length >= CMD_MAX_COMMAND_LEN)) {
        *status = &c_status_invalid_params;
        return; 
    }

    command->message[command->length] = 0; // make sure it's null terminated
    open_t name;

// typedef struct {
//     filename_t file;
//     bool replace;
//     filetype_t filetype;
//     fileaccess_t access;
//     dir_options_t dir_opt;
//     uint16_t record_size;
// } open_t;

    int channel = command->message[2] & 15;
    int err = parse_open((const char *)&command->message[3], name);
    if (err) {
        *status = &c_status_invalid_name;
        return;
    }
    switch(name.dir_opt.stream) {
    case e_stream_buffer:
        data_message.length = sprintf((char *)data_message.message, "/buffer");
        *reply = &data_message;
        *status = &c_status_all_ok;
        break;
    case e_stream_dir:
        {
            IecPartition *partition = iec_drive->get_file_system()->GetPartition(name.file.partition);
            if (!partition) {
                *status = &c_status_invalid_part;
                return;
            }
            char fatname[48];
            petscii_to_fat(name.file.filename.c_str(), fatname, 48);
            mstring work;
            FRESULT fres = resolve_directory_path(FileManager::getFileManager(), partition, name.file.path, work);
            if (fres == FR_OK) {
                strncpy((char *)data_message.message, work.c_str(), CMD_MAX_REPLY_LEN);
                data_message.length = work.length();
                *reply = &data_message;
                *status = &c_status_all_ok;
            } else {
                *status = &c_status_invalid_dir;
            }
        }
        break;
    case e_stream_partitions:
        data_message.length = sprintf((char *)data_message.message, "/partitions");
        *reply = &data_message;
        *status = &c_status_all_ok;
        break;
    case e_stream_file:
        {
            if (name.access == e_not_set) {
                name.access = (channel == 1) ? e_write : e_read;
            }

            if (name.filetype == e_any) {
                if (channel < 2) {
                    name.filetype = e_prg;
                } else if (name.access != e_read) { // for writes on other channels, default to seq.
                    name.filetype = e_seq;
                }
            }

            IecChannel *channel = iec_drive->get_data_channel((int)command->message[2]); // also works for command channel ;-)
            mstring work;
            const char *full_path = channel->ConstructPath(work, name.file, name.filetype, name.access );
            if (!full_path) {
                *status = &c_status_invalid_dir;
            } else { // yey!
                strncpy((char *)data_message.message, work.c_str(), CMD_MAX_REPLY_LEN);
                data_message.length = work.length();
                *status = &c_status_all_ok;
                *reply = &data_message;
            }
        }
        break;
    default:
        *status = &c_status_invalid_name;
    }
}

void SoftIECTarget :: cmd_get_iecname(Message *command, Message **reply, Message **status)
{
    if ((command->length < 3) || (command->length >= CMD_MAX_COMMAND_LEN)) {
        *status = &c_status_invalid_params;
        return; 
    }

    command->message[command->length] = 0; // make sure it's null terminated
    FileInfo info(128);
    info.lfname[127] = 0;
    strncpy(info.lfname, (const char *)&command->message[2], 127);
    get_extension(info.lfname, info.extension, true);

    filetype_t found_type = e_any;
    char iec_name[24];

    IecPartition::CreateIecName(&info, iec_name, found_type);
    data_message.message[0] = (uint8_t)found_type;
    data_message.length = 1 + strlen(iec_name);
    strcpy((char *)&data_message.message[1], iec_name);
    *reply = &data_message;
    *status = &c_status_all_ok;   
}


