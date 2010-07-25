#include "directory.h"

FRESULT Directory :: get_entry(FileInfo &info)
{
    return fs->dir_read(this, &info);
}
