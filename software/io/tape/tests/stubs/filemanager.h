// Stand-in for filemanager.h. The File class is the real one (filesystem/file.h); the
// FileManager is a fake that hands out scripted files and keeps account of every open
// and close (see tape_env.h for what it can be told to do).
#ifndef TAPE_TESTS_FILEMANAGER_H
#define TAPE_TESTS_FILEMANAGER_H
#include <stdint.h>
#include "path.h"
#include "file_system.h"

class FileManager
{
public:
    static FileManager *getFileManager();
    FRESULT fopen(const char *path, const char *filename, uint8_t flags, File **f);
    FRESULT fopen(Path *path, const char *filename, uint8_t flags, File **f);
    void    fclose(File *f);
};
#endif
