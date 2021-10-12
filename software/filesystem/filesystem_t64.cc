/*
 * t64_filesystem.cc
 *
 *  Created on: May 25, 2015
 *      Author: Gideon
 */

#include "filesystem_t64.h"
#include "pattern.h"

/*************************************************************/
/* T64 File System implementation                            */
/*************************************************************/

FileSystemT64 :: FileSystemT64(File *f) : FileSystem(0)
{
	t64_file = f;
}

FileSystemT64 :: ~FileSystemT64()
{

}

// Get number of free sectors on the file system
FRESULT FileSystemT64 :: get_free (uint32_t* free, uint32_t *cs)
{
	*free = 0;
	*cs = 1;
    return FR_OK;
}

// Clean-up cached data
FRESULT FileSystemT64 :: sync(void)
{
    return t64_file->sync();
}

FRESULT FileSystemT64 :: dir_open(const char *path, Directory **dir) // Opens directory (creates dir object, NULL = root)
{
	// no info.. The only directory that we can open here by name is '/'
    if (strlen(path) > 1) {
        return FR_NO_PATH;
    }
    if ((path[0] != '/') && (path[0] != '\\')) {
        return FR_NO_PATH;
    }

	*dir = new DirectoryT64(this); // create a dir object with index 0
	return FR_OK;
}

// reads next entry from dir
FRESULT DirectoryT64 :: get_entry(FileInfo &f)
{
	uint8_t read_buf[32], c;
	uint8_t *p;
	FRESULT fres;
	uint32_t bytes_read;

	// Fields that are always the same, or needs initialization.
    f.fs      = fs;
	f.date    = 0;
	f.time    = 0;
    f.extension[0] = '\0';

	if(idx == 0) {
	    fres = t64_file->seek(0); // rewind
		if(fres != FR_OK)
			return fres;

	    fres = t64_file->read(read_buf, 32, &bytes_read);
		if(fres != FR_OK)
			return fres;

	    if((bytes_read != 32) || (read_buf[0] != 0x43) || (read_buf[1] != 0x36) || (read_buf[2] != 0x34)) {
	        printf("Not a valid T64 file. %d %s\n", bytes_read, read_buf);
	        return FR_NO_FILESYSTEM;
	    }

	    // read name of directory as first entry
	    fres = t64_file->read(read_buf, 32, &bytes_read);
		if(fres != FR_OK)
			return fres;

		read_buf[31] = 0;
		f.lfname[24] = 0;
	    for(int b=0;b<24;b++) {
	    	char c = char(read_buf[b+8]);
	    	f.lfname[b] = c; //(c == '/') ? '!' : c;
	    }

	    max  = LD_WORD(&read_buf[2]);
	    used = LD_WORD(&read_buf[4]);
		if(!used)
			used = 1; // fix
	    f.size = 0L;
	    f.cluster = 0L;
	    f.attrib  = AM_VOL;
	} else {
		//printf("Idx = %d, Used = %d\n", idx, used);
		if(idx <= used) {
	        fres = t64_file->read(read_buf, 32, &bytes_read);
			if(fres != FR_OK)
				return fres;

			int v=0;
			//dump_hex(read_buf, 32);
	        if(read_buf[0]) {
	            memset(f.lfname, 0, f.lfsize);
	            for(int s=0;s<16;s++) {
	                c = read_buf[16+s];
	                if (c == '/') {
	                	c = '!';
	                } else if (c & 0x80) {
	                	c = ' ';
	                }
					f.lfname[s] = c;
					v++;
	            }

				// eui: no trailing spaces please
				for(int s=v-1; s>=0; s--) {
					p = (uint8_t *)&f.lfname[s];
	                if((*p == ' ') || (*p == 0xA0) || (*p == 0xE0)) {
						*p = 0;
	                }
					else {
						if(*p != 0)
							break;
					}
				}

	            if (v) {
	                strt = LD_WORD(&read_buf[2]);
	                stop = LD_WORD(&read_buf[4]);
	                f.time = strt;  // patch to store start address ###
	                f.cluster = LD_DWORD(&read_buf[8]); // file offset :)
	                f.attrib = 0;
	                strncpy(f.extension, "PRG", 4);
	                f.size = (stop != 0)?(stop - strt):(65536 - strt);
                    f.size += 2; // the file is actually two longer than the start-stop
	                printf("%s: %04x-%04x (Size: %d)\n", f.lfname, strt, stop, f.size);
	            } else {
	            	strcpy(f.lfname, "- Invalid name -");
	                f.cluster = 0;
	            }
	        }
		} else { // no more
			return FR_NO_FILE;
		}
	}
    idx++;
	return FR_OK;
}

// functions for reading and writing files
// Opens file (creates file object)
FRESULT FileSystemT64 :: file_open(const char *filename, uint8_t flags, File **file)  // Opens file (creates file object)
{
    FileInfo *info = NULL;
    PathInfo pi(this);
    pi.init(filename);
    PathStatus_t pres = walk_path(pi);
    if (pres == e_EntryFound) {
        info = pi.getLastInfo();
    } else if (pres == e_DirNotFound) {
        return FR_NO_PATH;
    } else {
        return FR_NO_FILE;
    }
    if (info->attrib & AM_VOL) {
        return FR_NO_FILE;
    }

	FileInT64 *ff = new FileInT64(this);
	*file = ff;

	FRESULT res = ff->open(info, flags);
	if(res == FR_OK) {
		return res;
	}
	delete ff;
	*file = NULL;
	return res;
}

/*************************************************************/
/* T64 File System implementation                            */
/*************************************************************/
FileInT64 :: FileInT64(FileSystemT64 *f) : File(f)
{
    file_offset = 0;
	offset = 0;
	length = 0;
	start_addr = 0;
    fs = f;
}

FileInT64 :: ~FileInT64()
{
}

FRESULT FileInT64 :: open(FileInfo *info, uint8_t flags)
{
	if(info->fs != fs)
		return FR_INVALID_OBJECT;

	file_offset = info->cluster;
	offset = 0;
	length = info->size;
	start_addr = info->time; // hack

	return fs->t64_file->seek(file_offset);
}

FRESULT FileInT64 :: close(void)
{
    FRESULT fres = fs->sync();
    delete this;
    return fres;
}

FRESULT FileInT64 :: read(void *buffer, uint32_t len, uint32_t *transferred)
{
    FRESULT res;

    uint8_t *dst = (uint8_t *)buffer;
    uint32_t bytes_read;
    *transferred = 0;

    if(!len)
        return FR_OK;

	// pre-pend the start address, as for any other PRG.
	if(offset == 0) {
		*(dst++) = uint8_t(start_addr); // LSB
		len--;
		offset ++;
	    ++(*transferred);
	}
    if(!len)
        return FR_OK;

	if(offset == 1) {
		*(dst++) = uint8_t(start_addr >> 8); // MSB
		len--;
		offset ++;
	    ++(*transferred);
	}
    if(!len)
        return FR_OK;

    if(len > (length - offset)) {
        len = length - offset;
    }

	res = fs->t64_file->read(dst, len, &bytes_read);
	*transferred += bytes_read;
	offset += bytes_read;
	return res;
}

FRESULT  FileInT64 :: write(const void *buffer, uint32_t len, uint32_t *transferred)
{
	return FR_WRITE_PROTECTED;
}

FRESULT FileInT64 :: seek(uint32_t pos)
{
	return FR_DENIED;
}

uint32_t FileInT64 :: get_size()
{
    return length;
}

uint32_t FileInT64 :: get_inode()
{
    return offset;
}
