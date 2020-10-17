#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "file_info.h"
#include "fs_errors_flags.h"

class Directory
{
public:
    Directory() { }
    virtual ~Directory() { }

    virtual FRESULT get_entry(FileInfo &info);   // get next directory entry
};

#endif
