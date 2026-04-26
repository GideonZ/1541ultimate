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
#include "rtc.h"
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
    root_buffer = new uint8_t[256];
    sect_buffer = new uint8_t[256];

    p->ioctl(GET_SECTOR_COUNT, &num_sectors);
    root_dirty = false;
    root_valid = false;
    layout = lay;

    init();
}

FileSystemCBM::~FileSystemCBM()
{
    delete[] sect_buffer;
    delete[] root_buffer;
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
    prt->ioctl(CTRL_SYNC, NULL);
    return FR_OK;
}

void FileSystemCBM::get_volume_name(uint8_t *sector, char *buffer, int buf_len, bool isRoot)
{
    int offset = isRoot ? volume_name_offset : 4; // FIXME, hard coded now
    /* Volume name extraction */
    for (int i = 0; i < 24; i++) {
        if (i < buf_len) {
            char c = char(sector[offset + i]);
            buffer[i] = c;
        }
    }
}

// Opens directory (creates dir object, NULL = root)
FRESULT FileSystemCBM::dir_open(const char *path, Directory **dir) // Opens directory
{
    DirInCBM *dd;

    PathInfo pi(this);
    pi.init(path);
    PathStatus_t pres = walk_path(pi);
    if (pres == e_EntryFound) {
        FileInfo *info = pi.getLastInfo();
        dd = new DirInCBM(this, info->cluster);
    } else {
        return FR_NO_PATH;
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

    FRESULT fres;
    DirInCBM cbmdir(this, parent->cluster);

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
    get_volume_name(root_buffer, (char *)(blk+2), 27, true); // get stuff like dos & disk ID
    memset(blk + 2, 0xA0, 16); // clear name part
    fat_to_petscii(nameToCreate, false, (char *)(blk + 2), 16, false); // overwrite with dir name
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

FRESULT FileSystemCBM::file_open(const char *pathname, uint8_t flags, File **file)
{
    FileInfo info(24);
    DirInCBM *dd;
    mstring fn;
    bool create = false;
    bool clear = false;
    *file = NULL;

    PathInfo pi(this);
    pi.init(pathname);
    pi.workPath.up(&fn); // just look for the parent dir

    PathStatus_t pres = walk_path(pi);
    if (pres != e_EntryFound) {
        return FR_NO_PATH;
    }
    dd = new DirInCBM(this, pi.getLastInfo()->cluster);

    FRESULT fres = find_file(fn.c_str(), dd, &info);
    if (fres == FR_NO_FILE) {
        create = (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS));
        if (!create) {
            return FR_NO_FILE;
        }
    } else if (fres == FR_OK) {
        if (flags & FA_CREATE_NEW) {
            delete dd;
            return FR_EXIST;
        }
        clear = (flags & FA_CREATE_ALWAYS);
    } else {
        delete dd;
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

    *file = ff;
    FRESULT res = ff->open(flags);

    if (res == FR_OK) {
        return res;
    }

    delete ff;
    delete *file;
    *file = NULL;
    return res;
}

FRESULT FileSystemCBM::file_rename(const char *old_name, const char *new_name)
{
    PathInfo pi(this);
    pi.init(old_name);
    PathStatus_t pres = walk_path(pi);
    if (pres == e_DirNotFound) {
        return FR_NO_PATH;
    }
    if (pres != e_EntryFound) {
        return FR_NO_FILE;
    }
    const char *filename = pi.getFileName();

    DirInCBM *dd = new DirInCBM(this, pi.getParentInfo()->cluster);
    FileInfo info(20);
    FRESULT res = find_file(filename, dd, &info);

/*
    FileInfo info(20);
    DirInCBM *dd = new DirInCBM(this);
    FRESULT res = find_file(old_name, dd, &info);
*/

    {
        const char *p = new_name;
        while (*p)
        {
            if (*p == '/')
                new_name = p + 1;
            p++;
        }
    }

    CbmFileName cbm;

    if (res == FR_OK) {
        DirEntryCBM *p = dd->get_pointer();
        if ((p->std_fileType & 0x07) == 0x06) {
            cbm.init_dir(new_name);
        } else {
            cbm.init(new_name);
        }

        if (cbm.getType() != 7) {
        	p->std_fileType = (p->std_fileType & 0xF8) | cbm.getType(); // save upper bits
        }
        memset(p->name, 0xA0, 16);
        memcpy(p->name, cbm.getName(), cbm.getLength());
        dirty = 1;
        sync();
    }

    delete dd;
    return res;
}

FRESULT FileSystemCBM::deallocate_vlir_records(uint8_t track, uint8_t sector, uint8_t *visited)
{
	uint8_t *rblk = new uint8_t[256];
    FRESULT res = read_sector(rblk, track, sector);
    if (res == FR_OK) {
    	if ((rblk[0] == 0) && (rblk[1] == 0xFF)) { // valid single block, assuming record block
    		for (int i=0;i<127;i++) {
    			if (rblk[2*i+2]) { // valid entry
    				res = deallocate_chain(rblk[2*i+2], rblk[2*i+3], visited);
    				if (res != FR_OK) {
    					break;
    				}
    			} else if (!rblk[2*i+3]) {
    				break; // 00 00 ends the list
    			}
    		}
    	}
    }
    delete[] rblk;
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

    DirInCBM *dd = new DirInCBM(this, pi.getParentInfo()->cluster);
    FileInfo info(20);
    FRESULT res = find_file(filename, dd, &info);

    // Deleting directories is not yet implemented, we should check here if the dir is empty.
    if (info.attrib & AM_DIR) {
        return FR_DENIED;
    }
    if (res == FR_OK) {
        DirEntryCBM *p = dd->get_pointer();
        bool vlir = is_vlir_entry(p);
        p->std_fileType = 0x00;
        dirty = 1;
        // Saving information, since moving the window makes *p invalid!
        int file_track = p->data_track;
        int file_sector = p->data_sector;
        int side_track = p->aux_track;
        int side_sector = p->aux_sector;
        if (vlir) {
        	deallocate_vlir_records(file_track, file_sector, dd->visited);
        }
        deallocate_chain(file_track, file_sector, dd->visited); // file chain
        deallocate_chain(side_track, side_sector, dd->visited); // side sector chain
        sync();
    }
    delete dd;
    return res;
}

FRESULT FileSystemCBM::read_sector(uint8_t *buffer, int track, int sector)
{
    int abs_sect = get_abs_sector(track, sector);
    if (abs_sect < 0) {
        return FR_INVALID_PARAMETER;
    }
    DRESULT res = prt->read(buffer, abs_sect, 1);
    if (res != RES_OK) {
        return FR_DISK_ERR;
    }
    return FR_OK;
}

FRESULT FileSystemCBM::write_sector(uint8_t *buffer, int track, int sector)
{
    int abs_sect = get_abs_sector(track, sector);
    if (abs_sect < 0) {
        return FR_INVALID_PARAMETER;
    }
    DRESULT res = prt->write(buffer, abs_sect, 1);
    if (res != RES_OK) {
        return FR_DISK_ERR;
    }
    return FR_OK;
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

    const int standard_num_sectors = 683;

    // free all sectors
    for(int i=0; i < standard_num_sectors; i++) {
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

    const int standard_num_sectors = 683*2;

    // free all sectors
    for(int i=0; i < standard_num_sectors; i++) {
        int t, s;
        get_track_sector(i, t, s);
        set_sector_allocation(t, s, false);
    }

    // allocate the two sectors for bam and first directory block
    set_sector_allocation(root_track, root_sector, true);
    set_sector_allocation(root_track, root_sector+1, true);
    set_sector_allocation(dir_track, dir_sector, true);

    // allocate the track on side 1 with extended directory sectors
    for(int i=0; i < 19; i++) {
        set_sector_allocation(53, i, true);
    }

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

    const int standard_num_sectors = 3200;

    // free all sectors
    for(int i=0; i < standard_num_sectors; i++) {
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
    bam_buffer[8] = (uint8_t )((num_sectors + 255) / 256); // num tracks

    int bam_bytes = num_sectors / 8;
    bam_buffer[36] = 0x1F; // in total 32+3 blocks in use after format
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
    root_dirty |= success;
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
    bam_dirty |= success;
    return success;
}

bool FileSystemDNP::set_sector_allocation(int track, int sector, bool alloc)
{
    int offset = track * 32;
    uint8_t *m = &bam_buffer[offset]; // since track starts with 1, this bumps the header exactly
    bool success = modify_allocation_bit(NULL, m, sector ^ 7, alloc); // the exor with 7 reverses the bits
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

        uint8_t bm = 128;
        for(int j=0; j < 8; j++) {
            if (bb & bm) {
                int abs_sect = 8*i + j;
                get_track_sector(abs_sect, track, sector);
                bam_buffer[32 + i] = bb & ~bm;
                bam_dirty |= (1 << ((i + 32) >> 8)); // set dirty bit of the sector 'bb' is located in
                return true;
            }
            bm >>= 1;
        }
    }
    return false;
}


// Get number of free sectors on the file system
FRESULT FileSystemD64::get_free(uint32_t *a, uint32_t *cs)
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
    *cs = 256;
    return FR_OK;
}

// Get number of free sectors on the file system
FRESULT FileSystemD71::get_free(uint32_t *a, uint32_t *cs)
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
    *cs = 256;
    return FR_OK;
}

// Get number of free sectors on the file system
FRESULT FileSystemD81::get_free(uint32_t *a, uint32_t *cs)
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
    *cs = 256;
    return FR_OK;
}

FRESULT FileSystemDNP::get_free(uint32_t *a, uint32_t *cs)
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
    *cs = 256;
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
    prt->ioctl(CTRL_SYNC, NULL);
    return fres;
}

/*********************************************************************/
/* D64/D71/D81 File System implementation                            */
/*********************************************************************/
DirInCBM::DirInCBM(FileSystemCBM *f, uint32_t cluster)
{
    fs = f;
    visited = 0;
    idx = -2;
    next_t = curr_t = 0;
    next_s = curr_s = 0;

    if (!cluster) {
        // by default the root directory is stored here as initial reference
        header_track = f->root_track;
        header_sector = f->root_sector;
        start_track = f->dir_track;
        start_sector = f->dir_sector;
        root = true;
    } else { // Subdir given
        fs->get_track_sector(cluster, header_track, header_sector);
        start_track = -1;  // it needs to be read from the header
        start_sector = -1;
        root = false;
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

    memset(&(p->aux_track), 0x00, 11); // set everything to zero

    p->std_fileType = cbm.getType();
    memset(p->name, 0xA0, 16);
    memcpy(p->name, cbm.getName(), cbm.getLength());
    p->data_track = 0;
    p->data_sector = 0;

#ifndef RUNS_ON_PC
    int y,m,d,wd,h,mn,s;
    rtc.get_time(y, m, d, wd, h, mn, s);
    p->year = (y >= 20) ? (uint8_t)(y - 20) : (uint8_t)(y + 80);
    p->month = (uint8_t)m;
    p->day = (uint8_t)d;
    p->hour = (uint8_t)h;
    p->minute = (uint8_t)mn;
#endif

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

        fs->get_volume_name(fs->sect_buffer, f.lfname, f.lfsize, root);
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

                uint16_t yr = (p->year < 80) ? (p->year + 20) : (p->year - 80);
                f.date  = yr << 9;
                f.date |= (((uint16_t)(p->month)) << 5);
                f.date |= p->day;
                f.time  = ((uint16_t)(p->hour)) << 11;
                f.time |= (((uint16_t)(p->minute)) << 5);

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

FileInCBM::FileInCBM(FileSystemCBM *f, DirEntryCBM *de, int dirtrack, int dirsector, int dirindex) : File(f)
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
    cvt = NULL;
    state = ST_LINEAR;
    header.size = 0;
    header.data = NULL;
}

FileInCBM::~FileInCBM()
{
    if (side) {
        delete side;
    }
    if (cvt) {
        delete cvt;
    }
    if (header.data) {
        delete[] header.data;
    }
    if (visited) {
        delete[] visited;
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

    uint8_t tp = dir_entry.std_fileType & 0x07;
    if (tp == 4) {
        isRel = true;
        side = new SideSectors(fs, dir_entry.record_size);
        if (dir_entry.aux_track) {
            side->load(dir_entry.aux_track, dir_entry.aux_sector);
        }
        state = ST_HEADER;
        header.size = 2;
        header.data = new uint8_t[2];
        header.pos = 0;
        header.data[0] = dir_entry.record_size;
        header.data[1] = 0;
    } else if (tp >= 1 && tp <= 3 && (dir_entry.geos_structure == 0 || dir_entry.geos_structure == 1) && dir_entry.aux_track) {
        if (!(flags & FA_OPEN_FROM_CBM)) { // do not do this when opened from IEC
            // CVT
            state = ST_HEADER;
            header.data = new uint8_t[4*254];
            memset(header.data, 0, 4*254);
            header.size = create_cvt_header();
            header.pos = 0;
        }
    } else if ((tp == 7) && (flags & FA_CREATE_ANY)) { // creating a new CVT file; see hack in the 'create' function of DirInCBM.
    	state = ST_HEADER;
        header.data = new uint8_t[254];
        memset(header.data, 0, 254);
        header.size = 254;
        header.pos = 0;

        // The header block is never written, although directory entry is taken from here
        // The second block of the header is the info block and that should always be written to the file system
        // .. we don't need any data from here to construct the records in case of VLIR, so we can always write it out
        // The third block of the header is the record block in case of VLIR, and should be written to the file system
        // .. in case of a linear file, this is the first data block, and should therefore also be written to the file system
        // The fourth block, if G98 format is never written to the file system, otherwise it is a data block. But we don't
        // .. know this until the very end when the file is closed.
        // So the conclusion is that for writing CVT files, only the header block should be seen as 'header'. The rest of the
        // blocks may be just written to the disk directly. In case of G98, we simply deallocate the 4th block afterwards.

        // Allocate CVT structure. Having this pointer set is handy for knowing later on that we need to fix the disk
        // structure. Secondly, the structure can be filled by reading the necessary data blocks and decode them. It is easier
        // to use this structure than to use individual bytes from the disk.
        cvt = new cvt_t;
        cvt->records = 0;
        cvt->current_section = 0;
    }

    return FR_OK;
}

static const char cvtSignature[]     = "PRG formatted GEOS file V1.0";
static const char cvtSignatureLong[] = "PRG GeoConvert98-format V2.0";

int FileInCBM::create_cvt_header(void)
{
    // allocate CVT structure
    cvt = new cvt_t;
    cvt->records = 0;
    cvt->current_section = 0;

    // Sector 1
    memcpy(header.data, &(dir_entry.std_fileType), 30); // copy Dir entry as is

    // Let's clear out the track/header info, as it is no longer valid in the cvt file

    header.data[1] = 0;
    header.data[2] = 0;
    header.data[19] = 0;
    header.data[20] = 0;

    // signature depends on later actions, but we start with the standard one
    strcpy((char *)&header.data[30], cvtSignature);

    // Sector 2 -> info block
    FRESULT fres = fs->move_window(fs->get_abs_sector(dir_entry.aux_track, dir_entry.aux_sector));
    if (fres != FR_OK) {
        printf("Failed to read aux track/sector: %d:%d\n", dir_entry.aux_track, dir_entry.aux_sector);
        return 30; // just the dir entry
    }
    memcpy(&header.data[254], &fs->sect_buffer[2], 254);

    // Sector 3 -> record block for VLIR, or terminate on single record if it is a linear file
    if (!dir_entry.geos_structure) { // linear file
        followChain(dir_entry.data_track, dir_entry.data_sector, cvt->sections[0].blocks, cvt->sections[0].bytes_in_last);
        cvt->sections[0].start  = fs->get_abs_sector(dir_entry.data_track, dir_entry.data_sector);
        cvt->records = 1;
        current_track = dir_entry.data_track;
        current_sector = dir_entry.data_sector;
        return 2*254; // 2 blocks header only
    }

    // VLIR
    // Read the Record block
    DRESULT dres = fs->prt->read(cvt->record_block, fs->get_abs_sector(dir_entry.data_track, dir_entry.data_sector), 1);
    if (dres != RES_OK) {
        return 30; // just the dir entry
    }

    uint8_t *vlir = cvt->record_block + 2;
    memcpy(&header.data[2*254], vlir, 254);

    bool long_files = false;
    cvt->records = 127;

    for (int i = 0; i < cvt->records; i++) {
        if ((!vlir[2*i]) && (!vlir[1+2*i])) { // both bytes are 0
            cvt->records = i;
            break;
        }
        cvt->sections[i].start = fs->get_abs_sector(vlir[2*i], vlir[1+2*i]);
        if (cvt->sections[i].start >= 0) { // valid?
            followChain(vlir[2*i], vlir[1 + 2*i], cvt->sections[i].blocks, cvt->sections[i].bytes_in_last);
            if (cvt->sections[i].blocks > 255) {
                long_files = true;
            }
        } else {
            cvt->sections[i].blocks = 0;
            cvt->sections[i].bytes_in_last = vlir[1+2*i]; // just copy last byte code, may be FF
        }
        //printf("Section #%d. Start: %d, Blocks: %d, Last: %d\n", i, cvt->sections[i].start, cvt->sections[i].blocks, cvt->sections[i].bytes_in_last);
    }

    // Strip off any skips from the end
    for (int i = cvt->records-1; i >= 0; i--) {
        if (vlir[2*i]) {
            cvt->records = i+1;
            break;
        }
    }

    // Find first sector
    for (int i=0; i < cvt->records; i++) {
        if (vlir[2*i]) {
            // Now we know the first track/sector of the data segment
            current_track = vlir[2*i];
            current_sector = vlir[2*i+1];
            cvt->current_section = i;
            break;
        }
    }

    // Place the correct header
    if (long_files) {
        memcpy(&header.data[3*254], vlir, 254); // prepare sector 4
        strcpy((char *)&header.data[30], cvtSignatureLong);
    }

    // Header could be SEQ or PRG, depending on actual type
/*
    if ((dir_entry.std_fileType & 7) != 2) {
        memcpy(header.data+30, "SEQ", 3);
    }
*/

    uint8_t *hdrLo = header.data + 2*254;
    uint8_t *hdrHi = header.data + 3*254;

    for(int i=0; i < cvt->records; i++) {
        hdrLo[2*i + 0] = (uint8_t)cvt->sections[i].blocks;
        hdrLo[2*i + 1] = (uint8_t)cvt->sections[i].bytes_in_last;
        hdrHi[2*i + 0] = (uint8_t)(cvt->sections[i].blocks >> 8);
        hdrHi[2*i + 1] = 0; // (uint8_t)cvt->sections[i].bytes_in_last;
    }

    return (long_files) ? 4*254 : 3*254;
}

FRESULT FileInCBM::fixup_cbm_file(void)
{
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
        p->record_size = header.data[0];
        side->validate(p->record_size);
        side->write(); // this doesn't use move window, so it's OK.
    }
    fs->dirty = 1;
    return fs->sync();
}

bool FileSystemCBM::is_vlir_entry(DirEntryCBM *p)
{
	uint8_t ty = p->std_fileType & 0x07;
	if ((ty < 1) || (ty > 3)) {
		return false;
	}
	if (! p->geos_filetype) { // not a GEOS file
		return false;
	}
	if (p->geos_structure != 1) { // not a VLIR file
		return false;
	}
	return true;
}

FRESULT FileInCBM::fixup_cvt(void)
{
	int mode = 0;
	// First the header is verified, to see if we need to do a CVT or G98 decode, or maybe neither
    if (memcmp(&header.data[33], cvtSignature + 3, 25) == 0) { //
    	mode = 1;
    } else if (memcmp(&header.data[33], cvtSignatureLong + 3, 25) == 0) { //
    	mode = 2;
    }

	FRESULT res = fs->move_window(dir_sect);
	if (res != FR_OK) {
		return res;
	}

	DirEntryCBM *p = (DirEntryCBM *)&fs->sect_buffer[dir_entry_offset];

    if (!mode) {
		p->std_fileType = 0x81; // set filetype to SEQ as the header check failed.
		fs->dirty = 1;
    	return fixup_cbm_file();
    }

    // Save chain start, since it will be overwritten shortly.
    int tr, sec;
    fs->get_track_sector(start_cluster, tr, sec);
    uint8_t track = (uint8_t)tr;
    uint8_t sector = (uint8_t)sec;

    // printf("CVT was written starting from %d:%d\n", track, sector);
    // We can safely take the dir entry from the header
    memcpy(&(p->std_fileType), header.data, 30);

    p->data_track = track;
    p->data_sector = sector;
    fs->dirty = 1;

    // Do we have an info block? We know this by observing the aux pointer. If it zero, there is no info block  (but what if it is cleared?)
    // Update: CVT files always have an info block, otherwise it wouldn't be a CVT file to start with.
    if (true) { // (p->aux_track) {
    	// Link the first block to the info pointer
    	p->aux_track = track;
    	p->aux_sector = sector;

    	// Move pointer to the first block, and clip it there by clearing the track number
    	int abs = fs->get_abs_sector(track, sector);
    	res = fs->move_window(abs);
    	if (res != FR_OK) {
    		return res;
    	}
    	// store link to next
    	track = fs->sect_buffer[0];
    	sector = fs->sect_buffer[1];

    	// terminate
    	fs->sect_buffer[0] = 0;
    	fs->sect_buffer[1] = 0xFF;
    	fs->dirty = 1;

    	// Return to directory
    	res = fs->move_window(dir_sect);
    	if (res != FR_OK) {
    		return res;
    	}
    }

    // The next block could either be a VLIR record block or the first data block of a sequential file.
    // Either way, the data pointer should point at it.
    p->data_track  = track;
	p->data_sector = sector;
	fs->dirty = 1;

    if (!p->geos_filetype || !p->geos_structure) { // sequential
    	// Nothing needs to be done for sequential files;
    	return fs->sync();
    }

    // Now we know that the next block is a record block. We now need to know where to cut the chain into records.
    res = fs->move_window(fs->get_abs_sector(track, sector));
	if (res != FR_OK) {
		return res;
	}
	track = fs->sect_buffer[0];
	sector = fs->sect_buffer[1];

	memset(cvt->sections, 0, sizeof(cvt->sections));
	uint8_t *vlir = fs->sect_buffer + 2;

	cvt->records = 127;
	for (int i=0;i<127;i++) {
        if (!vlir[2*i] && !vlir[2*i+1]) {
            cvt->records = i;
            break;
        }
        cvt->sections[i].blocks = vlir[2*i];
		cvt->sections[i].bytes_in_last = vlir[2*i + 1];
    }

    // Clip the file on disk here, as the record block is only one sector at all times
    fs->sect_buffer[0] = 0;
    fs->sect_buffer[1] = 0xFF;
    fs->dirty = 1;
/*
    printf("Record header block (after fix):\n");
    dump_hex_relative(fs->sect_buffer, 256);
    printf("Going to consider %d records.\n", cvt->records);
*/

    // From now on, don't move the window anymore.
    // When G98 format, the next block contains the high-bytes of the sector counts.
    if (mode == 2) { // G98 mode
        res = fs->read_sector(cvt->record_block, track, sector);
		if (res != FR_OK) {
			return res;
		}
		fs->set_sector_allocation(track, sector, false);
		// printf("Extended header block:\n");
		// dump_hex_relative(cvt->record_block, 256);

		track = cvt->record_block[0];
		sector = cvt->record_block[1];

		vlir = cvt->record_block + 2;
		for (int i=0; i < cvt->records; i++) {
			cvt->sections[i].blocks += 256 * (int)vlir[2*i];
		}
    }

    // Now that the records lengths and end-bytes have been read, we can traverse through the file and clip
    // each record by writing the end byte into the link bytes of each last sector
    // Track and sector are the pointers to the current block. Just read each sector, count and cut.
    for (int i=0; i < cvt->records; i++) {
    	if (! cvt->sections[i].blocks) { // 0 blocks are skipped
    		continue;
    	}
        uint8_t ct;
        uint8_t cs;
        // printf("Record #%d starts at: %d:%d and should be %d sectors long.\n", i, track, sector, cvt->sections[i].blocks);
        fs->sect_buffer[2 + 2*i] = track;
        fs->sect_buffer[3 + 2*i] = sector;
        int sectors_read = 0;
        for (int b=0; b < cvt->sections[i].blocks; b++) {
            ct = track;
            cs = sector;
            if (!ct) {
                break;
            }
            res = fs->read_sector(cvt->record_block, ct, cs);
            if (res != FR_OK) {
                printf("Read sector %d:%d failed with error %s\n", ct, cs, FileSystem :: get_error_string(res));
                break;
    		}
            // printf("%d: %d:%d\n", sectors_read, ct, cs);
            sectors_read ++;
            track = cvt->record_block[0];
    		sector = cvt->record_block[1];
    	}
        if (cvt->sections[i].blocks != sectors_read) {
            printf("ERROR: Could not read all sectors of this record! Read: %d. Expected: %d\n", sectors_read, cvt->sections[i].blocks);
            break;
        }
        uint8_t last_byte = (uint8_t)cvt->sections[i].bytes_in_last;
        if (cvt->record_block[0]) {
            cvt->record_block[0] = 0;
            cvt->record_block[1] = last_byte;
            fs->write_sector(cvt->record_block, ct, cs);
        } else if (cvt->record_block[1] != last_byte) {
            printf("ERROR: Last byte does not match. (%02x != %02x)\n", cvt->record_block[i], last_byte);
        }
    }
    return fs->sync();
}

FRESULT FileInCBM::close(void)
{
    fs->sync();
    FRESULT res = FR_OK;

    if (dir_entry_modified) {
    	dir_entry_modified = 0;

    	if (cvt) {
        	res = fixup_cvt();
    	} else {
    		res = fixup_cbm_file();
    	}
    }

    delete this;
    return res;
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
    uint32_t tr;

    *transferred = 0;
    
    if (!len)
        return FR_OK;

    while (len) {
        switch (state) {
            case ST_HEADER:
                res = read_header(dst, len, tr);
                break;
            case ST_LINEAR:
                res = read_linear(dst, len, tr);
                break;
            case ST_CVT:
                res = read_linear(dst, len, tr);
                break;
            case ST_END:
                res = FR_OK;
                tr = 0;
                break;
            default:
                res = FR_DENIED;
        }
        if (res != FR_OK)
            return res;

        if (!tr) {
            break;
        }
        *transferred += tr;
        dst += tr;
        len -= tr;
    }
    return FR_OK;
}

FRESULT FileInCBM::read_linear(uint8_t *dst, int len, uint32_t& tr)
{
    FRESULT res;
    uint8_t *src;
    int bytes_left;

    // make sure the current sector is within view
    res = fs->move_window(fs->get_abs_sector(current_track, current_sector));

/*
    printf("Reading track %d sector %d results in %d:\n", current_track, current_sector, res);
    dump_hex_relative(fs->sect_buffer, 256);
    printf("--> We are now at %02x offset. Wanting to read %d bytes.\n", offset_in_sector, len);
*/

    if (res != FR_OK)
        return res;

    if (!len) {
        return FR_OK;
    }

    src = &(fs->sect_buffer[offset_in_sector]);

    // determine the number of bytes left in sector
    bool merge_cvt_sector = cvt && (cvt->current_section != cvt->records - 1);
    if ((merge_cvt_sector) || (fs->sect_buffer[0] != 0)) {
        bytes_left = 256 - offset_in_sector;
    }
    else {
        if (fs->sect_buffer[1] < 2) {
            dump_hex_relative(fs->sect_buffer, 32);
            return FR_DISK_ERR;
        }
        bytes_left = (1 + (int)fs->sect_buffer[1]) - offset_in_sector;
    }

    // determine number of bytes to transfer now
    if (bytes_left > len)
        tr = len;
    else
        tr = bytes_left;

    // do the actual copy
    memcpy(dst, src, tr);
    offset_in_sector += tr;

    // continue
    if (offset_in_sector == 256) { // end of sector
        if (fs->sect_buffer[0] == 0) {
            if (merge_cvt_sector) { // joining vlir records together
                do {
                    cvt->current_section ++;
                    if  (cvt->current_section >= cvt->records) {
                        break;
                    }
                    current_track = cvt->record_block[2 + 2*cvt->current_section];
                    current_sector = cvt->record_block[3 + 2*cvt->current_section];
                } while(!current_track);
                offset_in_sector = 2;
                if (!current_track) {
                    state = ST_END;
                    return FR_OK; // no more valid records
                }
            } else {
                state = ST_END;
                return FR_OK; // end of linear file, or end of last vlir record
            }
        } else { // simply follow chain
            current_track = fs->sect_buffer[0];
            current_sector = fs->sect_buffer[1];
            offset_in_sector = 2;
        }
        res = visit();  // mark and check for cyclic link
    }
    return res;
}

FRESULT FileInCBM::read_header(uint8_t *dst, int len, uint32_t& tr)
{
    tr = header.size - header.pos;
    if (tr > len) {
        tr = len;
    }
    uint8_t *src = header.data + header.pos;
    memcpy(dst, src, tr);
    header.pos += tr;

    if (header.pos == header.size) {
        state = ST_LINEAR;
    }
    return FR_OK;
}

FRESULT FileInCBM::write_header(uint8_t *src, int len, uint32_t& tr)
{
    tr = header.size - header.pos;
    if (tr > len) {
        tr = len;
    }
    uint8_t *dst = header.data + header.pos;
    memcpy(dst, src, tr);
    header.pos += tr;

    if (header.pos == header.size) {
        state = ST_LINEAR;
    }
    return FR_OK;
}

FRESULT FileInCBM::write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    FRESULT res;

    uint8_t *src = (uint8_t *) buffer;
    uint32_t tr;

    *transferred = 0;

    while (len) {
        switch (state) {
            case ST_HEADER:
                res = write_header(src, len, tr);
                break;
            case ST_LINEAR:
            case ST_END:
                res = write_linear(src, len, tr);
                break;
            case ST_CVT:
                res = FR_DENIED;
                break;
            default:
                res = FR_DENIED;
        }
        if (res != FR_OK)
            return res;

        if (!tr) {
            break;
        }
        *transferred += tr;
        src += tr;
        len -= tr;
    }
    return FR_OK;
}

FRESULT FileInCBM::write_linear(uint8_t *src, int len, uint32_t& tr)
{
    FRESULT res;

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

    tr = 0;
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
                //fs->sync(); // make sure we can use the buffer to play around

                // Load the new sector
                res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
                if (res != FR_OK)
                    return res;
                fs->sect_buffer[0] = 0; // unlink
                fs->sect_buffer[1] = 1; // 0 bytes in this sector
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

        int bytes = 256 - offset_in_sector; // we can use the whole sector

        // determine number of bytes to transfer now
        if (bytes > len)
            bytes = len;

        // do the actual copy
        fs->dirty = 1;
        uint8_t *dst = &(fs->sect_buffer[offset_in_sector]);
        memcpy(dst, src, bytes);
        src += bytes;
        len -= bytes;
        offset_in_sector += bytes;
        tr += bytes;

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

uint32_t FileInCBM::get_size()
{
    uint32_t size = header.size;
    if (side) {
        size += side->get_file_size();
        file_size = size;
        return size;
    }

    // no side sectors, so we check how big the file currently is
    int ct, cs, abs;
    abs = start_cluster;
    memset(visited, 0, fs->num_sectors);
    fs->get_track_sector(abs, ct, cs);
    do {
        abs = fs->get_abs_sector(ct, cs);
        if (visited[abs]) {
            break;
        }
        visited[abs] = 1;
        FRESULT res = fs->move_window(abs);
        if (res != FR_OK)
            break;
        if (fs->sect_buffer[0]) {
            ct = fs->sect_buffer[0];
            cs = fs->sect_buffer[1];
            size += 254;
        } else {
            size += fs->sect_buffer[1] - 1;
            break;
        }
    } while(1);
    memset(visited, 0, fs->num_sectors);
    file_size = size;
    return size;
}

FRESULT FileInCBM::seek(uint32_t pos)
{
    fs->sync();


    if (start_cluster < 0) { // file doesn't yet exist
        if (!pos) {
            return FR_OK; // allow if seek to position 0
        }
    }

    fs->get_track_sector(start_cluster, current_track, current_sector);
    memset(visited, 0, fs->num_sectors);
    FRESULT res = fs->move_window(fs->get_abs_sector(current_track, current_sector));
    if (res != FR_OK)
        return res;

    if (pos < header.size) {
        state = ST_HEADER;
        header.pos = pos;
        pos = 0;
    } else {
        state = ST_LINEAR;
        pos -= header.size;
    }

    if (side) { // do side sectors exist? Then we can find the destination faster
        res = side->seek(pos, offset_in_sector, current_track, current_sector);
        return res;
    }

    uint32_t absPos = 0;
    while (pos >= 254) {
        res = visit();
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
            if (pos < 0) {
            	pos = 0;
            }
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

    do {
        noSectors++;
        FRESULT res = fs->move_window(fs->get_abs_sector(track, sector));
        if (res != FR_OK)
            return res;
        track = fs->sect_buffer[0];
        sector = fs->sect_buffer[1];
    } while (fs->sect_buffer[0]);

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

/*
- CVT Header sector, with directory entry, signature, etc..  (used to be section -3 in your code), doesn't exist in the CBM image, aside from the dir entry.
- Geos Info Block  (used to be section -2 in your code)  (actual sector can be found by de-referencing the REL file side sector pointer, which I now called 'aux_track / aux_sector' at offset 0x15/0x16.
- VLIR Record block, skipped when not VLIR (section -1 in your code)   Actual sector can be found by de-referencing standard file pointer, which I now called 'data_track / data_sector' at offset 0x03/0x04
- VLIR Extended Record block (only when G98 format), also not applicable for sequential / non VLIR.
- Records, (padded except for the last record) - section 0 and higher in your code.
*/
