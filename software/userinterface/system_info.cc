/*
 * system_info.cc
 *
 *  Created on: Jul 17, 2021
 *      Author: gideon
 */

#include "system_info.h"
#include "c1541.h"
#include "c64.h"
#include "iec.h"
#include "iec_channel.h"
#include "command_intf.h"
#include "acia.h"

extern C1541 *c1541_A;
extern C1541 *c1541_B;
/*

Drive Status information:

Drive A:      Enabled
Drive type:   1541-II
Mounted disk: this_is_file_name.d65
path:         /Usb0/directory1/directory/2directory3
              /directory4/directory5/    (when too long, let it flow to the next line)

Drive B:      Enabled
Drive type:   1541
Mounted disk: blank_disk.g64. (disk inserted using 'Insert blank disk').
path:         None

 */

void SystemInfo :: drive_info(StreamTextLog &b, C1541 *drive, char letter)
{
    b.format("Drive %c:      %s\n", letter, drive->get_drive_power() ? "Enabled" : "Disabled");
    if (drive->get_drive_power()) {
        b.format("Drive Bus ID: %d\n", drive->get_current_iec_address());
        b.format("Drive type:   %s\n", drive->get_drive_type_string());
        b.format("Drive ROM:    %s\n", drive->get_drive_rom_file());
        mstring path, name;
        drive->get_last_mounted_file(path, name);
        b.format("Mounted disk: %s\n", name.c_str());
        b.format("Path:         %s\n", path.c_str());
    }
    b.format("\n");
}

void SystemInfo :: iec_info(StreamTextLog &b)
{
    char buffer[64];
    b.format("SoftwareIEC:  %s\n", iec_if.iec_enable ? "Enabled" : "Disabled");
    if (iec_if.iec_enable) {
        b.format("Drive Bus ID: %d\n", iec_if.get_current_iec_address());
        iec_if.get_error_string(buffer);
        b.format("Error string: %s\n", buffer);
        for (int i=0; i < MAX_PARTITIONS; i++) {
            const char *p = iec_if.get_partition_dir(i);
            if (p) {
                b.format("Partition%3d: %s\n", i, p);
            }
        }
    }
    b.format("\n");
}

void SystemInfo :: hw_modules(StreamTextLog &b, uint16_t bits, const char *msg)
{
    const char *hw[] = { "ACIA 6551 at $DE00", "ACIA 6551 at $DF00", "RAM Expansion Unit", "16MB REU", "Ultimate Command Intf.", "Ultimate Command Intf.",
                         "Ultimate Command Intf.", "Audio Sampler", "Write Mirroring", "Dynamic Bus Optimization", "Kernal Replacement" };

    if (bits) {
        int indent = 0;
        b.format("%14s", msg);
        int index = 0;
        while(bits) {
            if (bits & 1) {
                b.format("%#s%s\n", indent, "", hw[index]);
                indent = 14;
            }
            bits >>= 1;
            index ++;
        }
    }
}

void SystemInfo :: cart_info(StreamTextLog &b)
{
    C64 *c64 = C64 :: getMachine();
    const cart_def *cart = c64->get_cart_definition();
    b.format("Cartridge:    %s\n", cart->name);
    b.format("Filename:     %s\n", ((cart_def *)cart)->filename.c_str());
    if (cart->type == CART_TYPE_GMOD2) {
        if (getFpgaCapabilities() & CAPAB_EEPROM) {
            b.format("EEPROM:       %s\n", c64->get_eeprom_dirty() ? "Dirty (Needs save!)" : "Clean");
        } else {
            b.format("EEPROM:       Not Supported");
        }
    }

    hw_modules(b, cart->disabled, "Incompatible:");
    uint16_t enabled = 0;
    if (C64_REU_ENABLE) enabled |= CART_REU;
    if (CMD_IF_SLOT_ENABLE) enabled |= CART_UCI;
    if (C64_SAMPLER_ENABLE) enabled |= CART_SAMPLER;

#if U64 // FIXME
    if (false) enabled |= CART_WMIRROR;
    if (false) enabled |= CART_DYNAMIC;
#endif
    if (C64_KERNAL_ENABLE) enabled |= CART_KERNAL;

    if (getFpgaCapabilities() & CAPAB_ACIA) {
        uint8_t acia_en, acia_nmi;
        uint16_t acia_addr;
        acia.GetHwMapping(acia_en, acia_addr, acia_nmi);
        if (acia_en && !(acia_addr & 0x100)) enabled |= CART_ACIA_DE;
        if (acia_en && (acia_addr & 0x100)) enabled |= CART_ACIA_DF;
    }

    hw_modules(b, enabled, "Now Enabled:");

    b.format("\n");
}

char *SystemInfo :: size_expression(uint64_t a, uint64_t b, char *buf)
{
    const char *scales[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    uint64_t bytes = a * b;
    int scale = 0;
    while(bytes >= 10000) {
        bytes /= 1024;
        scale++;
    }
    sprintf(buf, "%4d %s", (uint32_t)bytes, scales[scale]);
    return buf;
}

void SystemInfo :: storage_info(StreamTextLog& b)
{
    FileManager *fm = FileManager :: getFileManager();
    IndexedList<FileInfo *> dir(8, NULL);
    Path path; // defaults to Root directory
    fm->get_directory(&path, dir, NULL);
    char buffer[16];

    for(int i=0; i<dir.get_elements(); i++) {
        FileInfo *inf = dir[i];
        path.cd("/");
        path.cd(inf->lfname);
        uint32_t free_clusters, cluster_size;
        FRESULT fres = fm->get_free(&path, free_clusters, cluster_size);
        if (fres == FR_OK) {
            b.format("%14s%s Free\n", inf->lfname, size_expression(free_clusters, cluster_size, buffer));
        } else {
            b.format("%14s%s\n", FileSystem :: get_error_string(fres));
        }
    }
}

void SystemInfo :: generate(UserInterface *ui)
{
    StreamTextLog buffer(4096);

    buffer.format("Drive Status:\n");
    buffer.format("=============\n");
    if (c1541_A) {
        drive_info(buffer, c1541_A, 'A');
    }
    if (c1541_B) {
        drive_info(buffer, c1541_B, 'B');
    }
    iec_info(buffer);

    buffer.format("Cartridge Information:\n");
    buffer.format("======================\n");
    cart_info(buffer);

    buffer.format("Storage Devices:\n");
    buffer.format("================\n");
    storage_info(buffer);

    ui->run_editor(buffer.getText(), buffer.getLength());
}

