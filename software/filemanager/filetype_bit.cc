/*
 * filetype_bit.cc
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
extern "C" {
	#include "itu.h"
}
#include "filetype_bit.h"
#include "filemanager.h"
#include "userinterface.h"
#include <ctype.h>

// tester instance
FileTypeBIT tester_bit(file_type_factory);

// statics
static int readhead(File *f);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define BITFILE_FLASH          0x2D81
#define BITFILE_BOOT           0x2D82

FileTypeBIT :: FileTypeBIT(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    if((ITU_CAPABILITIES & CAPAB_FPGA_TYPE) >> FPGA_TYPE_SHIFT) {
        printf("Bitfile loading only supported on S3-700A.\n");
    } else {
        fac.register_type(this);
    }
    info = NULL;
}

FileTypeBIT :: FileTypeBIT(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating BIT type from info: %s\n", fi->lfname);
}

FileTypeBIT :: ~FileTypeBIT()
{
}

int FileTypeBIT :: fetch_children(void)
{
    return -1;
}

int FileTypeBIT :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Flash",  BITFILE_FLASH));

    return 1 + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeBIT :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "BIT")==0)
        return new FileTypeBIT(obj->parent, inf);
    return NULL;
}

void FileTypeBIT :: execute(int selection)
{
	printf("BIT Select: %4x\n", selection);
	File *file;
	FileInfo *inf;
	
    // second pass, after flash
    if(selection == BITFILE_BOOT) {
        boot();
        return; // shouldn't come here.
    }

    if(selection != BITFILE_FLASH) {
		FileDirEntry :: execute(selection);
        return;
    }        

    printf("Bitfile Load.. %s\n", get_name());
    file = root.fopen(this, FA_READ);
    int length;

    FRESULT res;
    UINT bytes_read;
    t_flash_address addr;
    flash = get_flash();
    flash->get_image_addresses(FLASH_ID_CUSTOMFPGA, &addr);
    
    if(file) {
        length = readhead(file);        

        if((length > 0)&&(length <= addr.max_length)) {
            printf("Valid bitfile. Size = %d. Flash position: %8x\n", length, addr.start);
            BYTE *buffer = new BYTE[length];
            res = file->read(buffer, length, &bytes_read);
            if((res != FR_OK)||(bytes_read != length)) {
                delete[] buffer;
                goto fail;
            }
            flash->protect_disable();
            bool okay = program(FLASH_ID_CUSTOMFPGA, buffer, length, "", get_name());
            flash->protect_enable();
            delete[] buffer;         
            if(okay) {            
        		push_event(e_path_object_exec_cmd, this, BITFILE_BOOT);
            }
        } else {
            user_interface->popup("Invalid FPGA bitfile.", BUTTON_OK);
        }
fail:
        root.fclose(file);
    } else {
        printf("Error opening file.\n");
    }
}

void FileTypeBIT :: boot()
{
    t_flash_address addr;
    flash->get_image_addresses(FLASH_ID_CUSTOMFPGA, &addr);
    printf("Rebooting FPGA from flash address %8x\n", addr.start);
    wait_ms(500);
    flash->reboot(addr.start);
}

bool FileTypeBIT :: program(int id, void *buffer, int length, char *version, char *descr)
{
    // taken from update.cc. Might just need to become a member of the flash baseclass, not of this filetype (TODO)

	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    
    if(image_address.has_header) {
        printf("Flashing  %s, version %s..\n", descr, version);
        BYTE *bin = new BYTE[length+16];
        DWORD *pul;
        pul = (DWORD *)bin;
        *(pul++) = (DWORD)length;
        memset(pul, 0, 12);
        strcpy((char*)pul, version);
        memcpy(bin+16, buffer, length);
        length+=16;
        p = (char *)bin;
    }
    else {
        printf("Flashing  %s..\n", descr);
        p = (char *)buffer;
    }    
    
	bool do_erase = flash->need_erase();
	int sector;
    int last_sector = -1;
    user_interface->show_status("Flashing..", length);
    while(length > 0) {
		if (do_erase) {
			sector = flash->page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
//				printf("Erase %d   \r", sector);
				if(!flash->erase_sector(sector)) {
                    user_interface->hide_status();
			        user_interface->popup("Erasing failed...", BUTTON_CANCEL);
			        return false;
				}
			}
		}
        printf("Page %d  \r", page);
        if(!flash->write_page(page, p)) {
            user_interface->hide_status();
            user_interface->popup("Programming failed...", BUTTON_CANCEL);
            return false;
        }
        page ++;
        p += page_size;
        length -= page_size;
        user_interface->update_status(NULL, page_size);
    }
    printf("            \n");
    user_interface->hide_status();
    return true;
}
    


/* first 13 bytes of a bit file */
static BYTE head13[] = {0, 9, 15, 240, 15, 240, 15, 240, 15, 240, 0, 0, 1};

/* readhead13
 *
 * Read the first 13 bytes of the bit file.  Discards the 13 bytes but
 * verifies that they are correct.
 *
 * Return -1 if an error occurs, 0 otherwise.
 */
static int readhead13 (File *f)
{
	int t;
	BYTE buf[13];
    UINT bytes_read;
    FRESULT res;

	/* read header */
	res = f->read(buf, 13, &bytes_read);
	if ((bytes_read != 13) || (res != FR_OK))
	{
		return -1;
	}
	
	/* verify header is correct */
	t = memcmp(buf, head13, 13);
	if (t)
	{
		return -1;
	}
	
	return 0;
}

/* readsecthead
 *
 * Read the header of a bit file section.  The section letter is placed in
 * section buffer "buf".
 *
 * Return -1 if an error occurs, '0' otherwise.
 */
static int readsecthead(BYTE *buf, File *f)
{
	int t;
    UINT bytes_read;
    FRESULT res;

	/* get section letter */
	res = f->read(buf, 1, &bytes_read);
	if ((bytes_read != 1) || (res != FR_OK))
	{
		return -1;
	}
	return 0;
} 

/* readstring
 *
 * Read a string from the bit file header. The string is placed in "buf".
 *
 * Return -1 if an error occurs, '0' otherwise.
 */
static int readstring(BYTE *buf, int bufsize, File *f)
{
	char lenbuf[2];
    int len, t;
    UINT bytes_read;
    FRESULT res;
    
	/* read length */
	res = f->read(lenbuf, 2, &bytes_read);
	if ((bytes_read != 2) || (res != FR_OK))
	{
		return -1;
	}

	/* convert 2-byte length to an int */
	len = (((int)lenbuf[0]) <<8) | lenbuf[1];
	
    if(len > bufsize) {
        return -1;
    }
    res = f->read(buf, len, &bytes_read);

	if ((bytes_read != len) || (buf[len-1] != 0))
	{
		return -1;
	}
    return 0;
}

/* readlength
 *
 * Read in the bitstream length.  The section letter "e" is discarded
 * and the length is returned.
 *
 * Return -1 if an error occurs, length otherwise.
 */
static int readlength(File *f)
{
	char s = 0;
	BYTE buf[4];
    UINT bytes_read;
    FRESULT res;
	int length;
	int t;
	
	/* get length */
	res = f->read(buf, 4, &bytes_read);
	if ((bytes_read != 4) || (res != FR_OK))
	{
		return -1;
	}
	
	/* convert 4-byte length to an int */
	length = (((int)buf[0]) <<24) | (((int)buf[1]) <<16) 
	         | (((int)buf[2]) <<8) | buf[3];
	
	return length;
}

/* readhead
 * 
 * Read the entire bit file header.  The file pointer will be advanced to
 * point to the beginning of the bitstream, and the length of the bitstream
 * will be returned.
 *
 * Return -1 if an error occurs, length of bitstream otherwise.
 */
static int readhead(File *f)
{
#define BUFSIZE 500

	int t, len;
	BYTE typ;
    BYTE *buffer = new BYTE[BUFSIZE];
    	
	/* get first 13 bytes */
	t = readhead13(f);
	if (t) goto fail;
	
	/* get other fields */
    do {
    	if(readsecthead(&typ, f) == -1)
    	    goto fail;

        switch(typ) {
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x64:
    	        t = readstring(buffer, BUFSIZE, f);
            	if (t) goto fail;
                break;
            case 0x65:
                len = readlength(f);
                break;
            default:
                printf("Unknown header field '%c'.\n", typ);
                goto fail;
        }
        switch(typ) {
            case 0x61:
                printf("Design name:    %s\n", buffer);
                break;
            case 0x62:
                printf("Target device:  xc%s\n", buffer);
                break;
            case 0x63:
                printf("Bitfile date:   %s\n", buffer);
                break;
            case 0x64:
                printf("Bitfile time:   %s\n", buffer);
                break;
            case 0x65:
                printf("Bitfile length: %08x\n", len);
                delete[] buffer;
                return len;
        }
        
    } while(1);
fail:
    delete[] buffer;
    return -1;
}

