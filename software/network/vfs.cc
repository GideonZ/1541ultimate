#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vfs.h"

#include "filemanager.h"

#define dbg_printf(...)
// #define dbg_printf(...)  printf(__VA_ARGS__)

void  vfs_load_plugin()
{
}

vfs_t *vfs_openfs()
{
    vfs_t *vfs = new vfs_t;
    vfs->path = FileManager :: getFileManager() -> get_new_path("vfs");
    vfs->last_direntry = NULL;
    vfs->last_dir = NULL;
    vfs->open_file = NULL;
    return vfs;
}

void vfs_closefs(vfs_t *vfs)
{
    Path *p = (Path *)vfs->path;
    FileManager :: getFileManager() -> release_path(p);
    delete vfs;
}

vfs_file_t *vfs_open(vfs_t *fs, const char *name, const char *flags)
{
    dbg_printf("Open file: '%s' flags: '%s'\n", name, flags);
    if (fs->open_file) {
        dbg_printf("There is already a file open.\n");
        return NULL;
    }
    Path *path = (Path *)fs->path;
    uint8_t bfl = FA_READ;
    if(flags[0] == 'w')
        bfl = FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS;
        
    File *file = 0;
    FRESULT fres = FileManager :: getFileManager() -> fopen(path, (char *)name, bfl, &file);
    if (!file)
        return NULL;

    vfs_file_t *vfs_file = new vfs_file_t;
    vfs_file->file = file;
    vfs_file->eof = 0;
        
    // link this fs to file
    vfs_file->parent_fs = fs;
    
    // store open file (only one for now in fs)
    fs->open_file = vfs_file;

    return vfs_file;    
}

void vfs_close(vfs_file_t *file)
{
    FileManager :: getFileManager() -> fclose((File *)file->file);
    dbg_printf("File closed. clearing open file link.\n");
    file->parent_fs->open_file = NULL;    
}

int  vfs_read(void *buffer, int chunks, int chunk_len, vfs_file_t *file)
{
    File *f = (File *)file->file;
    uint32_t trans = 0;
    uint32_t len = chunks*chunk_len;
    dbg_printf("R(%d,%d)",chunks,chunk_len);
    if(f->read(buffer, len, &trans) != FR_OK)
        return -1;
    
    if (trans < len) {
        file->eof = 1;
        dbg_printf("[EOF]");
    }        
    return trans;
}

int  vfs_write(const void *buffer, int chunks, int chunk_len, vfs_file_t *file)
{
    File *f = (File *)file->file;
    uint32_t trans = 0;
    uint32_t len = chunks*chunk_len;
    if(f->write(buffer, len, &trans) != FR_OK)
        return -1;
    
    return trans;
}

int  vfs_eof(vfs_file_t *file)
{
    dbg_printf("(EOF");
    dbg_printf("%d)", file->eof);
    return file->eof;
}

vfs_dir_t *vfs_opendir(vfs_t *fs, const char *name)
{
    Path *path = (Path *)fs->path;
    dbg_printf("OpenDIR: fs = %p, name arg = '%s'\n", fs, name);

    // create objects
    vfs_dir_t *dir = (vfs_dir_t *)new vfs_dir_t;
    vfs_dirent_t *ent = (vfs_dirent_t *)new vfs_dirent_t;

    // fill in wrapper pointer for directory
    IndexedList<FileInfo *> *listOfEntries = new IndexedList<FileInfo *>(16, NULL);
    dir->entries = listOfEntries;
    dir->index = 0;
    dir->entry = ent;
    dir->parent_fs = fs;
    fs->last_direntry = NULL;
    fs->last_dir = dir;

    path->get_directory(*listOfEntries);
    
    // clear wrapper for file info
    ent->file_info = NULL;
    return dir;
}

void vfs_closedir(vfs_dir_t *dir)
{
    dbg_printf("CloseDIR (%p)\n", dir);
    if(dir) {
        dir->parent_fs->last_direntry = NULL;
        dir->parent_fs->last_dir = NULL;
        if (dir->entries) {
        	IndexedList<FileInfo *> *listOfEntries = (IndexedList<FileInfo *> *)(dir->entries);
        	for(int i=0;i<listOfEntries->get_elements();i++) {
        		delete (*listOfEntries)[i];
        	}
        	delete listOfEntries;
        }
        delete dir;
    }
}

vfs_dirent_t *vfs_readdir(vfs_dir_t *dir)
{
    dbg_printf("READDIR: %p %d\n", dir, dir->index);
	IndexedList<FileInfo *> *listOfEntries = (IndexedList<FileInfo *> *)(dir->entries);

    if(dir->index < listOfEntries->get_elements()) {
        FileInfo *inf = (*listOfEntries)[dir->index];
        dir->entry->file_info = inf;
        dir->entry->name = inf->lfname;
        dir->parent_fs->last_direntry = dir->entry;
        dbg_printf("Read: %s\n", dir->entry->name);
        dir->index++;
        return dir->entry;
    }
    return NULL;
}

int  vfs_stat(vfs_t *fs, const char *name, vfs_stat_t *st)
{
    dbg_printf("STAT: VFS=%p. %s -> %p\n", fs, name, st);
    FileInfo *inf = NULL;
    if(fs->last_direntry) {
        inf = (FileInfo *)fs->last_direntry->file_info;
        dbg_printf("Last inf: %s\n", inf->lfname);
        if(strcmp(inf->lfname, name) != 0) {
            inf = NULL;
        }
    }
    FileInfo localInfo(32);
    if(!inf) {
    	if((FileManager :: getFileManager() -> fstat((Path *)fs->path, name, localInfo)) == FR_OK)
    		inf = &localInfo;
    }
    if(!inf)
        return -1;        
    
    st->year  = (inf->date >> 9) + 1980;
    st->month = (inf->date >> 5) & 0x0F;
    st->day   = (inf->date) & 0x1F;
    st->hr    = (inf->time >> 11);
    st->min   = (inf->time >> 5) & 0x3F;
    st->sec   = ((inf->time) & 0x1F) << 1;
    st->st_size = inf->size;
    st->st_mode = (inf->attrib & AM_DIR)?1:2;

    // make sure that the date makes sense, otherwise the FTP client will get confused
    if (st->month > 12)
    	st->month = 12;
    if (st->month < 1)
    	st->month = 1;
    if (st->day < 1)
    	st->day = 1;
    if (st->min > 59)
    	st->min = 59;
    if (st->hr > 23)
    	st->hr = 23;
    // > 31 is not possible
    return 0;
}

int  vfs_chdir(vfs_t *fs, const char *name)
{
    Path *p = (Path *)fs->path;
    dbg_printf("CD: VFS=%p -> %s\n", fs, name);
    if(! p->cd((char*)name))
        return -1;
    return 0;
}

char *vfs_getcwd(vfs_t *fs, void *args, int dummy)
{
    Path *p = (Path *)fs->path;
    const char *full_path = p->get_path();
    // now copy the string to output
    char *retval = (char *)malloc(strlen(full_path)+1);
    strcpy(retval, full_path);
    dbg_printf("CWD: %s\n", retval);
    return retval;
}

int  vfs_rename(vfs_t *fs, const char *old_name, const char *new_name)
{
	printf("Rename from %s to %s.\n", old_name, new_name);
    Path *p = (Path *)fs->path;
	FRESULT fres = FileManager :: getFileManager() -> rename(p, old_name, new_name);
	return (fres == FR_OK) ? 0 : -1;
}

int  vfs_mkdir(vfs_t *fs, const char *name, int flags)
{
	printf("Create dir: %s.\n", name);
    Path *p = (Path *)fs->path;
	FRESULT fres = FileManager :: getFileManager() -> create_dir(p, name);
	return (fres == FR_OK) ? 0 : -1;
}

int  vfs_rmdir(vfs_t *fs, const char *name)
{
	printf("RmDir: %s.\n", name);
    Path *p = (Path *)fs->path;
	FRESULT fres = FileManager :: getFileManager() -> delete_file(p, name);
	return (fres == FR_OK) ? 0 : -1;
}

int  vfs_remove(vfs_t *fs, const char *name)
{
	printf("Delete: %s.\n", name);
    Path *p = (Path *)fs->path;
	FRESULT fres = FileManager :: getFileManager() -> delete_file(p, name);
	return (fres == FR_OK) ? 0 : -1;
}
