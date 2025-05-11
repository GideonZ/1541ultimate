/*
 * filesystem_a64.h
 *
 *  Created on: Dec 16, 2023
 *      Author: Gideon
 */

#ifndef FILESYSTEM_A64_FILESYSTEM_H_
#define FILESYSTEM_A64_FILESYSTEM_H_

#include "file_system.h"
#include "filemanager.h"

class FileSystemA64 : public FileSystem
{
public:
    FileSystemA64();
    ~FileSystemA64();

    const char *identify() { return "FileSystemA64"; } // identify the file system type
    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **);  // Opens file (creates file object)
    PathStatus_t walk_path(PathInfo& pathInfo);
};

class FileOnA64 : public File
{
    File *cached;
    uint32_t inode;
    static uint32_t node_count;
public:
    FileOnA64(FileSystem *fs, File *wrapped) : File(fs), cached(wrapped)
    {
        node_count++;
        inode = node_count;
    }
    ~FileOnA64() { }

    FRESULT close(void)
    {
        if (cached) {
            FileManager::getFileManager()->fclose(cached);
            cached = NULL;
            delete this;
            return FR_OK;
        }
        delete this;
        return FR_INT_ERR;
    }

    FRESULT sync(void)
    {
        if (cached)
            return FileManager::sync(cached);
        return FR_INT_ERR;
    }
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred)
    {
        if (cached)
            return FileManager::read(cached, buffer, len, transferred);
        return FR_INT_ERR;
    }

    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred)
    {
        if (cached)
            return FileManager::write(cached, (void *)buffer, len, transferred);
        return FR_INT_ERR;
    }

    FRESULT seek(uint32_t pos)
    {
        if (cached)
            return FileManager::seek(cached, pos);
        return FR_INT_ERR;
    }

    uint32_t get_size(void)
    {
        if (cached)
            return cached->get_size();
        return FR_INT_ERR;
    }

    uint32_t get_inode(void)
    {
        return inode;
    }
};

#endif /* FILESYSTEM_A64_FILESYSTEM_H_ */
