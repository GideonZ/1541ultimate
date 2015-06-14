/*
 * t64_filesystem.cc
 *
 *  Created on: May 25, 2015
 *      Author: Gideon
 */

#include "t64_filesystem.h"

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
FRESULT FileSystemT64 :: get_free (uint32_t* free)
{
	*free = 0;
    return FR_OK;
}

// Clean-up cached data
FRESULT FileSystemT64 :: sync(void)
{
    return t64_file->sync();
}

// Opens directory (creates dir object, NULL = root)
Directory *FileSystemT64 :: dir_open(FileInfo *info)
{
//    if(info) { // can only get root directory, T64 does not allow sub directories
//        return NULL;
//    }
    Directory *dir = new Directory(this, 0); // use handle as index in dir. reset to 0
    return dir;
}


// Closes (and destructs dir object)
void FileSystemT64 :: dir_close(Directory *d)
{
    delete d;
}

// reads next entry from dir
FRESULT FileSystemT64 :: dir_read(Directory *d, FileInfo *f)
{
    uint32_t idx = (uint32_t)d->handle;
	uint8_t read_buf[32], c;
	uint8_t *p;
	FRESULT fres;
	uint32_t bytes_read;

	// Fields that are always the same, or needs initialization.
    f->fs      = this;
	f->date    = 0;
	f->time    = 0;
    f->extension[0] = '\0';

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
		f->lfname[24] = 0;
	    for(int b=0;b<24;b++) {
	        f->lfname[b] = char(read_buf[b+8]); 
	    }

	    max  = LD_WORD(&read_buf[2]);
	    used = LD_WORD(&read_buf[4]);
		if(!used)
			used = 1; // fix
	    f->size = 0L;
	    f->cluster = 0L;
	    f->attrib  = AM_VOL;
	} else {
		//printf("Idx = %d, Used = %d\n", idx, used);
		if(idx <= used) {
	        fres = t64_file->read(read_buf, 32, &bytes_read);
			if(fres != FR_OK)
				return fres;

			int v=0;
			//dump_hex(read_buf, 32);
	        if(read_buf[0]) {
	            memset(f->lfname, 0, f->lfsize);
	            for(int s=0;s<16;s++) {
	                c = read_buf[16+s];
	                if ((c & 0x80)||(c == 32)) { // this is a sufficient conversion
	                    f->lfname[s] = 32;
	                }
					else {
	                    f->lfname[s] = c;
	                    v++;
	                }
	            }

				// eui: no trailing spaces please
				for(int s=v-1; s>=0; s--) {
					p = (uint8_t *)&f->lfname[s];
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
	                printf("%s: %04x-%04x\n", f->lfname, strt, stop);
	                f->time = strt;  // patch to store start address ###
	                f->cluster = LD_DWORD(&read_buf[8]); // file offset :)
	                f->attrib = 0;
	                strncpy(f->extension, "PRG", 4);
	                f->size = (stop)?(stop - strt):(65536 - strt);
                    f->size += 2; // the file is actually two longer than the start-stop
	            } else {
	            	strcpy(f->lfname, "- Invalid name -");
	                f->cluster = 0;
	            }
	        }
		} else { // no more
			return FR_NO_FILE;
		}
	}
    idx++;
    d->handle = (void*)idx;
	return FR_OK;
}

// functions for reading and writing files
// Opens file (creates file object)
File   *FileSystemT64 :: file_open(FileInfo *info, uint8_t flags)
{
	FileInT64 *ff = new FileInT64(this);
	File *f = new File(info, (uint32_t)ff);
	FRESULT res = ff->open(info, flags);
	if(res == FR_OK) {
		return f;
	}
	delete ff;
	delete f;
	return NULL;
}

// Closes file (and destructs file object)
void FileSystemT64::file_close(File *f)
{
    FileInT64 *ff = (FileInT64 *)f->handle;
    ff->close();
    delete ff;
    delete f;
}

FRESULT FileSystemT64::file_read(File *f, void *buffer, uint32_t len, uint32_t *bytes_read)
{
    FileInT64 *ff = (FileInT64 *)f->handle;
    return ff->read(buffer, len, bytes_read);
}

FRESULT FileSystemT64::file_write(File *f, void *buffer, uint32_t len, uint32_t *bytes_written)
{
    FileInT64 *ff = (FileInT64 *)f->handle;
    return ff->write(buffer, len, bytes_written);
}

FRESULT FileSystemT64::file_seek(File *f, uint32_t pos)
{
    FileInT64 *ff = (FileInT64 *)f->handle;
    return ff->seek(pos);
}

/*************************************************************/
/* T64 File System implementation                            */
/*************************************************************/
FileInT64 :: FileInT64(FileSystemT64 *f)
{
	file_offset = 0;
	offset = 0;
	length = 0;

    fs = f;
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
	return fs->sync();
	//flag = 0;
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
	return res;
}

FRESULT  FileInT64 :: write(void *buffer, uint32_t len, uint32_t *transferred)
{
	return FR_DENIED;
}

FRESULT FileInT64 :: seek(uint32_t pos)
{
	return FR_DENIED;
}
