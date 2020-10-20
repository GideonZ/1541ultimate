/*
 * d64_filesystem.cc
 *
 *  Created on: May 9, 2015
 *      Author: Gideon
 */

#define SS_DEBUG 1

#include "filesystem_d64.h"
#include "side_sectors.h"
#include "pattern.h"
#include "cbmname.h"
#include "path.h"
#include <ctype.h>

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/

FileSystemCBM::FileSystemCBM(Partition *p, bool writable, const int *lay) :
        FileSystem(p)
{
    uint32_t sectors;
    current_sector = -1;
    dirty = 0;
    this->writable = writable;

    p->ioctl(GET_SECTOR_COUNT, &num_sectors);
    root_dirty = false;
    root_valid = false;
    layout = lay;

    init();
}

FileSystemCBM::~FileSystemCBM()
{
}

bool FileSystemCBM::is_writable()
{
    return writable;
}

void FileSystemCBM::set_volume_name(const char *name, uint8_t *bam_name, const char *dos)
{
    for (int t = 0; t < 27; t++) {
        bam_name[t] = 0xA0;
    }
    bam_name[21] = dos[0];
    bam_name[22] = dos[1];

    char c;
    int b;
    for (int t = 0, b = 0; t < 27; t++) {
        c = name[b++];
        if (!c)
            break;
        c = toupper(c);
        if (c == ',') {
            t = 17;
            continue;
        }
        bam_name[t] = (uint8_t) c;
    }
}


int FileSystemCBM::get_abs_sector(int track, int sector)
{
    int result = sector;
    //printf("T/S %d/%d => ", track, sector);

    if (track <= 0) {
        return -1;
    }

    --track;

    const int *m = layout;
    do {
        if (m[0] < 1) {
            result += (track * m[1]);
            break;
        }
        if (track >= m[0]) { // spans entire region
            result += (m[0] * m[1]);
            track -= m[0];
        }
        else {
            result += (track * m[1]);
            break;
        }
        m += 2;
    } while (1);

    if (sector >= m[1]) {
        return -1;
    }
    if (result >= num_sectors) {
        return -1;
    }
    //printf("%d :", result);
    return result;
}

bool FileSystemCBM::get_track_sector(int abs, int &track, int &sector)
{
    if (abs < 0 || abs >= num_sectors)
        return false;

    track = 1;
    int t;
    const int *m = layout;

    do {
        t = abs / m[1]; // how many could we span?
        if (m[0] < 1) { // last section, special case; all remaining sectors go there
            track += t;
            abs -= t * m[1];
            break;
        }
        if (abs >= (m[0] * m[1])) { // spans entire region
            track += m[0]; // maximize the number of tracks for this region
            abs -= m[0] * m[1];
            m += 2; // next region
        } else {
            track += t;
            abs -= t * m[1];
            break;
        }
    } while (1);

    sector = abs;
    return true;
}

bool FileSystemCBM::modify_allocation_bit(uint8_t *fr, uint8_t *bits, int sector, bool alloc)
{
    int n = (sector >> 3);
    int b = (sector & 7);

    if (!alloc) { // do free
        if (bits[n] & (1 << b)) { // already free?!
            return false;
        }
        bits[n] |= (1 << b); // set bit
        if (fr)
            (*fr)++; // add one free block
    } else { // do alloc
        if (!(bits[n] & (1 << b))) { // already allocated?!
            return false;
        }
        bits[n] &= ~(1 << b); // set bit
        if ((fr) && (*fr))
            (*fr)--; // subtract one free block
    }
    return true;
}

bool FileSystemCBM::get_next_free_sector(int &track, int &sector)
{
    int mt, ms;
    get_track_sector(num_sectors - 1, mt, ms);

    if (track == 0) {
        track = root_track - 1;
    }

    if (allocate_sector_on_track(track, sector)) {
        return true;
    }

    for (int i = root_track - 1; i >= 1; i--) {
        if (allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }
    for (int i = root_track + 1; i <= mt; i++) {
        if (allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }
    return false;
}

FRESULT FileSystemCBM::move_window(int abs)
{
    DRESULT res;
    if (abs < 0) {
        // usually because of a bad link passed to get_abs_sector().
        return FR_INT_ERR;
    }

    if (current_sector != abs) {
        if (dirty) {
            res = prt->write(sect_buffer, current_sector, 1);
            if (res != RES_OK)
                return FR_DISK_ERR;
        }
        res = prt->read(sect_buffer, abs, 1);
        if (res == RES_OK) {
            current_sector = abs;
            dirty = 0;
        }
        else {
            return FR_DISK_ERR;
        }
    }
    return FR_OK;
}

// check if file system is present on this partition
bool FileSystemCBM::check(Partition *p)
{
    return false;
}

// Initialize file system
bool FileSystemCBM::init(void)
{
    return true;
}

// Clean-up cached data
FRESULT FileSystemCBM::sync(void)
{
    if (dirty) {
        DRESULT res = prt->write(sect_buffer, current_sector, 1);
        if (res != RES_OK) {
            return FR_DISK_ERR;
        }
        dirty = 0;
    }
    if (root_dirty) {
        DRESULT res = prt->write(root_buffer, get_abs_sector(root_track, root_sector), 1);
        if (res != RES_OK) {
            return FR_DISK_ERR;
        }
        root_dirty = false;
    }
    return FR_OK;
}

void FileSystemCBM::get_volume_name(uint8_t *sector, char *buffer, int buf_len)
{
    /* Volume name extraction */
    for (int i = 0; i < 24; i++) {
        if (i < buf_len) {
            char c = char(sector[volume_name_offset + i]);
            buffer[i] = c;
        }
    }
}

// Opens directory (creates dir object, NULL = root)
FRESULT FileSystemCBM::dir_open(const char *path, Directory **dir, FileInfo *relativeDir) // Opens directory
{
    DirInCBM *dd;

    if (relativeDir) {
        dd = new DirInCBM(this, relativeDir);
    } else {
        PathInfo pi(this);
        pi.init(path);
        PathStatus_t pres = walk_path(pi);
        if (pres == e_EntryFound) {
            dd = new DirInCBM(this, pi.getLastInfo());
        } else {
            return FR_NO_PATH;
        }
    }

    FRESULT res = dd->open();
    if (res == FR_OK) {
        *dir = dd;
        return FR_OK;
    }
    delete dd;
    return res;
}

// Creates subdirectory
FRESULT FileSystemCBM::dir_create(const char *path)
{
    PathInfo pi(this);
    pi.init(path);
    PathStatus_t ps = walk_path(pi);
    if (ps == e_DirNotFound) {
        return FR_NO_PATH;
    }
    if (ps == e_EntryFound) {
        return FR_EXIST;
    }
    const char *nameToCreate = pi.getFileName();
    FileInfo *parent = pi.getLastInfo();

    //printf("I should create a directory of name '%s' into the subdirectory '%s' on cluster %d\n", nameToCreate, parent->lfname, parent->cluster);

    FRESULT fres;
    DirInCBM cbmdir(this, parent);

    // Create a directory entry in the parent
    fres = cbmdir.create(nameToCreate, true);
    if (fres != FR_OK) {
        return fres;
    }

    // Now create the directory itself, which is basically a file

    int dir_t = cbmdir.curr_t;
    int dir_s = cbmdir.curr_s;
    int dir_idx = cbmdir.idx % 8;
    FileInCBM *ff = new FileInCBM(this, cbmdir.get_pointer(), dir_t, dir_s, dir_idx);

    uint32_t tr;
    fres = ff->open(FA_CREATE_NEW);
    if (fres != FR_OK) {
        delete ff;
        return fres;
    }
    uint8_t *blk = new uint8_t[254];
    memset(blk, 0, 32);

    // Create directory header block

    blk[0] = 0x48;
    get_volume_name(root_buffer, (char *)(blk+2), 27); // get stuff like dos & disk ID
    fat_to_petscii(nameToCreate, false, (char *)(blk + 2), 27, false); // overwrite with dir name
    fres = ff->write(blk, 30, &tr);

    if (fres == FR_OK) {
        memset(blk, 0, 224);
        // now that we have written the first bytes, the allocated sector is known, we need this for a reference to ourselves
        blk[0] = (uint8_t) ff->current_track;
        blk[1] = (uint8_t) ff->current_sector;
        blk[2] = (uint8_t) cbmdir.header_track;
        blk[3] = (uint8_t) cbmdir.header_sector;
        blk[4] = (uint8_t) dir_t;
        blk[5] = (uint8_t) dir_s;
        blk[6] = (uint8_t) dir_idx;
        // Write out remainder of directory Header block
        fres = ff->write(blk, 254 - 30, &tr);
    }

    if (fres == FR_OK) {
        // Write out empty directory (bunch of zeros)
        memset(blk, 0, 254);
        fres = ff->write(blk, 254, &tr);
    }
    delete[] blk;

    fres = ff->close();
    delete ff;
    return fres;
}

FRESULT FileSystemCBM::find_file(const char *filename, DirInCBM *dir, FileInfo *info)
{
    // Easy peasy, for single directories. When we need subdirectories,
    // we simply parse the path using the Path object and search the each element.

    if (filename[0] == '/') {
        filename++;
    }
    CbmFileName cbm(filename);

    dir->open(); // Just root
    FRESULT res = FR_NOT_READY;
    do {
        res = dir->get_entry(*info);
        if (res != FR_OK) {
            break;
        }
        if(info->attrib & AM_VOL) {
            continue;
        }
        if (info->match_to_pattern(cbm)) {
            //printf("Found '%s' -> '%s'!\n", filename, info->lfname);
            break;
        }
    } while (1);

    dir->close();
    return res;
}

FRESULT FileSystemCBM::file_open(const char *filename, uint8_t flags, File **file, FileInfo *relativeDir)
{
    FileInfo info(24);
    DirInCBM *dd;
    mstring fn;
    bool create = false;
    bool clear = false;
    *file = NULL;

    if (!relativeDir) {
        PathInfo pi(this);
        pi.init(filename);
        pi.workPath.up(&fn); // just look for the parent dir

        PathStatus_t pres = walk_path(pi);
        if (pres != e_EntryFound) {
            return FR_NO_PATH;
        }
        dd = new DirInCBM(this, pi.getLastInfo());
    } else {
        dd = new DirInCBM(this, relativeDir);
        fn = filename;
    }

    FRESULT fres = find_file(filename, dd, &info);
    if (fres == FR_NO_FILE) {
        create = (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS));
    } else if (fres == FR_OK) {
        if (flags & FA_CREATE_NEW) {
            return FR_EXIST;
        }
        clear = (flags & FA_CREATE_ALWAYS);
    } else {
        return fres;
    }

    if (create) {
        FRESULT fres = dd->create(fn.c_str(), false);
        if (fres != FR_OK) {
            delete dd;
            return fres;
        }
    }

    int dir_t = dd->curr_t;
    int dir_s = dd->curr_s;
    int dir_idx = dd->idx % 8;
    DirEntryCBM *dir_entry = dd->get_pointer();

    if (clear) {
        DirEntryCBM de_copy = *dir_entry; // make copy
        dir_entry->data_sector = 0;
        dir_entry->data_track = 0;
        dir_entry->aux_sector = 0;
        dir_entry->aux_track = 0;
        dir_entry->record_size = 0;
        dirty = true;

        deallocate_chain(de_copy.data_track, de_copy.data_sector, dd->visited); // file chain
        deallocate_chain(de_copy.aux_track, de_copy.aux_sector, dd->visited); // side sector chain
        sync();

        move_window(get_abs_sector(dir_t, dir_s)); // make *dir entry valid again
    }

    // Okay, finally done. We have a dir entry, either of a new file or of an existing file.
    // If the create_always flag was set, the file content has been deleted, and the pointer
    // was reset, just like when a new entry was created. We can now attach a file object
    // that points to this dir-entry.

    FileInCBM *ff = new FileInCBM(this, dir_entry, dir_t, dir_s, dir_idx);

    delete dd; // after this dir_entry is no longer valid

    *file = new File(this, ff);
    FRESULT res = ff->open(flags);

    if (res == FR_OK) {
        return res;
    }

    delete ff;
    delete *file;
    *file = NULL;
    return res;
}

// Closes file (and destructs file object)
void FileSystemCBM::file_close(File *f)
{
    FileInCBM *ff = (FileInCBM *) f->handle;
    ff->close();
    delete ff;
    delete f;
}

FRESULT FileSystemCBM::file_read(File *f, void *buffer, uint32_t len, uint32_t *bytes_read)
{
    FileInCBM *ff = (FileInCBM *) f->handle;
    return ff->read(buffer, len, bytes_read);
}

FRESULT FileSystemCBM::file_write(File *f, const void *buffer, uint32_t len, uint32_t *bytes_written)
{
    FileInCBM *ff = (FileInCBM *) f->handle;
    return ff->write(buffer, len, bytes_written);
}

FRESULT FileSystemCBM::file_seek(File *f, uint32_t pos)
{
    FileInCBM *ff = (FileInCBM *) f->handle;
    return ff->seek(pos);
}

FRESULT FileSystemCBM::file_rename(const char *old_name, const char *new_name)
{
    FileInfo info(20);
    DirInCBM *dd = new DirInCBM(this);
    FRESULT res = find_file(old_name, dd, &info);

    if (new_name[0] == '/')
        new_name++;
    CbmFileName cbm(new_name);

    if (res == FR_OK) {
        DirEntryCBM *p = dd->get_pointer();
        p->std_fileType = (p->std_fileType & 0xF8) | cbm.getType(); // save upper bits
        memset(p->name, 0xA0, 16);
        memcpy(p->name, cbm.getName(), cbm.getLength());
        dirty = 1;
        sync();
    }

    delete dd;
    return res;
}

FRESULT FileSystemCBM::deallocate_chain(uint8_t track, uint8_t sector, uint8_t *visited)
{
    FRESULT res = FR_OK;
    while (track) {
        int absolute = get_abs_sector(track, sector);
        if (absolute > num_sectors) {
            res = FR_DISK_ERR;
            break;
        }
        if (absolute < 0) {
            res = FR_INT_ERR;
            break;
        }
        if (visited[absolute]) {
            res = FR_LOOP_DETECTED;
            break;
        }
        visited[absolute] = 1;

        res = move_window(absolute);
        if (res == FR_OK) {
            bool ok = set_sector_allocation(track, sector, false);
            track = sect_buffer[0];
            sector = sect_buffer[1];
        }
        else {
            break;
        }
    }
    return res;
}

FRESULT FileSystemCBM::file_delete(const char *path)
{
    PathInfo pi(this);
    pi.init(path);
    PathStatus_t pres = walk_path(pi);
    if (pres == e_DirNotFound) {
        return FR_NO_PATH;
    }
    if (pres != e_EntryFound) {
        return FR_NO_FILE;
    }
    const char *filename = pi.getFileName();

    DirInCBM *dd = new DirInCBM(this, pi.getParentInfo());
    FileInfo info(20);
    FRESULT res = find_file(filename, dd, &info);

    if (res == FR_OK) {
        DirEntryCBM *p = dd->get_pointer();
        p->std_fileType = 0x00;
        dirty = 1;
        // Saving information, since moving the window makes *p invalid!
        int file_track = p->data_track;
        int file_sector = p->data_sector;
        int side_track = p->aux_track;
        int side_sector = p->aux_sector;
        deallocate_chain(file_track, file_sector, dd->visited); // file chain
        deallocate_chain(side_track, side_sector, dd->visited); // side sector chain
        sync();
    }
    delete dd;
    return res;
}

/**************************************************************************************
 * Disk Type Specifics
 **************************************************************************************/
bool FileSystemD64::init(void)
{
    root_track = 18;
    root_sector = 0;
    dir_track = 18;
    dir_sector = 1;
    volume_name_offset = 144;

    if (prt->read(root_buffer, get_abs_sector(root_track, root_sector), 1) == RES_OK) {
        root_valid = true;
    }
    return root_valid;
}

bool FileSystemD71::init(void)
{
    root_track = 18;
    root_sector = 0;
    dir_track = 18;
    dir_sector = 1;
    volume_name_offset = 144;

    if (prt->read(root_buffer, get_abs_sector(root_track, root_sector), 1) == RES_OK) {
        root_valid = true;
    }
    return root_valid;
}

bool FileSystemD81::init(void)
{
    root_track = 40;
    root_sector = 0;
    dir_track = 40;
    dir_sector = 3;
    volume_name_offset = 4;

    if (prt->read(root_buffer, get_abs_sector(root_track, root_sector), 1) == RES_OK) {
        root_valid = true;
    }

    bam_dirty = false;
    if (prt->read(bam_buffer, get_abs_sector(40, 1), 2) == RES_OK) {
        bam_valid = true;
    }
    return root_valid && bam_valid;
}

bool FileSystemDNP::init(void)
{
    root_track = 1;
    root_sector = 1;
    dir_track = -1;
    dir_sector = -1;

    volume_name_offset = 4;

    if (prt->read(root_buffer, get_abs_sector(root_track, root_sector), 1) == RES_OK) {
        root_valid = true;
    }

    bam_dirty = 0;
    if (prt->read(bam_buffer, get_abs_sector(1, 2), 32) == RES_OK) {
        bam_valid = true;
    }

    return root_valid && bam_valid;
}

FRESULT FileSystemD64::format(const char *name)
{
    memset(root_buffer, 0, 256);

    root_buffer[0] = dir_track;
    root_buffer[1] = dir_sector;
    root_buffer[2] = 0x41; // Dos version
    root_buffer[3] = 0x20; // single sided

    set_volume_name(name, root_buffer + 144, "2A");
    root_dirty = true;

    // free all sectors
    for(int i=0; i < num_sectors; i++) {
        int t, s;
        get_track_sector(i, t, s);
        set_sector_allocation(t, s, false);
    }

    // allocate the sectors for bam and first directory block
    set_sector_allocation(root_track, root_sector, true);
    set_sector_allocation(dir_track, dir_sector, true);

    // create empty directory block
    current_sector = get_abs_sector(dir_track, dir_sector);
    memset(sect_buffer, 0, 256);
    sect_buffer[1] = 0xFF;
    dirty = 1;

    return sync(); // write bam and first dir sector
}


FRESULT FileSystemD71::format(const char *name)
{
    memset(root_buffer, 0, 256);
    memset(bam2_buffer, 0, 256);

    root_buffer[0] = dir_track;
    root_buffer[1] = dir_sector;
    root_buffer[2] = 0x41; // Dos version
    root_buffer[3] = 0x80; // double sided

    set_volume_name(name, root_buffer + 144, "2A");

    // free all sectors
    for(int i=0; i < num_sectors; i++) {
        int t, s;
        get_track_sector(i, t, s);
        set_sector_allocation(t, s, false);
    }

    // allocate the two sectors for bam and first directory block
    set_sector_allocation(root_track, root_sector, true);
    set_sector_allocation(root_track, root_sector+1, true);
    set_sector_allocation(dir_track, dir_sector, true);

    // mark sectors as modified
    root_dirty = true;
    bam2_dirty = true;

    // create empty directory block
    current_sector = get_abs_sector(dir_track, dir_sector);
    memset(sect_buffer, 0, 256);
    sect_buffer[1] = 0xFF;
    dirty = 1;

    return sync(); // write bam and first dir sector
}

FRESULT FileSystemD81::format(const char *name)
{
    memset(root_buffer, 0, 256);
    memset(bam_buffer, 0, 512);

    root_buffer[0] = dir_track;
    root_buffer[1] = dir_sector;
    root_buffer[2] = 0x44; // Dos version

    set_volume_name(name, root_buffer + 4, "3D");
    root_dirty = true;

    // free all sectors
    for(int i=0; i < num_sectors; i++) {
        int t, s;
        get_track_sector(i, t, s);
        set_sector_allocation(t, s, false);
    }
    // allocate the three sectors for bam and first directory block
    set_sector_allocation(root_track, root_sector, true);
    set_sector_allocation(root_track, root_sector+1, true);
    set_sector_allocation(root_track, root_sector+2, true);
    set_sector_allocation(dir_track, dir_sector, true);

    // set bam header
    bam_buffer[0] = root_track;
    bam_buffer[1] = root_sector + 2;
    bam_buffer[2] = 0x44; // dos
    bam_buffer[3] = 0xBB;
    bam_buffer[4] = root_buffer[22];
    bam_buffer[5] = root_buffer[23];
    bam_buffer[6] = 0xC0; // verify on
    memcpy(bam_buffer + 256, bam_buffer, 6);
    bam_buffer[256] = 0x00; // end of chain
    bam_buffer[257] = 0xFF; // end of chain

    // mark sectors as modified
    bam_dirty = 1;

    // create empty directory block
    current_sector = get_abs_sector(dir_track, dir_sector);
    memset(sect_buffer, 0, 256);
    sect_buffer[1] = 0xFF;
    dirty = 1;

    return sync(); // write bam and first dir sector
}

FRESULT FileSystemDNP::format(const char *name)
{
    memset(root_buffer, 0, 256);
    memset(bam_buffer, 0, 8192);

    root_buffer[0] = 0x01; // Track to root directory
    root_buffer[1] = 0x22; // Track to root directory
    root_buffer[2] = 0x48; // Dos version 'H'
    set_volume_name(name, root_buffer + 4, "1H");
    root_buffer[32] = root_track; // reference to this dir header block
    root_buffer[33] = root_sector; // reference to this dir header block
    root_dirty = true;

    // set bam header
    bam_buffer[2] = 0x48; // dos
    bam_buffer[3] = 0xB7;
    bam_buffer[4] = root_buffer[22];
    bam_buffer[5] = root_buffer[23];
    bam_buffer[6] = 0xC0; // verify on
    bam_buffer[7] = (uint8_t )((num_sectors + 255) / 256); // num tracks

    int bam_bytes = num_sectors / 8;
    bam_buffer[36] = 0xF8; // in total 32+3 blocks in use after format
    for(int i=5; i < bam_bytes; i++) {
        bam_buffer[32 + i] = 0xFF;
    }
    // In case it's not a multiple of 8
    if (num_sectors & 7) {
        uint8_t bb = (1 << (num_sectors & 7));
        bam_buffer[32 + bam_bytes] = bb - 1;
    }
    // mark sectors as modified
    bam_dirty = 0xFFFFFFFF;

    // create empty directory block
    current_sector = get_abs_sector(1, 34); // fixed location for root dir
    memset(sect_buffer, 0, 256);
    sect_buffer[1] = 0xFF;
    dirty = 1;

    return sync(); // write bam and first dir sector, and the root block
}

bool FileSystemD64::set_sector_allocation(int track, int sector, bool alloc)
{
    uint8_t *m = &root_buffer[4 * track];
    bool success = modify_allocation_bit(m, m+1, sector, alloc);
    root_dirty = success;
    return success;
}

bool FileSystemD71::set_sector_allocation(int track, int sector, bool alloc)
{
    uint8_t *m, *fr;

    if (track > 35) {
        fr = &root_buffer[0xDD + (track - 36)];
        m = &bam2_buffer[3 * (track - 36)];
    }
    else {
        fr = &root_buffer[4 * track];
        m = fr + 1;
    }

    bool success = modify_allocation_bit(fr, m, sector, alloc);
    if (success) {
        root_dirty = true;
        if (track > 35) {
            bam2_dirty = true;
        }
    }
    return success;
}

bool FileSystemD81::set_sector_allocation(int track, int sector, bool alloc)
{
    uint8_t *m, *fr;

    if (track > 40) {
        fr = &bam_buffer[0x110 + 6 * (track - 41)];
    }
    else {
        fr = &bam_buffer[0x10 + 6 * (track - 1)];
    }
    m = fr + 1;

    bool success = modify_allocation_bit(fr, m, sector, alloc);
    bam_dirty = success;
    return success;
}

bool FileSystemDNP::set_sector_allocation(int track, int sector, bool alloc)
{
    int offset = track * 32;
    uint8_t *m = &bam_buffer[offset]; // since track starts with 1, this bumps the header exactly
    bool success = modify_allocation_bit(NULL, m, sector, alloc);
    if (success) {
        bam_dirty |= (1 << (offset >> 8));
    }
    return success;
}

bool FileSystemD64::allocate_sector_on_track(int track, int &sector)
{
    uint8_t k, b, *m;

    if (root_buffer[4 * track]) {
        m = &root_buffer[4 * track];
        for (int i = 0; i < 21; i++) {
            k = (i >> 3);
            b = i & 7;
            if ((m[k + 1] >> b) & 1) {
                m[k + 1] &= ~(1 << b);
                --(*m);
                sector = i;
                root_dirty = true;
                return true;
            }
        }

        // error!! bam indicated free sector, but there is none.
        root_buffer[4 * track] = 0;
        root_dirty = true;
    }
    return false;
}

bool FileSystemD71::allocate_sector_on_track(int track, int &sector)
{
    uint8_t k, b, *m, *fr;

    if (track > 35) {
        fr = &root_buffer[0xDD + (track - 36)];
        m = &bam2_buffer[3 * (track - 36)];
    }
    else {
        fr = &root_buffer[4 * track];
        m = fr + 1;
    }

    if (*fr) {
        for (int i = 0; i < 21; i++) {
            k = (i >> 3);
            b = i & 7;
            if ((m[k] >> b) & 1) {
                m[k] &= ~(1 << b);
                --(*fr);
                sector = i;
                root_dirty = true;
                if (track > 35) {
                    bam2_dirty = true;
                }
                return true;
            }
        }

        // error!! bam indicated free sector, but there is none.
        *fr = 0;
        root_dirty = true;
        if (track > 35) {
            bam2_dirty = true;
        }
    }
    return false;
}

bool FileSystemD81::allocate_sector_on_track(int track, int &sector)
{
    uint8_t k, b, *m, *fr;

    if (track > 40) {
        fr = &bam_buffer[0x110 + 6 * (track - 41)];
    }
    else {
        fr = &bam_buffer[0x10 + 6 * (track - 1)];
    }
    m = fr + 1;

    if (*fr) {
        for (int i = 0; i < 40; i++) {
            k = (i >> 3);
            b = i & 7;
            if ((m[k] >> b) & 1) {
                m[k] &= ~(1 << b);
                --(*fr);
                sector = i;
                bam_dirty = true;
                return true;
            }
        }

        // error!! bam indicated free sector, but there is none.
        *fr = 0;
        bam_dirty = true;
    }
    return false;
}

bool FileSystemD71::get_next_free_sector(int &track, int &sector)
{
    if (track == 0) {
        track = root_track - 1;
    }

    if (allocate_sector_on_track(track, sector)) {
        return true;
    }

    // First try Side 0, from directory downwards
    for (int i = root_track - 1; i >= 1; i--) {
        if (allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }

    // then, try Side 1, from outer track to inner track
    for (int i = 36; i <= 70; i++) {
        if (i == 53)
            i++;
        if (allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }

    // Then last try Side 0 again, from outer track back to directory
    for (int i = 35; i > root_track; i++) {
        if (allocate_sector_on_track(i, sector)) {
            track = i;
            return true;
        }
    }
    return false;
}

bool FileSystemDNP::get_next_free_sector(int &track, int &sector)
{
    int bam_bytes = num_sectors / 8;
    if (!bam_valid) {
        return false;
    }

    for(int i=0; i < bam_bytes; i++) {
        uint8_t bb = bam_buffer[32 + i];
        if (!bb)
            continue;

        uint8_t bm = 1;
        for(int j=0; j < 8; j++) {
            if (bb & bm) {
                int abs_sect = 8*i + j;
                get_track_sector(abs_sect, track, sector);
                bam_buffer[32 + i] = bb & ~bm;
                bam_dirty |= (1 << ((i + 32) >> 8)); // set dirty bit of the sector 'bb' is located in
                return true;
            }
            bm <<= 1;
        }
    }
    return false;
}


// Get number of free sectors on the file system
FRESULT FileSystemD64::get_free(uint32_t *a)
{
    if (!root_valid) {
        return FR_NO_FILESYSTEM;
    }
    uint32_t f = 0;
    for (int i = 1; i <= 35; i++) {
        if (i != 18) {
            f += root_buffer[4 * i];
        }
    }
    *a = f;
    return FR_OK;
}

// Get number of free sectors on the file system
FRESULT FileSystemD71::get_free(uint32_t *a)
{
    if (!root_valid) {
        return FR_NO_FILESYSTEM;
    }
    uint32_t f = 0;
    for (int i = 1; i <= 35; i++) {
        if (i != 18) {
            f += root_buffer[4 * i];
        }
    }
    for (int i = 36; i <= 70; i++) {
        if (i != 53) {
            f += root_buffer[0xDD + (i - 36)];
        }
    }
    *a = f;
    return FR_OK;
}

// Get number of free sectors on the file system
FRESULT FileSystemD81::get_free(uint32_t *a)
{
    if (!bam_valid) {
        return FR_NO_FILESYSTEM;
    }
    uint32_t f = 0;
    for (int i = 1; i <= 40; i++) {
        if (i != 40) {
            f += bam_buffer[0x10 + 6 * (i - 1)];
        }
    }
    for (int i = 41; i <= 80; i++) {
        f += bam_buffer[0x110 + 6 * (i - 41)];
    }
    *a = f;
    return FR_OK;
}

FRESULT FileSystemDNP::get_free(uint32_t *a)
{
    if (!bam_valid) {
        return FR_NO_FILESYSTEM;
    }
    uint32_t f = 0;
    int bam_bytes = (num_sectors / 8);
    for(int i=0; i < bam_bytes; i++) {
        uint8_t bb = bam_buffer[32+i];
        if (!bb) // no blocks
            continue;
        if (bb == 0xFF) { // all blocks
            f += 8;
            continue;
        }
        for (int j=0;j<8;j++) { // something else
            if (bb & 1) f++;
            bb >>= 1;
        }
    }
    *a = f;
    return FR_OK;
}

FRESULT FileSystemDNP :: sync(void)
{
    FRESULT fres = FileSystemCBM :: sync();

    int sector = 2;
    uint32_t dirty = bam_dirty;
    uint8_t *bam = bam_buffer;

    while(dirty) {
        if (dirty & 3) {
            DRESULT res = prt->write(bam, sector, 2);
            if(res != RES_OK) {
                return FR_DISK_ERR;
            }
        }
        dirty >>= 2;
        sector += 2;
        bam += 512;
    }
    bam_dirty = 0;
    return fres;
}

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/
DirInCBM::DirInCBM(FileSystemCBM *f, FileInfo *subdir)
{
    fs = f;
    visited = 0;
    idx = -2;
    next_t = curr_t = 0;
    next_s = curr_s = 0;

    if ((!subdir) || (subdir->cluster == 0)) {
        // by default the root directory is stored here as initial reference
        header_track = f->root_track;
        header_sector = f->root_sector;
        start_track = f->dir_track;
        start_sector = f->dir_sector;
    } else { // Subdir given
        fs->get_track_sector(subdir->cluster, header_track, header_sector);
        start_track = -1;  // it needs to be read from the header
        start_sector = -1;
    }
}

DirInCBM::~DirInCBM()
{
    if (visited) {
        delete[] visited;
    }
}


FRESULT DirInCBM::open(void)
{
    idx = -2;

    if (!visited) {
        visited = new uint8_t[fs->num_sectors];
    }
    memset(visited, 0, fs->num_sectors);
    return FR_OK;
}

FRESULT DirInCBM::close(void)
{
    return FR_OK;
}

DirEntryCBM *DirInCBM::get_pointer(void)
{
    return (DirEntryCBM*)&fs->sect_buffer[(idx & 7) << 5]; // 32x from start of sector
}

FRESULT DirInCBM::create(const char *filename, bool dir)
{
    open();
    idx = 0;
    DirEntryCBM *p;

    CbmFileName cbm;
    if (dir) {
        cbm.init_dir(filename);
    } else {
        cbm.init(filename);
    }

    /* Bring the first sector of the directory into the buffer */

    int abs_sect = fs->get_abs_sector(header_track, header_sector);
    if (abs_sect < 0) { // bad chain link
        return FR_NO_FILE;
    }
    visited[abs_sect] = 1;

    if (start_track < 0) {
        if (fs->move_window(abs_sect) != FR_OK) {
            return FR_DISK_ERR;
        }
        next_t = fs->sect_buffer[0];
        next_s = fs->sect_buffer[1];
        start_track = next_t;
        start_sector = next_s;
    } else {
        next_t = start_track;
        next_s = start_sector;
    }

    do {
        bool link = false;
        bool append = false;

        if ((idx & 7) == 0) {
            link = true;

            if (!next_t) { // end of list
                // do all directory entries have to be on this track?
                if (start_track == fs->dir_track) {
                    next_t = fs->dir_track;
                    next_s = fs->dir_sector;
                    if (!(fs->allocate_sector_on_track(next_t, next_s))) {
                        return FR_DISK_FULL;
                    }
                } else {
                    if (!(fs->get_next_free_sector(next_t, next_s))) {
                        return FR_DISK_FULL;
                    }
                }
                append = true;

                // success, now link new sector
                fs->sect_buffer[0] = (uint8_t) next_t;
                fs->sect_buffer[1] = (uint8_t) next_s;
                fs->dirty = 1;
            }
            curr_t = next_t;
            curr_s = next_s;
        }

        int abs_sect = fs->get_abs_sector(curr_t, curr_s);
        if (abs_sect < 0) {
            return FR_DISK_ERR;
        }
        if (fs->move_window(abs_sect) != FR_OK) {
            return FR_DISK_ERR;
        }
        if (append) {
            memset(fs->sect_buffer, 0, 256); // clear new sector out
            fs->sect_buffer[1] = 0xFF;
            fs->dirty = 1;
        }

        if (link) {
            next_t = fs->sect_buffer[0];
            next_s = fs->sect_buffer[1];


            if (visited[abs_sect]) { // cycle detected
                return FR_NO_FILE;
            }
            visited[abs_sect] = 1;
        }

        // We always have a sector now in which we may write the new filename IF we find an empty space
        p = (DirEntryCBM *)&fs->sect_buffer[(idx & 7) << 5]; // 32x from start of sector
        if (!p->std_fileType) { // file type is zero, so it is an unused entry
            break;
        }
        idx++;
    } while (idx < 256);

    memset(&(p->aux_track), 0x00, 11);
    p->std_fileType = cbm.getType();
    memset(p->name, 0xA0, 16);
    memcpy(p->name, cbm.getName(), cbm.getLength());
    p->data_track = 0;
    p->data_sector = 0;

    fs->dirty = 1;
    fs->sync();
    return FR_OK;
}

FRESULT DirInCBM::get_entry(FileInfo &f)
{
    if (!fs->root_valid) {
        return FR_NO_FILE;
    }

    // Fields that are always the same.
    f.fs = fs;
    f.date = 0;
    f.time = 0;

    idx++;

    if (idx == -1) {
        curr_t = -1;
        curr_s = -1;

        int abs_sect = fs->get_abs_sector(header_track, header_sector);
        if (abs_sect < 0) { // bad chain link
            return FR_NO_FILE;
        }
        if (visited[abs_sect]) { // cycle detected
            return FR_NO_FILE;
        }
        visited[abs_sect] = 1;

        if (fs->move_window(abs_sect) != FR_OK) {
            return FR_DISK_ERR;
        }

        f.cluster = abs_sect;
        f.attrib = AM_VOL;
        f.extension[0] = '\0';
        f.size = 0;
        f.name_format = NAME_FORMAT_CBM;

        fs->get_volume_name(fs->sect_buffer, f.lfname, f.lfsize);
        if (f.lfsize > 24) {
            f.lfname[24] = 0;
        }
        else {
            f.lfname[f.lfsize - 1] = 0;
        }
        if (start_track < 0) {
            next_t = fs->sect_buffer[0];
            next_s = fs->sect_buffer[1];
        } else {
            next_t = start_track;
            next_s = start_sector;
        }
        return FR_OK;
    }
    else {
        do {
            bool link = false;
            if ((idx & 7) == 0) {
                link = true;
                curr_t = next_t;
                curr_s = next_s;
                if (!next_t) { // end of list
                    return FR_NO_FILE;
                }
            }
            int abs_sect = fs->get_abs_sector(curr_t, curr_s);
            if (abs_sect < 0) { // bad chain link
                return FR_NO_FILE;
            }
            if (fs->move_window(abs_sect) != FR_OK) {
                return FR_DISK_ERR;
            }

            if (link) {
                next_t = fs->sect_buffer[0];
                next_s = fs->sect_buffer[1];

                if (visited[abs_sect]) { // cycle detected
                    return FR_NO_FILE;
                }
                visited[abs_sect] = 1;
            }

            DirEntryCBM *p = (DirEntryCBM *)&fs->sect_buffer[(idx & 7) << 5]; // 32x from start of sector
            if (!p->std_fileType) { // deleted file
                idx++;
                continue;
            }
            uint8_t tp = (p->std_fileType & 0x0f);
            if ((tp >= 1) && (tp <= 6)) {
                int j = 0;
                for (int i = 0; i < 16; i++) {
                    if ((p->name[i] == 0xA0) || (p->name[i] < 0x20))
                        break;
                    if (j < f.lfsize) {
                        f.lfname[j++] = p->name[i];
                    }
                }
                if (j < f.lfsize)
                    f.lfname[j] = 0;

                f.attrib = (p->std_fileType & 0x40) ? AM_RDO : 0;
                f.cluster = fs->get_abs_sector(p->data_track, p->data_sector);
                f.size = (int) p->size_low + 256 * (int) p->size_high;
                f.size *= 254;
                f.name_format = NAME_FORMAT_CBM;
                if (tp >= 1 && tp <= 3 && (p->geos_structure == 0 || p->geos_structure == 1) && p->aux_track) {
                    strncpy(f.extension, "CVT", 4);
                }
                else if (tp == 1) {
                    strncpy(f.extension, "SEQ", 4);
                }
                else if (tp == 2) {
                    strncpy(f.extension, "PRG", 4);
                }
                else if (tp == 3) {
                    strncpy(f.extension, "USR", 4);
                }
                else if (tp == 4) {
                    strncpy(f.extension, "REL", 4);
                }
                else if (tp == 5) {
                    strncpy(f.extension, "CBM", 4);
                }
                else if (tp == 6) {
                    //strncpy(f.extension, "DIR", 4);
                    f.attrib |= AM_DIR;
                    f.extension[0] = 0;
                }
                //printf("%5d/%3d: ", abs_sect, idx);
                return FR_OK;
            }
            idx++;
        } while (idx < 256);
        return FR_NO_FILE;
    }
    return FR_INT_ERR;
}

FileInCBM::FileInCBM(FileSystemCBM *f, DirEntryCBM *de, int dirtrack, int dirsector, int dirindex)
{
    dir_sect = f->get_abs_sector(dirtrack, dirsector);
    dir_entry_offset = dirindex * 32;
    dir_entry = *de; // make a copy!
    start_cluster = 0;
    current_track = 0;
    current_sector = 0;
    offset_in_sector = 0;
    num_blocks = 0;
    file_size = 0;
    dir_entry_modified = 0;
    visited = 0;
    fs = f;
    isRel = false;
    side = NULL;
}

FileInCBM::~FileInCBM()
{
    if (side) {
        delete side;
    }
}

FRESULT FileInCBM::open(uint8_t flags)
{
    if (!dir_entry.data_track) {
        start_cluster = -1; // to be allocated
        num_blocks = 0;
        file_size = 0;
        current_track = 0;
        current_sector = 0;
    }
    else { // open existing file
        start_cluster = fs->get_abs_sector(dir_entry.data_track, dir_entry.data_sector);
        current_track = dir_entry.data_track;
        current_sector = dir_entry.data_sector;
        num_blocks = ((uint32_t)(dir_entry.size_high) << 8) | dir_entry.size_low;
        offset_in_sector = 2;
        file_size = 254 * num_blocks;
    }

    visited = new uint8_t[fs->num_sectors];
    memset(visited, 0, fs->num_sectors);

    visit(); // mark initial sector

    if ((dir_entry.std_fileType & 0x07) == 4) {
        isRel = true;
        side = new SideSectors(fs, dir_entry.record_size);
        if (dir_entry.aux_track) {
            side->load(dir_entry.aux_track, dir_entry.aux_sector);
        }
    }

    return FR_OK;
}

FRESULT FileInCBM::close(void)
{
    fs->sync();

    if (dir_entry_modified) {
        dir_entry_modified = 0;

        FRESULT res = fs->move_window(dir_sect);
        if (res != FR_OK) {
            return res;
        }
        DirEntryCBM *p = (DirEntryCBM *)&fs->sect_buffer[dir_entry_offset];

        p->std_fileType |= 0x80; // close file

        int tr, sec;
        fs->get_track_sector(start_cluster, tr, sec);
        p->data_track = (uint8_t) tr;
        p->data_sector = (uint8_t) sec;

        if (side) {
            num_blocks += side->get_number_of_blocks();
        }
        p->size_low = num_blocks & 0xFF;
        p->size_high = num_blocks >> 8;

        if(side) {
            side->get_track_sector(tr, sec);
            p->aux_track = (uint8_t) tr;
            p->aux_sector = (uint8_t) sec;
            //p->record_size = (uint8_t) record_size; FIXME
            side->validate();
            side->write(); // this doesn't use move window, so it's OK.
        }

        fs->dirty = 1;
        fs->sync();
    }

    delete[] visited;

    return FR_OK;
}

FRESULT FileInCBM::visit(void)
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

FRESULT FileInCBM::read(void *buffer, uint32_t len, uint32_t *transferred)
{
    FRESULT res;

    uint8_t *dst = (uint8_t *) buffer;
    uint8_t *src;

    int bytes_left;
    int tr;

    *transferred = 0;
    
    if (!len)
        return FR_OK;

    while (len) {
        // make sure the current sector is within view
        res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
        if (res != FR_OK)
            return res;

        src = &(fs->sect_buffer[offset_in_sector]);

        // determine the number of bytes left in sector
        if (fs->sect_buffer[0] == 0)  { // last sector
            bytes_left = (1 + (int)fs->sect_buffer[1]) - offset_in_sector;
        }
        else {
            bytes_left = 256 - offset_in_sector;
        }

        if (!bytes_left) {
            break;
        }

        // determine number of bytes to transfer now
        if (bytes_left > len)
            tr = len;
        else
            tr = bytes_left;

        // do the actual copy
        for (int i = 0; i < tr; i++) {
            *(dst++) = *(src++);
        }
        len -= tr;
        offset_in_sector += tr;
        *transferred += tr;

        // continue
        if (offset_in_sector == 256) {
            if (fs->sect_buffer[0] == 0) {
                return FR_OK;
            }
            current_track = fs->sect_buffer[0];
            current_sector = fs->sect_buffer[1];
            offset_in_sector = 2;

            res = visit();  // mark and check for cyclic link
            if (res != FR_OK) {
                return res;
            }
        }
    }
    return FR_OK;
}

FRESULT FileInCBM::write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    FRESULT res;

    uint8_t *src = (uint8_t *) buffer;
    uint8_t *dst;

    int bytes_left;
    int tr;

    *transferred = 0;

    if (!len)
        return FR_OK;

    if (current_track == 0) { // need to allocate the first block
        fs->sync(); // make sure we can use the buffer to play around

        if (!fs->get_next_free_sector(current_track, current_sector))
            return FR_DISK_FULL;
        num_blocks = 1;
        start_cluster = fs->get_abs_sector(current_track, current_sector);

        if (side) {
            if (!side->addDataBlock(current_track, current_sector)) {
                return FR_DISK_FULL;
            }
        }

        res = fs->move_window(start_cluster);
        if (res != FR_OK) {
            return res;
        }
        dir_entry_modified = 1;
        offset_in_sector = 2;
        fs->sect_buffer[0] = 0; // unlink
        fs->sect_buffer[1] = 1; // 0 bytes in this sector
    }
    else {
        // make sure the current sector is within view
        res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
        if (res != FR_OK)
            return res;
    }

    while (len) {
        // check if we are at the end of our current sector
        if (offset_in_sector == 256) {
            if (fs->sect_buffer[0] == 0) { // we don't have any more bytes.. so extend the file
                if (!fs->get_next_free_sector(current_track, current_sector))
                    return FR_DISK_FULL;

                if (side) {
                    if (!side->addDataBlock(current_track, current_sector)) {
                        return FR_DISK_FULL;
                    }
                }

                num_blocks += 1;
                fs->sect_buffer[0] = current_track;
                fs->sect_buffer[1] = current_sector;
                fs->dirty = 1;
                dir_entry_modified = 1;
                fs->sync(); // make sure we can use the buffer to play around

                // Load the new sector
                res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
                if (res != FR_OK)
                    return res;
                fs->sect_buffer[0] = 0; // unlink
                offset_in_sector = 2;
            }
            else {
                current_track = fs->sect_buffer[0];
                current_sector = fs->sect_buffer[1];
                res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
                if (res != FR_OK)
                    return res;
                offset_in_sector = 2;
            }
        }

        bytes_left = 256 - offset_in_sector; // we can use the whole sector

        // determine number of bytes to transfer now
        if (bytes_left > len)
            tr = len;
        else
            tr = bytes_left;

        // do the actual copy
        fs->dirty = 1;
        dst = &(fs->sect_buffer[offset_in_sector]);
        for (int i = 0; i < tr; i++) {
            *(dst++) = *(src++);
        }
        len -= tr;
        offset_in_sector += tr;
        *transferred += tr;

        if (fs->sect_buffer[0] == 0) { // last sector (still)
            if ((offset_in_sector - 1) > fs->sect_buffer[1]) {// we extended the file
                fs->sect_buffer[1] = (uint8_t) (offset_in_sector - 1);
                file_size = (num_blocks - 1) * 254 + (offset_in_sector - 2);
            }
        }
    }
    fs->sync();

    // Update link to file in directory,  but we'll do this when we close the file
    // So we set the file status to dirty as well.
    return FR_OK;
}

FRESULT FileInCBM::seek(uint32_t pos)
{
    fs->sync();
    fs->get_track_sector(start_cluster, current_track, current_sector);
    memset(visited, 0, fs->num_sectors);
    uint32_t absPos = 0;

    while (pos >= 254) {
        FRESULT res = visit();
        if (res != FR_OK)
            return res;

        res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
        if (res != FR_OK)
            return res;

        if (fs->sect_buffer[0]) {
            current_track = fs->sect_buffer[0];
            current_sector = fs->sect_buffer[1];
            pos -= 254;
            absPos += 254;
        } else {
            pos = fs->sect_buffer[1] - 1;
            file_size = (absPos + pos);
            break;
        }
    }
    offset_in_sector = pos + 2;
    return FR_OK;
}

FRESULT FileInCBM::followChain(int track, int sector, int& noSectors, int& bytesLastSector)
{
    noSectors = 0;
    FRESULT res = fs->move_window(fs->get_abs_sector(track, sector));
    if (res != FR_OK)
        return res;

    while (fs->sect_buffer[0]) {
        FRESULT res = fs->move_window(fs->get_abs_sector(track, sector));
        if (res != FR_OK)
            return res;
        track = fs->sect_buffer[0];
        sector = fs->sect_buffer[1];
        noSectors++;
    }
    bytesLastSector = fs->sect_buffer[1];

    return FR_OK;
}

#if SS_DEBUG
void FileInCBM :: dumpSideSectors(void)
{
    if (!side) {
        printf("No Side Sectors defined.\n");
        return;
    }
    side->dump();
}
#endif


#if 0
if (!strcasecmp(info.extension, "CVT")) { // This is not the way to do it, especially now that info is no longer
    // filled with any name.

    res = ff->openCVT(&info, flags, dir_t, dir_s, dir_idx);
}

FRESULT FileInCBM::openCVT(FileInfo *info, uint8_t flags, int t, int s, int i)
{
    if (info->fs != fs)
        return FR_INVALID_OBJECT;

    section = -3;
    sector_index = 0;
    start_cluster = -1;
    current_track = t;
    current_sector = s;
    dir_entry_offset = i * 32;

    offset_in_sector = 2;

    file_size = info->size;
    num_blocks = (info->size + 253) / 254;

    visited = new uint8_t[fs->num_sectors];
    memset(visited, 0, fs->num_sectors);

    visit();
    return FR_OK;
}

static char cvtSignature[] = "PRG formatted GEOS file V1.0";
FRESULT FileInCBM::read(void *buffer, uint32_t len, uint32_t *transferred)
{
    bool isCVT = start_cluster == -1;
    bool lastSection = false;


    if (section == -4) { // rel header! ;)
        if (offset_in_sector == 2) {
            *(dst++) = 0x55; // FIXME record_size;
            offset_in_sector ++;
            len --;
            continue;
        } else if(offset_in_sector == 3) {
            *(dst++) = 0;
            offset_in_sector ++;
            len--;
            continue;
        } else {
            section = 0;
            offset_in_sector  = 2;
        }
    }

    if (section == -3) {
        memset(vlir, 0, 256);
        vlir[0] = 0;
        vlir[1] = 255;
        memcpy(vlir + 2, fs->sect_buffer + dir_entry_offset + 2, 30);
        memcpy(vlir + 32, cvtSignature, strlen(cvtSignature));
        memcpy(tmpBuffer, vlir, 256);
        tmpBuffer[0x15] = 1;
        tmpBuffer[0x16] = 255;
        isVlir = vlir[0x17];
        if (isVlir) {
            tmpBuffer[3] = 1;
            tmpBuffer[4] = 255;
        }
        else {
            int t1, t2;
            followChain(vlir[3], vlir[4], t1, t2);
            tmpBuffer[3] = t1;
            tmpBuffer[4] = t2;
        }
        src = tmpBuffer + offset_in_sector;
    }
    if (section == -1 && !vlir[1]) {
        memcpy(vlir, fs->sect_buffer, 256);
        memset(tmpBuffer, 0, 256);
        tmpBuffer[1] = 255;
        for (int i = 0; i < 127; i++) {
            if (!vlir[2 + 2 * i]) {
                tmpBuffer[3 + 2 * i] = vlir[3 + 2 * i];
            }
            else {
                int t1, t2;
                followChain(vlir[2 + 2 * i], vlir[3 + 2 * i], t1, t2);
                tmpBuffer[2 + 2 * i] = t1;
                tmpBuffer[3 + 2 * i] = t2;
            }
        }
        // memcpy(tmpBuffer, fs->sect_buffer, 256);
        src = tmpBuffer + offset_in_sector;
    }

    if (isCVT && isVlir && section >= 0) {
        int nextSection = section;
        if (nextSection < 127)
            nextSection++;

        while (nextSection < 127 && !vlir[2 + 2 * nextSection]
                && (vlir[3 + 2 * nextSection] == 0 || vlir[3 + 2 * nextSection] == 255))
            nextSection++;

        lastSection = nextSection >= 127;
    }

    // determine the number of bytes left in sector
    if (section >= 0 && fs->sect_buffer[0] == 0 && (!isVlir || lastSection)) { // last sector
        bytes_left = (1 + fs->sect_buffer[1]) - offset_in_sector;
    }
    else {
        bytes_left = 256 - offset_in_sector;
    }

    if (offset_in_sector == 256
            || (section >= 0 && fs->sect_buffer[0] == 0 && offset_in_sector > fs->sect_buffer[1]
                    && (!isVlir || lastSection))) { // proceed to the next sector
        if (section == -3) {
            current_track = vlir[0x15];
            current_sector = vlir[0x16];
            offset_in_sector = 2;
            section = -2;
        }
        else if (section == -2) {
            current_track = vlir[3];
            current_sector = vlir[4];
            offset_in_sector = 2;
            section = isVlir ? -1 : 0;
            vlir[0] = vlir[1] = 0;
        }
        else if (isCVT && isVlir && !fs->sect_buffer[0]) {
            if (section < 127)
                section++;

            while (section < 127 && !vlir[2 + 2 * section]
                    && (vlir[3 + 2 * section] == 0 || vlir[3 + 2 * section] == 255))
                section++;

            if (section < 127) {
                current_track = vlir[2 + 2 * section];
                current_sector = vlir[3 + 2 * section];
                offset_in_sector = 2;
            }
            else
                return FR_OK;
        }
        else {
            if ((fs->sect_buffer[0] == 0) && !isVlir && section >= 0)
                return FR_OK;
            current_track = fs->sect_buffer[0];
            current_sector = fs->sect_buffer[1];
            offset_in_sector = 2;
            sector_index += 1;
        }

#endif
