/*
 * filesystem_a64.h
 *
 *  Created on: Dec 16, 2023
 *      Author: Gideon
 */

#ifndef FILESYSTEM_A64_FILESYSTEM_H_
#define FILESYSTEM_A64_FILESYSTEM_H_

#include "file_system.h"

class FileSystemA64 : public FileSystem
{
public:
    FileSystemA64();
    ~FileSystemA64();

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **);  // Opens file (creates file object)
    PathStatus_t walk_path(PathInfo& pathInfo);
};

#endif /* FILESYSTEM_A64_FILESYSTEM_H_ */
