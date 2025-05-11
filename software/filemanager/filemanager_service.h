#ifndef FILEMANAGER_SERVICE_H
#define FILEMANAGER_SERVICE_H
#include "file.h"
#include "file_info.h"
#include "ff.h" // For FRESULT
#include "FreeRTOS.h"
#include "task.h"

enum class FileOpType {
    POLL_MEDIA,
    FSTAT,
    OPEN,
    CLOSE,
    READ,
    WRITE,
    SEEK,
    SYNC,
    DELETE,
    RENAME,
    GET_FREE,
    CREATE_DIR,
    FS_READ_SECTOR,
    FS_WRITE_SECTOR,
    OPEN_DIR,
    READ_DIR,
    CLOSE_DIR,
    IS_WRITEABLE,
};

struct FileCommand {
    FileOpType type;
    Path *path_obj;         // optional path object
    const char* path;       // optional path string
    const char* filename;   // optional filename
    const char* new_name;   // for rename
    File** file_ptr;        // to write file pointer into
    FileInfo* file_info;    // to write stat result into
    Directory **dir;        // for open/close dir
    uint8_t* buffer;        // pointing to memory to read or write
    uint32_t size;          // size command
    uint32_t* transferred;  // size transferred
    uint32_t track, sector; // for read/write sector
    TaskHandle_t caller;
    FRESULT result;
    uint8_t flags;          // for fopen
    bool open_mount;        // for stat
};

#endif // FILEMANAGER_SERVICE_H
