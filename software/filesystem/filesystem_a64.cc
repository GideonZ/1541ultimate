/*
 * filesystem_a64.cc
 *
 *  Created on: Dec 16, 2023
 *      Author: Gideon
 * This filesystem fetches a file from assembly 64, caches it
 * on the /temp disk, and then opens that file instead. It 
 * thus forms a bridge between the internet and local files.
 */

#include "filesystem_a64.h"
#include "assembly.h"
#include "attachment_writer.h"
#include "filemanager.h"
#include "node_directfs.h"
#include "init_function.h"

/*************************************************************/
/* A64 File System implementation                            */
/*************************************************************/
uint32_t FileOnA64::node_count = 0;


FileSystemA64 :: FileSystemA64() : FileSystem(0)
{
}

FileSystemA64 :: ~FileSystemA64()
{

}

// functions for reading and writing files
// Opens file (creates file object)
FRESULT FileSystemA64 :: file_open(const char *filename, uint8_t flags, File **file)
{
    Path temp(filename);
    printf("A64 filesystem, trying to open: '%s'\n", filename);
    char *fixed = new char[1+strlen(filename)];
    strcpy(fixed, filename);
    fix_filename(fixed);
    FileManager *fm = FileManager::getFileManager();

    // Let's see if cached copy exists
    FileInfo inf(128);
    mstring fixed_temp_path("/Temp/_a64");  // No trailing underscore since 'fixed' already starts with one
    fixed_temp_path += fixed;
    delete[] fixed;
    FRESULT fres = fm->fstat(fixed_temp_path.c_str(), inf);

    // File was not found on the temp disk, let's download it
    if (fres == FR_NO_FILE) {
        fm->house_keeping_delete("/Temp/", "_a64_*");
        mstring work1, work2;
        const char *remain = temp.getTail(2, work2); // starts with slash, so we do +1
        assembly.request_binary(temp.getSub(0, 2, work1), remain+1);
        TempfileWriter *writer = (TempfileWriter *)assembly.get_user_context();
        if (writer) {
            fres = fm->rename(writer->get_filename(0), fixed_temp_path.c_str());
            printf("Rename from %s to %s gave: %d\n", writer->get_filename(0), fixed_temp_path.c_str(), fres);
            delete writer;
        }
    }

    if (fres == FR_OK) {
        File *temp = NULL;
        fres = fm->fopen(fixed_temp_path.c_str(), flags, &temp);
        if (temp) {
            FileOnA64 *wrapper = new FileOnA64(this, temp);
            *file = wrapper;
        }
    }
    return fres;
}

PathStatus_t FileSystemA64 :: walk_path(PathInfo& pathInfo)
{
    // Totally fake it! We just assume the whole path exists!
    // In Assembly64, the path always exists of 3 items: id, category, item
    // If hasMore is true when index = 3; we should return e_TerminateOnFile
    // This allows for the filesystem to create a mount point for items 
    // inside the temporary file.
    int elements = 0;
    for (int i=0; i<3; i++) {
        if (pathInfo.hasMore()) {
            pathInfo.index++;
            elements++;
        } else {
            break;
        }
    }
    // Check for incomplete path
    if (elements < 3) {
        return e_DirNotFound; // or entry not found, doesn't matter
    }

    // Check for overcomplete path
    if (pathInfo.hasMore()) {
        FileInfo *inf = pathInfo.getNewInfoPointer();
        strncpy(inf->lfname, pathInfo.getPathFromLastFS(), inf->lfsize);
        inf->attrib = 0; // just a file!
        inf->fs = this;
        get_extension(pathInfo.getPathFromLastFS(), inf->extension, true);
        return e_TerminatedOnFile;
    }

    FileInfo *inf = pathInfo.getNewInfoPointer();
    strncpy(inf->lfname, pathInfo.getFileName(), inf->lfsize);
    inf->attrib = 0; // just a file!
    inf->fs = this;
    get_extension(pathInfo.getFileName(), inf->extension, true);

    return e_EntryFound;
}

void add_a64_to_root(void *_context, void *_param)
{
    FileSystemA64 *a64 = new FileSystemA64();
    Node_DirectFS *node = new Node_DirectFS(a64, "a64", AM_DIR | AM_HID);
    FileManager :: getFileManager()->add_root_entry(node);
}
InitFunction init_a64("Assembly 64 FS", add_a64_to_root, NULL, NULL, 30);
