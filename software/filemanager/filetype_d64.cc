/*
 * filetype_d64.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *  Copyright (C) 2011 Daniel Kahlin <daniel@kahlin.net>
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

#include "filetype_d64.h"
#include "directory.h"
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1541.h"

extern "C" {
    #include "dump_hex.h"
}

// tester instance
FileTypeD64 tester(file_type_factory);

extern C1541 *c1541_A;
extern C1541 *c1541_B;

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/
#define D64FILE_RUN        0x2101
#define D64FILE_MOUNT      0x2102
#define D64FILE_MOUNT_RO   0x2103
#define D64FILE_MOUNT_UL   0x2104
#define D64FILE_MOUNT_B    0x2111
#define D64FILE_MOUNT_RO_B 0x2112
#define D64FILE_MOUNT_UL_B 0x2113

FileTypeD64 :: FileTypeD64(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypeD64 :: FileTypeD64(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating d64 type from info: %s\n", fi->lfname);
    // we'll create a file-mapped block device and a default
    // partition to attach our file system to
    blk = new BlockDevice_File(this, 256);
    prt = new Partition(blk, 0, (fi->size)>>8, 0);
    fs  = new FileSystemD64(prt);
}

FileTypeD64 :: ~FileTypeD64()
{
	if(info) {
		if(fs) {
			delete fs;
			delete prt;
			delete blk;
			fs = NULL; // invalidate object
		}
	}
}

int FileTypeD64 :: fetch_children()
{
    printf("Fetch d64 children. %p\n", this);
    if(!fs)
        return -1;
        
    cleanup_children();

    // now getting the root directory
    Directory *r = fs->dir_open(NULL);
    FileInfo fi(32);    

    int i=0;        
    while(r->get_entry(fi) == FR_OK) {
        children.append(new FileDirEntry(this, &fi));
        ++i;
    }
    fs->dir_close(r);
    return i;
}

int FileTypeD64 :: fetch_context_items(IndexedList<PathObject *> &list)
{
    int count = 0;
    if(CAPABILITIES & CAPAB_DRIVE_1541_1) {
        list.append(new MenuItem(this, "Run Disk", D64FILE_RUN));
        list.append(new MenuItem(this, "Mount Disk", D64FILE_MOUNT));
        list.append(new MenuItem(this, "Mount Disk Read Only", D64FILE_MOUNT_RO));
        list.append(new MenuItem(this, "Mount Disk Unlinked", D64FILE_MOUNT_UL));
        count += 4;
    }
    
    if(CAPABILITIES & CAPAB_DRIVE_1541_2) {
        list.append(new MenuItem(this, "Mount Disk on Drive B", D64FILE_MOUNT_B));
        list.append(new MenuItem(this, "Mount Disk R/O on Dr.B", D64FILE_MOUNT_RO_B));
        list.append(new MenuItem(this, "Mount Disk Unlnkd Dr.B", D64FILE_MOUNT_UL_B));
        count += 3;
    }

	list.append(new MenuItem(this, "Enter", FILEDIR_ENTERDIR));
    count++;
    
    return count + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeD64 :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "D64")==0)
        return new FileTypeD64(obj->parent, inf);
//    if(strcmp(inf->extension, "D71")==0)
//        return new FileTypeD64(NULL, inf);
//    if(strcmp(inf->extension, "D81")==0)
//        return new FileTypeD64(NULL, inf);
    return NULL;
}

void FileTypeD64 :: execute(int selection)
{
    bool protect;
    File *file;
    BYTE flags;
    t_drive_command *drive_command;
    
	switch(selection) {
	case D64FILE_MOUNT_UL:
	case D64FILE_MOUNT_UL_B:
		protect = false;
		flags = FA_READ;
		break;
	case D64FILE_MOUNT_RO:
	case D64FILE_MOUNT_RO_B:
		protect = true;
		flags = FA_READ;
		break;
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case D64FILE_MOUNT_B:
        protect = (info->attrib & AM_RDO);
        flags = (protect)?FA_READ:(FA_READ | FA_WRITE);
	default:
		break;
	}

	switch(selection) {
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case D64FILE_MOUNT_RO:
	case D64FILE_MOUNT_UL:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            // unfortunately, we need to insert this here, because we have to make sure we still have
            // a user interface available, to ask if a save is needed.
            c1541_A->check_if_save_needed();
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT;
//			push_event(e_mount_drv1, file, protect);
			if(selection == D64FILE_RUN) {
                C64Event::prepare_dma_load(NULL, "*", 1, RUNCODE_MOUNT_LOAD_RUN);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                C64Event::perform_dma_load(NULL, RUNCODE_MOUNT_LOAD_RUN);

            } else {
                push_event(e_unfreeze);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                if(selection != D64FILE_MOUNT) {
                    drive_command = new t_drive_command;
                    drive_command->command = MENU_1541_UNLINK;
                    //				push_event(e_unlink_drv1);
                    push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                }
            }
		} else {
			printf("Error opening file.\n");
		}
		break;
	case D64FILE_MOUNT_B:
	case D64FILE_MOUNT_RO_B:
	case D64FILE_MOUNT_UL_B:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            // unfortunately, we need to insert this here, because we have to make sure we still have
            // a user interface available, to ask if a save is needed.
            c1541_B->check_if_save_needed();
            push_event(e_unfreeze);
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT;
//			push_event(e_mount_drv1, file, protect);
			push_event(e_object_private_cmd, c1541_B, (int)drive_command);

			if(selection != D64FILE_MOUNT_B) {
                drive_command = new t_drive_command;
                drive_command->command = MENU_1541_UNLINK;
//				push_event(e_unlink_drv1);
    			push_event(e_object_private_cmd, c1541_B, (int)drive_command);
			}
		} else {
			printf("Error opening file.\n");
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/

FileSystemD64 :: FileSystemD64(Partition *p) : FileSystem(p)
{
    DWORD sectors;
    image_mode = 0;
    current_sector = -1;
    dirty = 0;

    if(p->ioctl(GET_SECTOR_COUNT, &sectors) == RES_OK) {
        if(sectors >= 1366) // D71
            ++image_mode;
        if(sectors >= 3200) // D81
            ++image_mode;
    }
    num_sectors = sectors;

    bam_dirty = false;
    bam_valid = false;
    if(p->read(bam_buffer, get_root_sector(), 1) == RES_OK) {
        bam_valid = true;
    }
    
}
    
FileSystemD64 :: ~FileSystemD64()
{
}

int FileSystemD64 :: get_abs_sector(int track, int sector)
{
    int result = 0;
    
    if(track <= 0) {
        return -1;
    }

    --track;
    
    if (image_mode > 1) {
        if(sector >= 40) {
            return -1;
        }
        result = (track * 40) + sector;
        if (result >= num_sectors) {
            return -1;
        }
        return result;
    }

    if (image_mode == 1) {
        if(track >= 35) {
            result = 683;
            track -= 35;
        }
    }

    if(track >= 17) {
        result += 17 * 21;
        track -= 17;
    } else {
        if (sector >= 21) {
            return -1;
        }
        return result + (track * 21) + sector;
    }
    if(track >= 7) {
        result += 7 * 19;
        track -= 7;
    } else {
        if (sector >= 19) {
            return -1;
        }
        return result + (track * 19) + sector;
    }
    if(track >= 6) {
        result += 6 * 18;
        track -= 6;
    } else {
        if (sector >= 18) {
            return -1;
        }
        return result + (track * 18) + sector;
    }

    if (sector >= 17) {
        return -1;
    }

    result += (track * 17) + sector;
    if (result >= num_sectors) {
        return -1;
    }
    return result;
}

bool FileSystemD64 :: get_track_sector(int abs, int &track, int &sector)
{
	if(abs < 0 || abs >= num_sectors)
		return false;


	if (image_mode > 1) {
        track = (abs / 40);
        sector = abs - (track * 40);
        track ++;
        return true;
    }
    
    if (image_mode == 1) {
        if (abs >= 683) {
            track = 35;
            abs -= 683;
        } else {
            track = 0;
        }
    }
    
    if(abs >= (17*21)) {
        track += 17;
        abs -= (17*21);
    } else {
        int t = (abs / 21);
        sector = (abs - t*21);
        track += t + 1;
        return true;
    }
    
    if(abs >= (7*19)) {
        track += 7;
        abs -= (7*19);
    } else {
        int t = (abs / 19);
        sector = (abs - t*19);
        track += t + 1;
        return true;
    }        

    if(abs >= (6*18)) {
        track += 6;
        abs -= (6*18);
    } else {
        int t = (abs / 18);
        sector = (abs - t*18);
        track += t + 1;
        return true;
    }        

    int t = (abs / 17);
    sector = (abs - t*17);
    track += t + 1;
    return true;
}

int FileSystemD64 :: get_root_sector(void)
{
    if(image_mode > 1)
        return get_abs_sector(40, 0);

    return get_abs_sector(18, 0);
}

/*
bool FileSystemD64 :: is_free(int abs)
{
    if(image_mode > 1)
        return true; // we don't understand D71/D81 bit allocation maps yet

            
        
}
*/

bool FileSystemD64 :: allocate_sector_on_track(int track, int &sector)
{
    BYTE k,b,*m;

    if(bam_buffer[4*track]) {
        m = &bam_buffer[1 + 4*track];
        for(int i=0;i<21;i++) {
            k = (i >> 3);
            b = i & 7;
            if ((m[k] >> b) & 1) {
                m[k] &= ~(1 << b);
                --bam_buffer[4*track];
                sector = i;
                bam_dirty = true;
                return true;
            }
        }
        // error!! bam indicated free sector, but there is none.
        bam_buffer[4*track] = 0;
        bam_dirty = true;
    }
    return false;
}

bool FileSystemD64 :: get_next_free_sector(int &track, int &sector)
{
    if(image_mode > 1)
        return false; // we don't understand D71/D81 bit allocation maps yet
        
    if(allocate_sector_on_track(track, sector))
        return true;

    for(int i=17;i>=1;i--) {
        if(allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }
    for(int i=19;i<=35;i++) {
        if(allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }
    return false;
}

FRESULT FileSystemD64 :: move_window(int abs)
{
    DRESULT res;
    if (abs < 0) {
        // usually because of a bad link passed to get_abs_sector().
        return FR_INT_ERR;
    }

    if(current_sector != abs) {
    	if(dirty) {
    		res = prt->write(sect_buffer, current_sector, 1);
			if(res != RES_OK)
				return FR_DISK_ERR;
    	}
    	res = prt->read(sect_buffer, abs, 1);
        if(res == RES_OK) {
            current_sector = abs;
        } else {
        	return FR_DISK_ERR;
        }
    }
    return FR_OK;
}



// check if file system is present on this partition
bool FileSystemD64 :: check(Partition *p)
{
    return false;
}

// Initialize file system
bool FileSystemD64 :: init(void)
{
    return true;
}              

// Get number of free sectors on the file system
FRESULT FileSystemD64 :: get_free (DWORD*)
{
    return FR_DENIED; // not yet implemented
}

// Clean-up cached data
FRESULT FileSystemD64 :: sync(void)
{
    return FR_OK;
}              

// Opens directory (creates dir object, NULL = root)
Directory *FileSystemD64 :: dir_open(FileInfo *info)
{
    if(info) { // can only get root directory, D64 does not allow sub directories
        return NULL;
    }
	DirInD64 *dd = new DirInD64(this);
    Directory *dir = new Directory(this, (DWORD)dd);
	FRESULT res = dd->open(info);
	if(res == FR_OK) {
		return dir;
	}
    delete dd;
    delete dir;
    return NULL;
}
    

// Closes (and destructs dir object)
void FileSystemD64 :: dir_close(Directory *d)
{
    DirInD64 *dd = (DirInD64 *)d->handle;
    dd->close();
    delete dd;
    delete d;
}

// reads next entry from dir
FRESULT FileSystemD64 :: dir_read(Directory *d, FileInfo *f)
{
    DirInD64 *dd = (DirInD64 *)d->handle;
    return dd->read(f);
}

// functions for reading and writing files
// Opens file (creates file object)
File   *FileSystemD64 :: file_open(FileInfo *info, BYTE flags)
{
	FileInD64 *ff = new FileInD64(this);
	File *f = new File(this, (DWORD)ff);
	FRESULT res = ff->open(info, flags);
	if(res == FR_OK) {
		return f;
	}
	delete ff;
	delete f;
	return NULL;
}  

// Closes file (and destructs file object)
void FileSystemD64::file_close(File *f)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    ff->close();
    delete ff;
    delete f;
}

FRESULT FileSystemD64::file_read(File *f, void *buffer, DWORD len, UINT *bytes_read)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    return ff->read(buffer, len, bytes_read);
}

FRESULT FileSystemD64::file_write(File *f, void *buffer, DWORD len, UINT *bytes_written)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    return ff->write(buffer, len, bytes_written);
}

FRESULT FileSystemD64::file_seek(File *f, DWORD pos)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    return ff->seek(pos);
}

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/
DirInD64 :: DirInD64(FileSystemD64 *f)
{
    fs = f;
}

FRESULT DirInD64 :: open(FileInfo *info)
{
    idx = 0;

    visited = new BYTE[fs->num_sectors];
    for (int i=0; i<fs->num_sectors; i++) {
        visited[i] = 0;
    }

    return FR_OK;
}

FRESULT DirInD64 :: close(void)
{
    delete [] visited;

    return FR_OK;
}


FRESULT DirInD64 :: read(FileInfo *f)
{
    int next_t, next_s;
    
    // Fields that are always the same.
    f->fs      = fs;
	f->date    = 0;
	f->time    = 0;

	if(idx == 0) {
	    f->attrib  = AM_VOL;
        f->cluster = fs->get_root_sector();
        f->extension[0] = '\0';

        /* Volume name extraction */
        if(fs->prt->read(fs->sect_buffer, fs->get_root_sector(), 1) == RES_OK) {
            for(int i=0;i<24;i++) {
                if(i < f->lfsize)
                    f->lfname[i] = char(fs->sect_buffer[144+i] | 0x80);
            }
            if(f->lfsize > 24)
                f->lfname[24] = 0;
        	f->size    = 0;
        	idx        = 1;
        	return FR_OK;
        } else {
            return FR_DISK_ERR;
        }
    } else {
        --idx; // adjust for header
        do {
            if((idx & 7)==0) {
                if(idx == 0) {
            	    next_t = (fs->image_mode==2)?40:18;
            	    next_s = 1;
                } else {
                    next_t = (int)fs->sect_buffer[0];
                    next_s = (int)fs->sect_buffer[1];
                }
                // printf("- Reading %d %d - \n", next_t, next_s);
                if(!next_t) { // end of list
                    return FR_NO_FILE;
                }
                int abs_sect = fs->get_abs_sector(next_t, next_s);
                if (abs_sect < 0) { // bad chain link
                    return FR_NO_FILE;
                }
                if (visited[abs_sect]) { // cycle detected
                    return FR_NO_FILE;
                }
                visited[abs_sect] = 1;

                if(fs->prt->read(fs->sect_buffer, abs_sect, 1) != RES_OK) {
                    return FR_DISK_ERR;
                }
            }
            BYTE *p = &fs->sect_buffer[(idx & 7) << 5]; // 32x from start of sector
            //dump_hex(p, 32);
            if((p[2] & 0x0f) == 0x02) { // PRG
                int j = 0;
                for(int i=5;i<21;i++) {
                    if(j < f->lfsize)
                        f->lfname[j++] = char(p[i] & 0x7F);
                }
                if(j < f->lfsize)
                    f->lfname[j] = 0;

        	    f->attrib = (p[2] & 0x40)?AM_RDO:0;
                f->cluster = fs->get_abs_sector((int)p[3], (int)p[4]);
                f->size = (int)p[30] + 256*(int)p[31];
                strncpy(f->extension, "PRG", 4);
                idx += 2; // continue after this next time (readjust for header)
                return FR_OK;                
            }
            idx++;
        } while(idx < 256);
        return FR_NO_FILE;
    }
    return FR_INT_ERR;    
}



FileInD64 :: FileInD64(FileSystemD64 *f)
{
    current_track = 0;
    current_sector = 0;
    offset_in_sector = 0;
    num_blocks = 0;
    dir_sect = -1;
    dir_entry_offset = 0;

    fs = f;
}
    
FRESULT FileInD64 :: open(FileInfo *info, BYTE flags)
{
	if(info->fs != fs)
		return FR_INVALID_OBJECT;

	if(!(fs->get_track_sector(info->cluster, current_track, current_sector)))
		return FR_INT_ERR;

	offset_in_sector = 2;

	num_blocks = info->size;
//	dir_sect = info->dir_sector;
//	dir_entry_offset = info->dir_offset;

    visited = new BYTE[fs->num_sectors];
    for (int i=0; i<fs->num_sectors; i++) {
        visited[i] = 0;
    }

    visit(); // mark initial sector

	return FR_OK;
}

FRESULT FileInD64 :: close(void)
{
	fs->sync();
	//flag = 0;
    delete [] visited;

    return FR_OK;
}

FRESULT FileInD64 :: visit(void)
{
    int abs_sect = fs->get_abs_sector(current_track, current_sector);
    if (abs_sect < 0) { // bad chain link
        return FR_INT_ERR;
    }
    if (visited[abs_sect]) { // cycle detected
        return FR_INT_ERR;
    }
    visited[abs_sect] = 1;
    return FR_OK;
}

FRESULT FileInD64 :: read(void *buffer, DWORD len, UINT *transferred)
{
    FRESULT res;
    
    BYTE *dst = (BYTE *)buffer;
    BYTE *src;
    
    int bytes_left;
    int tr;
    
    *transferred = 0;

    if(!len)
        return FR_OK;

    while(len) {
        // make sure the current sector is within view
        res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
        if(res != FR_OK)
            return res;
        src = &(fs->sect_buffer[offset_in_sector]);
        
        // determine the number of bytes left in sector
        if(fs->sect_buffer[0] == 0) { // last sector
            bytes_left = (1 + fs->sect_buffer[1]) - offset_in_sector;
        } else {
            bytes_left = 256 - offset_in_sector;
        }

        // determine number of bytes to transfer now
        if(bytes_left > len)
            tr = len;
        else
            tr = bytes_left;

        // do the actual copy    
        for(int i=0;i<tr;i++) {
            *(dst++) = *(src++);
        }
        len -= tr;
        offset_in_sector += tr;
        *transferred += tr;
        
        // check for end of file
        if((bytes_left == tr) && (fs->sect_buffer[0] == 0))
            return FR_OK;

        // continue
        if(offset_in_sector == 256) { // proceed to the next sector
            current_track = fs->sect_buffer[0];
            current_sector = fs->sect_buffer[1];
            offset_in_sector = 2;
            res = visit();  // mark and check for cyclic link
            if(res != FR_OK) {
                return res;
            }
        }
    }
    return FR_OK;        
}

FRESULT  FileInD64 :: write(void *buffer, DWORD len, UINT *transferred)
{
    FRESULT res;
    
    BYTE *src = (BYTE *)buffer;
    BYTE *dst;
    
    int bytes_left;
    int tr;
    
    *transferred = 0;

    if(!len)
        return FR_OK;

    if(current_track == 0) { // need to allocate the first block
        fs->sync(); // make sure we can use the buffer to play around
        if(!fs->get_next_free_sector(current_track, current_sector))
            return FR_DISK_FULL;
        num_blocks = 1;
        offset_in_sector = 2;
        fs->sect_buffer[0] = 0; // unlink
        fs->sect_buffer[1] = 1; // 0 bytes in this sector
    } else {
        // make sure the current sector is within view
        res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
        if(res != FR_OK)
            return res;
    }
    
    while(len) {
        // check if we are at the end of our current sector
        if(offset_in_sector == 256) {
            if(fs->sect_buffer[0] == 0) { // we don't have any more bytes.. so extend the file
                if(!fs->get_next_free_sector(current_track, current_sector))
                    return FR_DISK_FULL;
                num_blocks += 1;
                fs->sect_buffer[0] = current_track;
                fs->sect_buffer[1] = current_sector;
                fs->dirty = 1;
                fs->sync(); // make sure we can use the buffer to play around
                fs->sect_buffer[0] = 0; // unlink
                offset_in_sector = 2;
            } else { 
                current_track = fs->sect_buffer[0];
                current_sector = fs->sect_buffer[1];
                res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
                if(res != FR_OK)
                    return res;
                offset_in_sector = 2;
            }
        }

        bytes_left = 256 - offset_in_sector; // we can use the whole sector

        // determine number of bytes to transfer now
        if(bytes_left > len)
            tr = len;
        else
            tr = bytes_left;

        // do the actual copy    
        fs->dirty = 1;
        dst = &(fs->sect_buffer[offset_in_sector]);
        for(int i=0;i<tr;i++) {
            *(dst++) = *(src++);
        }
        len -= tr;
        offset_in_sector += tr;
        *transferred += tr;
        
        if(fs->sect_buffer[0] == 0) { // last sector (still)
            if((offset_in_sector-1) > fs->sect_buffer[1]) // we extended the file
                fs->sect_buffer[1] = (BYTE)(offset_in_sector - 1);
        }
    }
    fs->sync();
    
    // TODO: Update link to file in directory.
    return FR_OK;        
}

FRESULT FileInD64 :: seek(DWORD pos)
{
	return FR_DENIED;
}
