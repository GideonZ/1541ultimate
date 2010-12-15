#ifndef FILE_PARTITION_H
#define FILE_PARTITION_H

#include "blockdev.h"
#include "disk.h"
#include "file_system.h"
#include "partition.h"
#include "file_direntry.h"
#include "path.h"
#include "fat_fs.h" // WORKAROUND FOR NOT HAVING A FACTORY YET

// we derive this class from the FileDirEntry. This enables us
// to access the root directory directly as children from a
// partition (by using FileInfo), and yet we still have the
// possibility to override the way the entry is shown in the
// browser, and how the fetching of children works.

class FilePartition : public FileDirEntry
{
	Partition *prt;
    
    void init(void);
public:
    FilePartition(PathObject *par, Partition *p, char *n);
    virtual ~FilePartition();

    char *get_type_string(BYTE typ);
    char *get_display_string(void);
    int   fetch_children(void);
};

#endif
