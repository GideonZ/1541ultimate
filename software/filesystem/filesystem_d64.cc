/*
 * d64_filesystem.cc
 *
 *  Created on: May 9, 2015
 *      Author: Gideon
 */

#include "filesystem_d64.h"
#include "pattern.h"

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/

FileSystemD64 :: FileSystemD64(Partition *p) : FileSystem(p)
{
    uint32_t sectors;
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
    	printf("T/S:%d:%d => ", track, sector);
    	if(sector >= 40) {
            result = -1;
        } else {
			result = (track * 40) + sector;
        }
		if (result >= num_sectors) {
            result = -1;
        }
        printf("%d\n", result);
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
    uint8_t k,b,*m;

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
FRESULT FileSystemD64 :: get_free (uint32_t*)
{
    return FR_DENIED; // not yet implemented
}

// Clean-up cached data
FRESULT FileSystemD64 :: sync(void)
{
    return FR_OK;
}

// Opens directory (creates dir object, NULL = root)
FRESULT FileSystemD64 :: dir_open(const char *path, Directory **dir, FileInfo *info)
{
//    if(info) { // can only get root directory, D64 does not allow sub directories
//        return NULL;
//    }

	DirInD64 *dd = new DirInD64(this);
    *dir = new Directory(this, dd);
	FRESULT res = dd->open(info);
	if(res == FR_OK) {
		return FR_OK;
	}
    delete dd;
    delete *dir;
    return res;
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
FRESULT FileSystemD64 :: file_open(const char *path, Directory *dir, const char *filename, uint8_t flags, File **file)
{
	FileInfo info(24);
	do {
		FRESULT fres = dir_read(dir, &info);
		if (fres != FR_OK) {
			dir_close(dir);
			return FR_NO_FILE;
		}
		if (info.attrib & AM_VOL)
			continue;
		if (pattern_match(filename, info.lfname)) {
			break;
		}
	} while(1);

	dir_close(dir);

	FileInD64 *ff = new FileInD64(this);
	*file = new File(this, ff);

	FRESULT res = ff->open(&info, flags);
	if(res == FR_OK) {
		return res;
	}
	delete ff;
	delete *file;
	return res;
}

// Closes file (and destructs file object)
void FileSystemD64::file_close(File *f)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    ff->close();
    delete ff;
    delete f;
}

FRESULT FileSystemD64::file_read(File *f, void *buffer, uint32_t len, uint32_t *bytes_read)
{
	FileInD64 *ff = (FileInD64 *)f->handle;
    return ff->read(buffer, len, bytes_read);
}

FRESULT FileSystemD64::file_write(File *f, void *buffer, uint32_t len, uint32_t *bytes_written)
{
    FileInD64 *ff = (FileInD64 *)f->handle;
    return ff->write(buffer, len, bytes_written);
}

FRESULT FileSystemD64::file_seek(File *f, uint32_t pos)
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
    visited = 0;
    idx = -1;
}

FRESULT DirInD64 :: open(FileInfo *info)
{
    idx = -1;

    visited = new uint8_t[fs->num_sectors];
    for (int i=0; i<fs->num_sectors; i++) {
        visited[i] = 0;
    }

    return FR_OK;
}

FRESULT DirInD64 :: close(void)
{
	if (visited)
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

	if(idx == -1) {
		f->attrib  = AM_VOL;
        f->cluster = fs->get_root_sector();
        f->extension[0] = '\0';

        /* Volume name extraction */
		if(fs->move_window(fs->get_root_sector()) == FR_OK) {
			int offset = (fs->image_mode==2)?4:144;
			for(int i=0;i<24;i++) {
                if(i < f->lfsize)
                    f->lfname[i] = char(fs->sect_buffer[offset+i] & 0x7F);
            }
            if(f->lfsize > 24)
                f->lfname[24] = 0;
        	f->size    = 0;
        	idx        = 0;
        	return FR_OK;
        } else {
            return FR_DISK_ERR;
        }
		printf("D64/71/81 title now read.");
    } else {
        do {
            if((idx & 7)==0) {
                if(idx == 0) {
            	    next_t = (fs->image_mode==2)?40:18;
            	    next_s = (fs->image_mode==2)?3:1;
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

        		if(fs->move_window(abs_sect) != FR_OK) {
                    return FR_DISK_ERR;
                }
            }
            uint8_t *p = &fs->sect_buffer[(idx & 7) << 5]; // 32x from start of sector
            //dump_hex(p, 32);
            if((p[2] & 0x0f) == 0x02) { // PRG
                int j = 0;
                for(int i=5;i<21;i++) {
                	if ((p[i] == 0xA0) || (p[i] < 0x20))
                		break;
                	if(j < f->lfsize)
                		f->lfname[j++] = char(p[i] & 0x7F);
                }
                if(j < f->lfsize)
                    f->lfname[j] = 0;

        	    f->attrib = (p[2] & 0x40)?AM_RDO:0;
                f->cluster = fs->get_abs_sector((int)p[3], (int)p[4]);
                f->size = (int)p[30] + 256*(int)p[31];
                strncpy(f->extension, "PRG", 4);
                idx ++;
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
    visited = 0;
    fs = f;
}

FRESULT FileInD64 :: open(FileInfo *info, uint8_t flags)
{
	if(info->fs != fs)
		return FR_INVALID_OBJECT;

	if(!(fs->get_track_sector(info->cluster, current_track, current_sector)))
		return FR_INT_ERR;

	offset_in_sector = 2;

	num_blocks = info->size;
//	dir_sect = info->dir_sector;
//	dir_entry_offset = info->dir_offset;

    visited = new uint8_t[fs->num_sectors];
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

FRESULT FileInD64 :: read(void *buffer, uint32_t len, uint32_t *transferred)
{

	FRESULT res;

    uint8_t *dst = (uint8_t *)buffer;
    uint8_t *src;

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

FRESULT  FileInD64 :: write(void *buffer, uint32_t len, uint32_t *transferred)
{
    FRESULT res;

    uint8_t *src = (uint8_t *)buffer;
    uint8_t *dst;

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
                fs->sect_buffer[1] = (uint8_t)(offset_in_sector - 1);
        }
    }
    fs->sync();

    // TODO: Update link to file in directory.
    return FR_OK;
}

FRESULT FileInD64 :: seek(uint32_t pos)
{
	return FR_DENIED;
}
