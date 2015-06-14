#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "integer.h"
#include "file_system.h"
//#include "file.h"
#include "mystring.h"

class Directory
{
    FileSystem *fs;
public:
    void *handle;

    Directory(FileSystem *f, void *h) {
        handle = h;
        fs = f;
    }
        
    ~Directory() { }

    virtual FRESULT get_entry(FileInfo &info);   // get next directory entry
};

// DIR (FatDIR) is a derivate from this base class. This base class implements the interface

#endif
