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
/* T64 File System implementation                            */
/*************************************************************/

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
    printf("A64 filesystem, trying to open: '%s'\n", filename);
    assembly.request_binary(filename);
    TempfileWriter *writer = (TempfileWriter *)assembly.get_user_context();
    if (writer) {
        mstring tempfile(TEMP_FILE_PATH);
        FileManager *fm = FileManager::getFileManager();
        FRESULT fres = fm->fopen(writer->get_filename(0), flags, file);
        delete writer;
        return fres;
    } else {
        return FR_NO_FILE;
    }
}

PathStatus_t FileSystemA64 :: walk_path(PathInfo& pathInfo)
{
    // Totally fake it! We just assume the whole path exists!
    // TODO: If hasMore is true when index = 3; we should return e_TerminateOnFile
    while (pathInfo.hasMore()) {
        pathInfo.index++;
    }
    return e_EntryFound;
}

void add_a64_to_root(void *_context, void *_param)
{
    FileSystemA64 *a64 = new FileSystemA64();
    Node_DirectFS *node = new Node_DirectFS(a64, "a64");
    FileManager :: getFileManager()->add_root_entry(node);
}
InitFunction init_a64(add_a64_to_root, NULL, NULL, 30);
