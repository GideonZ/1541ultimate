// modem.cc uses the file manager only to relay a file to an incoming caller.
#ifndef HOST_MODEM_FILEMANAGER_H
#define HOST_MODEM_FILEMANAGER_H
#include <stdint.h>
typedef int FRESULT;
#define FR_OK 0
#define FA_READ 1
class File { public: int read(void *, uint32_t, uint32_t *) { return 1; } };
class FileManager {
public:
    static FileManager *getFileManager() { static FileManager f; return &f; }
    FRESULT fopen(const char *, int, File **) { return 1; }
    FRESULT fread(File *, void *, uint32_t, uint32_t *) { return 1; }
    void fclose(File *) {}
};
#endif
