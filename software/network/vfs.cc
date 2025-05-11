#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vfs.h"

#include "filemanager.h"

#define dbg_printf(...)
//#define dbg_printf(...)  printf(__VA_ARGS__)

void  vfs_load_plugin()
{
}

vfs_t *vfs_openfs()
{
    vfs_t *vfs = new vfs_t;
    vfs->path = new Path();
    vfs->open_file = NULL;
    return vfs;
}

void vfs_closefs(vfs_t *vfs)
{
    Path *p = (Path *)vfs->path;
    delete p;
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
        bfl = FA_WRITE | FA_CREATE_ALWAYS;
        
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
    if(FileManager::read(f, buffer, len, &trans) != FR_OK)
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
    FRESULT fres = FileManager::write(f, (void *)buffer, len, &trans);
    if(fres != FR_OK)
        return -1;
    
    return trans;
}

int  vfs_eof(vfs_file_t *file)
{
    dbg_printf("(EOF");
    dbg_printf("%d)", file->eof);
    return file->eof;
}

vfs_dir_t *vfs_opendir(vfs_t *fs, const char *name, into_mode_t into_mode)
{
    Path *path = (Path *)fs->path;
    dbg_printf("OpenDIR: fs = %p, name arg = '%s' Into = %d\n", fs, name, into_mode);

    mstring matchPattern;
    Path *temp = new Path();
    temp->cd(path->get_path());
    temp->cd(name);
    if (!FileManager::is_path_valid(temp)) { // Is it not a directory?
        // Maybe it's a file, so we 'cd' one step up and store the last part of the path as a pattern
        temp->up(&matchPattern);
        dbg_printf("Stripped = '%s'\n", matchPattern.c_str());
        if (! FileManager::is_path_valid(temp)) { // Is the parent not a directory? then we don't know.
            delete temp;
            return NULL;
        }
    }

    // create objects
    vfs_dir_t *dir = (vfs_dir_t *)new vfs_dir_t;
    vfs_dirent_t *ent = (vfs_dirent_t *)new vfs_dirent_t;

    // fill in wrapper pointer for directory
    IndexedList<FileInfo *> *listOfEntries = new IndexedList<FileInfo *>(16, NULL);
    dir->entries = listOfEntries;
    dir->index = 0;
    dir->entry = ent;
    dir->parent_fs = fs;
    dir->into_mode = into_mode;
    dir->do_alternative = false;

    get_directory(temp, *listOfEntries, matchPattern.c_str());

    delete temp; // delete the temporary path object

    // clear wrapper for file info
    ent->file_info = NULL;
    return dir;
}

void vfs_closedir(vfs_dir_t *dir)
{
    dbg_printf("CloseDIR (%p)\n", dir);
    if(dir) {
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

static bool vfs_has_into_extension(const char *ext)
{
    if (strncasecmp(ext, "D64", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "D71", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "D81", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "DNP", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "FAT", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "ISO", 3) == 0) {
        return true;
    }
    if (strncasecmp(ext, "T64", 3) == 0) {
        return true;
    }
    return false;
}

vfs_dirent_t *vfs_readdir(vfs_dir_t *dir)
{
    dbg_printf("READDIR: %p %d\n", dir, dir->index);
	IndexedList<FileInfo *> *listOfEntries = (IndexedList<FileInfo *> *)(dir->entries);

    while(dir->index < listOfEntries->get_elements()) {
        FileInfo *inf = (*listOfEntries)[dir->index];
        dir->entry->file_info = inf;
        dbg_printf("Read: %s\n", inf->lfname);
        if (inf->attrib & AM_VOL) {
            dir->index++;
            continue;
        }
        switch (dir->into_mode) {
        case e_vfs_files_only:
            break;
        case e_vfs_dirs_only:
            if (vfs_has_into_extension(inf->extension)) {
                inf->attrib |= AM_DIR;
            }
            break;
        case e_vfs_double_listed:
            if (vfs_has_into_extension(inf->extension)) {
                if (dir->do_alternative) {
                    dir->do_alternative = false;
                    inf->attrib |= AM_DIR;
                } else {
                    dir->do_alternative = true;
                    dir->index --; // list normally now, list alternative next time
                    // compensate for the ++ later on
                }
            }
            break;
        }
        dir->index++;
        return dir->entry;
    }
    return NULL;
}

static int vfs_stat_impl(FileInfo *inf, vfs_stat_t *st)
{
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

    inf->generate_fat_name(st->name, 64);

    // > 31 is not possible
    return 0;
}

int  vfs_stat_dirent(vfs_dirent_t *ent, vfs_stat_t *st)
{
    FileInfo *inf = (FileInfo *)ent->file_info;
    return vfs_stat_impl(inf, st);
}

int  vfs_stat(vfs_t *fs, const char *name, vfs_stat_t *st)
{
    dbg_printf("STAT: VFS=%p. %s -> %p\n", fs, name, st);
    FileInfo localInfo(32);
    if ((FileManager :: getFileManager() -> fstat((Path *)fs->path, name, localInfo, false)) == FR_OK) {
        return vfs_stat_impl(&localInfo, st);
    }
    return -1;
}

int  vfs_chdir(vfs_t *fs, const char *name)
{
//    dbg_printf("CD: %s\n", name);

    Path *temp = new Path();
    Path *p = (Path *)fs->path;

    temp->cd(p->get_path());

    if (!temp->cd((char*)name)) { // Construction of Path is illegal
        delete temp;
        return -1;
    }

    if (!FileManager::is_path_valid(temp)) { // Resulting path doesn't exist on the file system
        delete temp;
        return -2;
    }
    p->cd((char *)name); // This was a valid action for temp, so the same can be done on p
    delete temp;
    return 0;
}

char *vfs_getcwd(vfs_t *fs, void *args, int dummy)
{
    Path *p = (Path *)fs->path;
    const char *full_path = p->get_path();
    // now copy the string to output
    char *retval = (char *)malloc(strlen(full_path)+1);
    strcpy(retval, full_path);
    int n = strlen(retval);
    // snoop off the last slash
    if ((n > 1) && (retval[n-1] == '/'))
        retval[n-1] = 0;
    //dbg_printf("CWD: %s\n", retval);
    return retval;
}

int  vfs_rename(vfs_t *fs, const char *old_name, const char *new_name)
{
	printf("Rename from %s to %s.\n", old_name, new_name);
    Path *pf = new Path((Path *)fs->path);
    Path *pt = new Path((Path *)fs->path);
    pf->cd(old_name);
    pt->cd(new_name);
	FRESULT fres = FileManager :: getFileManager() -> rename(pf->get_path(), pt->get_path());
    delete pf;
    delete pt;
    if (fres != FR_OK) {
        printf("Rename failed: %s\n", FileSystem::get_error_string(fres));
    }
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
