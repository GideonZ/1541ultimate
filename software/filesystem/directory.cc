#include "directory.h"

FRESULT Directory :: get_entry(FileInfo &info)
{
    return FR_NO_FILESYSTEM; // fs->dir_read(this, &info);
}
