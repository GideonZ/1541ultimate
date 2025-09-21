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
#include "subsys.h"

extern "C" {
    #include "dump_hex.h"
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_d64(FileType :: getFileTypeFactory(), FileTypeD64 :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


FileTypeD64 :: FileTypeD64(BrowsableDirEntry *node, int ftype)
{
	this->node = node; // link
	this->ftype = ftype;
}

FileTypeD64 :: ~FileTypeD64()
{

}

int FileTypeD64 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    C64 *machine = C64 :: getMachine();

    bool can_mount = true;
    if (!(capabilities & CAPAB_MM_DRIVE) && (ftype != 1541)) {
        can_mount = false;
    } else if (ftype < 1541) {
        can_mount = false;
    }

    if ((capabilities & CAPAB_DRIVE_1541_1) && can_mount) {
#if COMMODORE
        if (machine->exists()) {
            list.append(new Action("Run Disk", runDisk_st, 0, ftype));
            count++;
        }
        list.append(new Action("Mount Disk", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64, ftype));
#else
        list.append(new Action("Mount Disk", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64, ftype));
        if (machine->exists()) {
            list.append(new Action("Run Disk", runDisk_st, 0, ftype));
            count++;
        }
#endif

        list.append(new Action("Mount Disk Read Only", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64_RO, ftype));
        list.append(new Action("Mount Disk Unlinked", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64_UL, ftype));
        count += 3;
    }

    if ((capabilities & CAPAB_DRIVE_1541_2) && can_mount) {
        list.append(new Action("Mount Disk on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_D64, ftype));
        list.append(new Action("Mount Disk R/O on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_D64_RO, ftype));
        list.append(new Action("Mount Disk Unl. on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_D64_UL, ftype));
        count += 3;
    }
    
    if (C64 :: isMP3RamDrive(0) == ftype) {list.append(new Action("Load into MP3 Drv A", loadMP3_st, (ftype << 2) | 0)); count++;}
    if (C64 :: isMP3RamDrive(1) == ftype) {list.append(new Action("Load into MP3 Drv B", loadMP3_st, (ftype << 2) | 1)); count++;}
    if (C64 :: isMP3RamDrive(2) == ftype) {list.append(new Action("Load into MP3 Drv C", loadMP3_st, (ftype << 2) | 2)); count++;}
    if (C64 :: isMP3RamDrive(3) == ftype) {list.append(new Action("Load into MP3 Drv D", loadMP3_st, (ftype << 2) | 3)); count++;}
    
    return count;
}

FileType *FileTypeD64 :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "D64")==0)
        return new FileTypeD64(obj, 1541);
    if(strcmp(inf->extension, "D71")==0)
        return new FileTypeD64(obj, 1571);
    if(strcmp(inf->extension, "D81")==0)
        return new FileTypeD64(obj, 1581);
    if(strcmp(inf->extension, "DNP")==0)
        return new FileTypeD64(obj, DRVTYPE_MP3_DNP);
    return NULL;
}

SubsysResultCode_e FileTypeD64 :: runDisk_st(SubsysCommand *cmd)
{
    // First command is to mount the disk
    SubsysCommand *drvcmd = new SubsysCommand(cmd->user_interface, SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64, cmd->mode, cmd->path.c_str(), cmd->filename.c_str());
    drvcmd->execute();

    // Second command is to perform a load"*",8,1
    char *drvId = "H";
    drvId[0] = 0x40 + c1541_A->get_current_iec_address();
    SubsysCommand *c64cmd = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_MOUNT_LOAD_RUN, drvId, "*");
    c64cmd->execute();

    return SSRET_OK;
}

SubsysResultCode_e FileTypeD64 :: loadMP3_st(SubsysCommand *cmd)
{
    int total_bytes_read;
    uint32_t bytes_read;

    int drvNo = cmd->functionID & 3;
    int ftype = cmd->functionID >> 2;
    uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
    uint8_t ramBase = reu[0x7dc7 + drvNo] ;
    
    bool ok = C64 :: isMP3RamDrive(drvNo) == ftype;
    if (ramBase > 0x40) ok=false;
    if (!ok)
    {
       cmd->user_interface->popup("Invalid operation", BUTTON_OK);
       return SSRET_WRONG_MOUNT_MODE;
    }
    
    uint8_t* dstAddr = reu+(((uint32_t) ramBase) << 16);
    
    int expSize = 1;
    if (ftype == 1541) expSize = 174848;
    if (ftype == 1571) expSize = 2*174848;
    if (ftype == 1581) expSize = 819200;
    if (ftype == DRVTYPE_MP3_DNP)
    {
        expSize = C64 :: getSizeOfMP3NativeRamdrive(drvNo);
        
        FileInfo info(32);
        FileManager *fm = FileManager :: getFileManager();
        fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), info);
        int actSize = info.size;
        
        if (expSize != actSize)
        {
           cmd->user_interface->popup("Size does not match", BUTTON_OK);
           return SSRET_MP3_INVALID_SIZE;
        }
        if (expSize > 12*1024*1024)
        {
           cmd->user_interface->popup("DNP file too large", BUTTON_OK);
           return SSRET_MP3_DNP_TOO_LARGE;
        }
    }

    FileManager *fm = FileManager::getFileManager();
    FileInfo info(32);
    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), info);

    File *file = 0;
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
    if (file) {
        total_bytes_read = 0;

        if (ftype == 1571) {
            file->read(dstAddr, expSize / 2, &bytes_read);
            total_bytes_read += bytes_read;
            file->read(dstAddr + 700 * 256, expSize / 2, &bytes_read);
        } else {
            file->read(dstAddr, expSize, &bytes_read);
        }
        total_bytes_read += bytes_read;

        printf("\nClosing file. ");
        fm->fclose(file);
        file = NULL;
        printf("done.\n");
        static char buffer[48];
        sprintf(buffer, "Bytes loaded: %d ($%8x)", total_bytes_read, total_bytes_read);
        cmd->user_interface->popup(buffer, BUTTON_OK);
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem::get_error_string(fres), BUTTON_OK);
        return SSRET_DISK_ERROR;
    }
    return SSRET_OK;
}
