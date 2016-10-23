/*
 * filetype_crt.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 2008-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "filetype_crt.h"
#include "directory.h"
#include "filemanager.h"
#include "c64.h"
#include "userinterface.h"
#include <ctype.h>

// tester instance
FileTypeCRT tester_crt(file_type_factory);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define CRTFILE_RUN            0x2C01
#define CRTFILE_CONFIGURE      0x2C02

struct t_cart {
    WORD hw_type;
    WORD type_select;
    char *cart_name;
};

#define CART_NOT_IMPL  0xFFFF
#define CART_NORMAL    1
#define CART_ACTION    2
#define CART_RETRO     3
#define CART_DOMARK    4
#define CART_OCEAN     5
#define CART_EASYFLASH 6
#define CART_SUPERSNAP 7
#define CART_EPYX      8
#define CART_FINAL3    9
#define CART_SYSTEM3   10
#define CART_COMAL80   11
#define CART_SBASIC    12
#define CART_WESTERMANN 13
#define CART_KCS       14
#define CART_FINAL12   15
#define CART_BBASIC    16

const struct t_cart c_recognized_carts[] = {
    {  0, CART_NORMAL,    "Normal cartridge" },
    {  1, CART_ACTION,    "Action Replay" },
    {  2, CART_KCS,       "KCS Power Cartridge" },
    {  3, CART_FINAL3,    "Final Cartridge III" },
    {  4, CART_SBASIC,    "Simons Basic" },
    {  5, CART_OCEAN,     "Ocean type 1 (256 and 128 Kb)" },
    {  6, CART_NOT_IMPL,  "Expert Cartridge" },
    {  7, CART_NOT_IMPL,  "Fun Play" },
    {  8, CART_NOT_IMPL,  "Super Games" },
    {  9, CART_RETRO,     "Atomic Power" },
    { 10, CART_EPYX,      "Epyx Fastload" },
    { 11, CART_WESTERMANN,"Westermann" },
    { 12, CART_NOT_IMPL,  "Rex" },
    { 13, CART_FINAL12,   "Final Cartridge I" },
    { 14, CART_NOT_IMPL,  "Magic Formel" },
    { 15, CART_SYSTEM3,   "C64 Game System" },
    { 16, CART_NOT_IMPL,  "Warpspeed" },
    { 17, CART_NOT_IMPL,  "Dinamic" },
    { 18, CART_NOT_IMPL,  "Zaxxon" },
    { 19, CART_DOMARK,    "Magic Desk, Domark, HES Australia" },
    { 20, CART_SUPERSNAP, "Super Snapshot 5" },
    { 21, CART_COMAL80,   "COMAL 80" },
    { 32, CART_EASYFLASH, "EasyFlash" },
    { 33, CART_NOT_IMPL,  "EasyFlash X-Bank" },
    { 34, CART_NOT_IMPL,  "Capture" },
    { 35, CART_NOT_IMPL,  "Action Replay 3" },
    { 36, CART_RETRO,     "Retro Replay" },
    { 37, CART_NOT_IMPL,  "MMC64" },
    { 38, CART_NOT_IMPL,   "MMC Replay" },
    { 39, CART_NOT_IMPL,  "IDE64" },
    { 40, CART_NOT_IMPL,  "Super Snapshot V4" },
    { 41, CART_NOT_IMPL,  "IEEE 488" },
    { 42, CART_NOT_IMPL,  "Game Killer" },
    { 43, CART_NOT_IMPL,  "Prophet 64" },
    { 44, CART_NOT_IMPL,  "EXOS" },
    { 45, CART_NOT_IMPL,  "Freeze Frame" },
    { 46, CART_NOT_IMPL,  "Freeze Machine" },
    { 47, CART_NOT_IMPL,  "Snapshot64" },
    { 48, CART_NOT_IMPL,  "Super Explode V5" },
    { 49, CART_NOT_IMPL,  "Magic Voice" },
    { 50, CART_NOT_IMPL,  "Action Replay 2" },
    { 51, CART_NOT_IMPL,  "MACH 5" },
    { 52, CART_NOT_IMPL,  "Diashow Maker" },
    { 53, CART_NOT_IMPL,  "Pagefox" },
    { 54, CART_BBASIC,    "Kingsoft Business Basic" },
    { 55, CART_NOT_IMPL,  "Silver Rock 128" },
    { 56, CART_NOT_IMPL,  "Formel 64" },
    { 57, CART_NOT_IMPL,  "RGCD" },
    { 58, CART_NOT_IMPL,  "RR-Net MK3" },
    { 59, CART_NOT_IMPL,  "Easy Calc" },
    { 60, CART_NOT_IMPL,  "GMod2" },
    { 0xFFFF, 0xFFFF, "" } }; 
    

FileTypeCRT :: FileTypeCRT(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypeCRT :: FileTypeCRT(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating CRT type from info: %s\n", fi->lfname);
}

FileTypeCRT :: ~FileTypeCRT()
{
}

int FileTypeCRT :: fetch_children(void)
{
  return -1;
}

int FileTypeCRT :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Run Cart",  CRTFILE_RUN));

    return 1 + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeCRT :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "CRT")==0)
        return new FileTypeCRT(obj->parent, inf);
    return NULL;
}

__inline static WORD get_word(BYTE *p)
{
    return (WORD(p[0]) << 8) + p[1];
}

__inline static DWORD get_dword(BYTE *p)
{
    return DWORD(get_word(p) << 16) + get_word(p+2);
}

void FileTypeCRT :: execute(int selection)
{
	printf("CRT Select: %4x\n", selection);
	File *file;
	FileInfo *inf;
	
    // second pass, after unfreeze
    if(selection == CRTFILE_CONFIGURE) {
        configure_cart();
        return;
    }

    if(selection != CRTFILE_RUN) {
		FileDirEntry :: execute(selection);
        return;
    }        

    printf("Cartridge Load.. %s FileSize = %d\n", get_name(), info->size);
    file = root.fopen(this, FA_READ);
    DWORD dw;
    FRESULT fres;
    total_read = 0;
    max_bank = 0;
    load_at_a000 = false;
    static char str[64];

    if(file) {
        name = NULL;
        if(check_header(file)) { // pointer is now at 0x20
            dw = get_dword(crt_header + 16);
            printf("Header OK. Now reading chip packets starting from %6x.\n", dw);
            fres = file->seek(dw);
            if(fres != FR_OK)
                goto fail;
            while(read_chip_packet(file))
                ;

        } else {
            if(name) {
                strcpy(str, name);
                strcat(str, ". Not Implemented.");
                user_interface->popup(str, BUTTON_OK);
            } else {
                user_interface->popup("Header of CRT file not correct.", BUTTON_OK);
            }
        }

		push_event(e_unfreeze);
		push_event(e_path_object_exec_cmd, this, CRTFILE_CONFIGURE);

fail:
        root.fclose(file);
    } else {
        printf("Error opening file.\n");
    }
}

bool FileTypeCRT :: check_header(File *f)
{
    UINT bytes_read;
    FRESULT res = f->read(crt_header, 0x20, &bytes_read);
    if(res != FR_OK)
        return false;
    if(strncmp((char*)crt_header, "C64 CARTRIDGE   ", 16))
        return false;
    if(crt_header[0x16])
        return false;

    printf("CRT Hardware type: %b ", crt_header[0x17]);
    int idx = 0;
    while(c_recognized_carts[idx].hw_type != 0xFFFF) {
        if(BYTE(c_recognized_carts[idx].hw_type) == crt_header[0x17]) {
            name = c_recognized_carts[idx].cart_name;
            printf("%s\n", name);
            type_select = c_recognized_carts[idx].type_select;
            if(type_select == CART_NOT_IMPL) {
                printf("Not implemented.\n");
                return false;
            }
	    if(type_select == CART_KCS) {
	        /* Bug in many KCS CRT images, wrong header size */
	        crt_header[0x10]=crt_header[0x11]=crt_header[0x12]=0;
		crt_header[0x13]=0x40;
	    }
            return true;
        }
        idx++;
    }
    printf("Unknown\n");
    return false;
}    

bool FileTypeCRT :: read_chip_packet(File *f)
{
    UINT bytes_read;
    FRESULT res = f->read(chip_header, 0x10, &bytes_read);
    if((res != FR_OK)||(!bytes_read))
        return false;
    if(strncmp((char*)chip_header, "CHIP", 4))
        return false;
    DWORD total_length = get_dword(chip_header + 4);
    WORD bank = get_word(chip_header + 10);
    WORD load = get_word(chip_header + 12);
    WORD size = get_word(chip_header + 14);

    if(bank > 0x3f)
        return false;

    if((type_select == CART_OCEAN)&&(load == 0xA000))
        bank &= 0xEF; // max 128k per region

    if(bank > max_bank)
        max_bank = bank;

    bool split = false;
    if(size > 0x2000)
        if((type_select == CART_OCEAN)||(type_select == CART_EASYFLASH)||(type_select == CART_DOMARK)||(type_select == CART_SYSTEM3))
            split = true;

    DWORD mem_addr = ((DWORD)C64_CARTRIDGE_RAM_BASE) << 16;
    if(type_select == CART_KCS){
        mem_addr += load - 0x8000;
    } else {
    if(split)
        mem_addr += 0x2000 * DWORD(bank);
    else
        mem_addr += DWORD(size) * DWORD(bank);

    if(load == 0xA000) {
        mem_addr += 512 * 1024; // interleaved mode (TODO: make it the same in hardware as well, currently only for EasyFlash)
        load_at_a000 = true;
    }
    printf("Reading chip data for bank %d to $%4x with size $%4x to 0x%8x. %s\n", bank, load, size, mem_addr, (split)?"Split":"Contiguous");
    }

    if(size) {
        if(split) {
            res = f->read((void *)mem_addr, 0x2000, &bytes_read);
            total_read += DWORD(bytes_read);
            if(bytes_read != 0x2000) {
                printf("Just read %4x bytes, expecting 8K\n", bytes_read);
                return false;
            }
            mem_addr += 512 * 1024; // interleaved
            res = f->read((void *)mem_addr, size-0x2000, &bytes_read);
            total_read += DWORD(bytes_read);
        } else {            
            res = f->read((void *)mem_addr, size, &bytes_read);
            total_read += DWORD(bytes_read);
            if(bytes_read != UINT(size)) {
                printf("Just read %4x bytes\n", bytes_read);
                return false;
            }
        }
        return (res == FR_OK);
    }
    return true;
}

/*
    constant c_none         : X"0";
    constant c_8k           : X"1";
    constant c_16k          : X"2";
    constant c_16k_umax     : X"3";
    constant c_fc3          : X"4";
    constant c_ss5          : X"5";
    constant c_retro        : X"6";
    constant c_action       : X"7";
    constant c_system3      : X"8";
    constant c_domark       : X"9";
    constant c_ocean128     : X"A";
    constant c_ocean256     : X"B";
    constant c_easy_flash   : X"C";
    constant c_epyx         : X"E";
    constant c_kcs          : X"10";
    constant c_fc           : X"11";
*/

void FileTypeCRT :: configure_cart(void)
{
    C64_MODE = C64_MODE_RESET;
    C64_REU_ENABLE = 0;
    C64_ETHERNET_ENABLE = 0;

    C64_CARTRIDGE_TYPE = 0;

    DWORD len = total_read;
    if(!len)
        type_select = CART_NOT_IMPL;

    int bits = 21;
    while(len < 0x100001) {
        len <<= 1;
        bits--;
    }

    WORD b = max_bank;
    WORD mask = 0;
    while(b) {
        mask <<= 1;
        mask |= 1;
        b >>= 1;
    }

    printf("Total ROM size read: %6x bytes; %d bits (%6x). Max_Bank = %d. Mask = %d\n", total_read, bits, (1<<bits), max_bank, mask);

    switch(type_select) {
        case CART_NORMAL:
            if ((crt_header[0x18] == 0) && (crt_header[0x19] == 1)) {
                C64_CARTRIDGE_TYPE = 0x03; // Ultimax
            } else if ((crt_header[0x18] == 1) && (crt_header[0x19] == 1)) {
                C64_CARTRIDGE_TYPE = 0x02; // 16K
            } else if ((crt_header[0x18] == 1) && (crt_header[0x19] == 0)) {
                C64_CARTRIDGE_TYPE = 0x01; // 8K
            } else {
                if (total_read > 8192)
                    C64_CARTRIDGE_TYPE = 0x02; // 16K
                else
                    C64_CARTRIDGE_TYPE = 0x01; // 8K
            }
            break;
        case CART_ACTION:
            C64_CARTRIDGE_TYPE = 0x07; // Action
            break;
        case CART_RETRO:
            C64_CARTRIDGE_TYPE = 0x06; // Retro
            break;
        case CART_DOMARK:
            C64_CARTRIDGE_TYPE = 0x09; // Domark
            break;
        case CART_OCEAN:
//            if ((total_read > 0x20000)&&(total_read <= 0x40000)) { // 16K banks
//                C64_CARTRIDGE_TYPE = 0x0B; // Ocean 256K
//                if(!load_at_a000) { // error! There is no data for upper range
//                    printf("Fixing Ocean 256, by copying second 128K to A000 range\n");
//                    DWORD mem_base = ((DWORD)C64_CARTRIDGE_RAM_BASE) << 16;
//                    memcpy((void *)(mem_base + 512*1024), (void *)(mem_base + 128*1024), 128*1024);
//                }
//            } else {
//                C64_CARTRIDGE_TYPE = 0x09; // Ocean 128K/512K
//            }                
//
            if (load_at_a000) {
                C64_CARTRIDGE_TYPE = 0x0B; // Ocean 256K
            } else {
//                DWORD mem_base = ((DWORD)C64_CARTRIDGE_RAM_BASE) << 16;
//                memcpy((void *)(mem_base + 256*1024), (void *)(mem_base + 0*1024), 256*1024);
                C64_CARTRIDGE_TYPE = 0x0A; // Ocean 128K/512K
            }                
            break;
        case CART_EASYFLASH:
            C64_CARTRIDGE_TYPE = 0x0C; // EasyFlash
            break;
        case CART_SUPERSNAP:
            C64_CARTRIDGE_TYPE = 0x05; // Snappy
            break;
        case CART_EPYX:
            C64_CARTRIDGE_TYPE = 0x0E; // Epyx
            break;
        case CART_FINAL3:
            C64_CARTRIDGE_TYPE = 0x04; // Final3
            break;
        case CART_SYSTEM3:
            C64_CARTRIDGE_TYPE = 0x08; // System3
            break;
        case CART_COMAL80:
            C64_CARTRIDGE_TYPE = 0x0F; // Comal 80
            break;
        case CART_SBASIC:
            C64_CARTRIDGE_TYPE = 0x10; // Simons Basic
            break;
        case CART_WESTERMANN:
            C64_CARTRIDGE_TYPE = 0x11; // Westermann
            break;
        case CART_KCS:
            C64_CARTRIDGE_TYPE = 0x13; // KCS Power Cartridge
            break;
        case CART_FINAL12:
            C64_CARTRIDGE_TYPE = 0x14; // Final Cartridge 1 & 2
            break;
        case CART_BBASIC:
            C64_CARTRIDGE_TYPE = 0x15; // Business Basic
            break;
        default:
            break;
    }
    printf("*%b*\n", C64_CARTRIDGE_TYPE);

    C64_MODE = C64_MODE_UNRESET;
}
