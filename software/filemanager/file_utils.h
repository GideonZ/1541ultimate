#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "filemanager.h"

// Utility functions
FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target, const char *matchPattern);
FRESULT delete_recursive(Path *path, const char *name);
FRESULT print_directory(const char *path);
FRESULT load_file(const char *path, const char *filename, uint8_t *mem, uint32_t maxlen, uint32_t *transferred);
FRESULT save_file(bool overwrite, const char *path, const char *filename, uint8_t *mem, uint32_t len, uint32_t *transferred);
FRESULT fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool overwrite);

// User Interface support functions
// void  get_display_string(Path *p, const char *filename, char *buffer, int width);

#endif // FILE_UTILS_H
