#include "iec_drive.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"

void outbyte(int c) { putc(c, stdout); }
CommandInterface cmd_if;

BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

void init_ram_disk()
{
    const int sz = 512;
    const int size = 1024 * 1024;
    const int sectors = size / sz;
    uint8_t *ramdisk_mem = new uint8_t[size]; // 1 MB only
    ramdisk_blk = new BlockDevice_Ram(ramdisk_mem, sz, sectors);
    {
        FileSystem *ramdisk_fs;
        Partition *ramdisk_prt;
        ramdisk_prt = new Partition(ramdisk_blk, 0, 0, 0);
        ramdisk_fs  = new FileSystemFAT(ramdisk_prt);
        ramdisk_fs->format("RamDisk");
        delete ramdisk_fs;
        delete ramdisk_prt;
    }
    ramdisk_node = new FileDevice(ramdisk_blk, "Temp", "RAM Disk");
    ramdisk_node->attach_disk(sz);
    FileManager :: getFileManager()->add_root_entry(ramdisk_node);
}

void get_status(IecDrive *dr)
{
    uint8_t *data;
    int data_size;

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6f); // channel 15
    dr->talk();
    dr->prefetch_more(256, data, data_size);
    dump_hex_relative(data, data_size);
    dr->pop_more(data_size);
}

void get_status2(IecDrive *dr)
{
    uint8_t byte;
    t_channel_retval ret1, ret2;

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6f); // channel 15
    dr->talk();
    do {
        ret1 = dr->prefetch_data(byte);
        ret2 = dr->pop_data();
        printf("(%d) %02x (%d)\n", ret1, byte, ret2);
    } while(ret1 == IEC_OK);
    printf(".\n");
}

void send_command(IecDrive *dr, const char *cmd)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6F); // open channel 15
    for(int i=0;i<strlen(cmd);i++) {
        dr->push_data((uint8_t)cmd[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
}

int main(int argc, const char **argv)
{
    UserInterface *ui = new UserInterface("Test Drive");

    IecDrive *dr = new IecDrive();
    t_channel_retval ret;
    uint8_t *data;
    int data_size;
    uint32_t tr;

    init_ram_disk();
    File *f;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen("/Temp/h.prg", FA_CREATE_NEW | FA_WRITE, &f);
    if (fres == FR_OK) {
        f->write("This is really a silly test.", 28, &tr);
        printf("Written %u bytes to the file.\n", tr);
        fm->fclose(f);
    }

    get_status(dr);
    get_status2(dr);
    get_status2(dr);
    send_command(dr, "CD:/Temp");

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0); // open channel 0!
    dr->push_data('H'); // Interesting filename!
    dr->push_ctrl(SLAVE_CMD_EOI);
    dr->talk();

    ret = dr->prefetch_more(256, data, data_size);
    printf("Ret: %d Count = %d\n", ret, data_size);
    dump_hex_relative(data, data_size);
    dr->pop_more(data_size);

    ret = dr->prefetch_more(256, data, data_size);
    printf("Ret: %d Count = %d\n", ret, data_size);
    dump_hex_relative(data, data_size);
    dr->pop_more(data_size);

    ret = dr->prefetch_more(256, data, data_size);
    printf("Ret: %d Count = %d\n", ret, data_size);
    dump_hex_relative(data, data_size);
    dr->pop_more(data_size);

    ret = dr->prefetch_more(256, data, data_size);
    printf("Ret: %d Count = %d\n", ret, data_size);
    dump_hex_relative(data, data_size);
    dr->pop_more(data_size);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0); // close

    delete dr;
    delete ui;
    return 0;
}
