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
    data[data_size] = 0;
    printf("Status: %s\n", data);
    //dump_hex_relative(data, data_size);
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
    get_status(dr);
}

void open_file(IecDrive *dr, const char *fn)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0); // open channel 0!
    while(*fn) dr->push_data(*(fn++));
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

    const uint8_t *msg = (const uint8_t *)"This is really a silly test.";
    FRESULT fres;
    fres = fm->save_file(false, "/Temp", "a.prg", msg, 28, &tr);
    if (fres == FR_OK) {
        printf("Written %u bytes to the file.\n", tr);
    } else {
        printf("%s\n", FileSystem::get_error_string(fres));
    }
    fm->save_file(false, "/Temp", "bb.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "ccc.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "dddd.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "eeeee.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "ffffff.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "ggggggg.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "hhhhhhhh.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "iiiiiiiii.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "jjjjjjjjjj.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "kkkkkkkkkkk.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "llllllllllll.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "mmmmmmmmmmmmm.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "nnnnnnnnnnnnnn.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "ooooooooooooooo.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "pppppppppppppppp.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "qqqqqqqq012345678.prg", msg, 28, &tr);
    fm->save_file(false, "/Temp", "abc{2f}def.usr", msg, 28, &tr);
    fm->save_file(false, "/Temp", "{c1c2c3}.seq", msg, 28, &tr);
    fm->create_dir("/Temp/SomeDir");
    fm->create_dir("/Temp/OtherDir");
    get_status(dr);
    send_command(dr, "CD/TEMP");
    send_command(dr, "CD/SOMEDIR");
    send_command(dr, "CD/_/OTHERDIR2");
    open_file(dr, "$=T0:*=L");
//    open_file(dr, "$=P:*=L");
    get_status(dr);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60);
    dr->talk();
    do {
        ret = dr->prefetch_more(256, data, data_size);
        printf("Ret: %d Count = %d\n", ret, data_size);
        dump_hex_relative(data, data_size);
        dr->pop_more(data_size);
    } while(ret == 0);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0); // close

    get_status(dr);

    delete dr;
    delete ui;
    return 0;
}
