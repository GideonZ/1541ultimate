#include "iec_drive.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"

void outbyte(int c) { putc(c, stdout); }
CommandInterface cmd_if;

BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

char last_status[128];

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

    memcpy(last_status, data, data_size);
    last_status[data_size] = 0;
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

const char *send_command(IecDrive *dr, const char *cmd)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6F); // open channel 15
    for(int i=0;i<strlen(cmd);i++) {
        dr->push_data((uint8_t)cmd[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
    get_status(dr);
    return last_status;
}

void open_file(IecDrive *dr, const char *fn)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0); // open channel 0!
    while(*fn) dr->push_data(*(fn++));
    dr->push_ctrl(SLAVE_CMD_EOI);
}

void write_file(IecDrive *dr, uint8_t chan, const char *fn, const char *msg)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0 | chan);
    while(*fn) dr->push_data(*(fn++));
    dr->push_ctrl(SLAVE_CMD_EOI);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    while(*msg) dr->push_data(*(msg++));
    dr->push_ctrl(SLAVE_CMD_EOI);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0 | chan);
    dr->push_ctrl(SLAVE_CMD_EOI);
}

void read_directory(IecDrive *dr, const char *file)
{
    open_file(dr, file);
    get_status(dr);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60);
    dr->talk();
    int ret;
    uint8_t *data;
    int data_size;
    do {
        ret = dr->prefetch_more(256, data, data_size);
        printf("Ret: %d Count = %d\n", ret, data_size);
        dump_hex_relative(data, data_size);
        dr->pop_more(data_size);
    } while(ret == 0);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0); // close

    get_status(dr);
}

const uint8_t *testmsg = (const uint8_t *)"This is really a silly test.";
void create_test_files(FileManager *fm)
{
    uint32_t tr;
    FRESULT fres;
    fres = fm->save_file(false, "/Temp", "a.prg", testmsg, 28, &tr);
    if (fres == FR_OK) {
        printf("Written %u bytes to the file.\n", tr);
    } else {
        printf("%s\n", FileSystem::get_error_string(fres));
    }
    fm->save_file(false, "/Temp", "bb.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ccc.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc2.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc3.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "dddd.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "eeeee.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ffffff.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ggggggg.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "hhhhhhhh.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "iiiiiiiii.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "jjjjjjjjjj.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "kkkkkkkkkkk.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "llllllllllll.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "mmmmmmmmmmmmm.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "nnnnnnnnnnnnnn.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ooooooooooooooo.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "pppppppppppppppp.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "qqqqqqqq012345678.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "abc{2f}def.usr", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "{c1c2c3}.seq", testmsg, 28, &tr);
    fm->create_dir("/Temp/SomeDir");
    fm->create_dir("/Temp/OtherDir");
    fm->create_dir("/Temp/OtherDir/Level2");
    fm->create_dir("/Temp/blah{c1c2}");
}

int execute_suite1(FileManager *fm, IecDrive *dr)
{
    mstring status;
    int error = 0;
    FRESULT fres;
    uint32_t tr;

    fm->create_dir("/Temp/Partition2");
    dr->add_partition(1, "/Temp");
    dr->add_partition(2, "/Temp/Partition2");

    status = send_command(dr, "CP1");
    if(status != "02,PARTITION SELECTED,01,00\r") error++;

    status = send_command(dr, "CD/TEMP");
    if(status != "71,DIRECTORY ERROR,01,00\r") error++;

    status = send_command(dr, "CD/SOMEDIR");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD/_/OTHERDIR");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD:LEVEL2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD//");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD/OTHERDIR/LEVEL2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD//:BLAH\xc1\xc2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD_");
    if(status != "00, OK,00,00\r") error++;

    read_directory(dr, "$=T0:*=L");

    // send_command(dr, "MD//HOI");
    status = send_command(dr, "MD:HOI");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD/OTHERDIR:DEEPER");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD_/OTHERDIR:DEEPER");
    if(status != "71,DIRECTORY ERROR,00,00\r") error++;
    status = send_command(dr, "MD:_/DEEPER");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "RD:HOI");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD:HOI");
    if(status != "00, OK,00,00\r") error++;

    fres = fm->save_file(false, "/Temp/HOI", "mmmmmmmmmmmmm.prg", testmsg, 28, &tr);
    if (fres == FR_OK) {
        printf("Written %u bytes to the file.\n", tr);
    } else {
        printf("%s\n", FileSystem::get_error_string(fres));
    }
    status = send_command(dr, "RD:HOI");
    if(status != "63,FILE EXISTS,00,00\r") error++;

    // read_directory(dr, "$");
    status = send_command(dr, "T-RA");
    if(status != "WED. 26/06/25 12:41:01 AM\r") error++;
    status = send_command(dr, "T-RI");
    if(status != "2025-06-26T00:41:01 WED\r") error++;
    status = send_command(dr, "T-RB");

    status = send_command(dr, "C2:DEST=");
    if(status != "32,SYNTAX ERROR,00,00\r") error++;
    status = send_command(dr, "C2:DEST=1:A,1:BB");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CP2");
    if(status != "02,PARTITION SELECTED,02,00\r") error++;

    status = send_command(dr, "RENAME1:DOOM=1:DDDD");
    if(status != "00, OK,00,00\r") error++;

    status = send_command(dr, "RENAME2:DOOM=1:DOOM");
    if(status != "00, OK,00,00\r") error++;

    // send_command(dr, "CP3");
    read_directory(dr, "$1");
    // send_command(dr, "CP2");
    //read_directory(dr, "$=P");
    status = send_command(dr, "SCRATCH1:C*");
    if(status != "01, FILES SCRATCHED,03,00\r") error++;

    read_directory(dr, "1:A");

    write_file(dr, 3, "2:WRITETEST,U,W", "This is some random string that should be written to an open file.");

    fm->print_directory("/Temp/Partition2");
    return error;
}

int main(int argc, const char **argv)
{
    UserInterface *ui = new UserInterface("Test Drive");

    IecDrive *dr = new IecDrive();
    t_channel_retval ret;
    uint32_t tr;
    int error = 0;
    mstring status;

    init_ram_disk();
    File *f;
    FileManager *fm = FileManager :: getFileManager();

    //create_test_files(fm);
    //error = execute_suite1(fm, dr);
    read_directory(dr, "$=P");
    status = send_command(dr, "C\xD0\x0B");
    if(status != "02,PARTITION SELECTED,11,00\r") error++;

    fm->save_file(false, "/Temp", "ccc.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc2.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "abc.prg", testmsg, 28, &tr);
    fm->create_dir("/Temp/Subdir");
    fm->save_file(false, "/Temp/SubDir", "01234.usr", testmsg, 28, &tr);
    read_directory(dr, "$:*=B");
    read_directory(dr, "$/Subdir");
    status = send_command(dr, "CD/Subdir");
    if(status != "00, OK,00,00\r") error++;
    read_directory(dr, "$//");
    read_directory(dr, "$_");

    printf("Errors: %d\n", error);
    return 0;

    delete dr;
    delete ui;
    return 0;
}
