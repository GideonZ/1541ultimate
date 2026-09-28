#ifndef X00_WRAPPER_H
#define X00_WRAPPER_H

#include <stdint.h>
#include "file.h"
#include "mystring.h"

class FileManager;

// A CBM file in a PC64 host file: "C64File" and a zero, the CBM name at offset 8, a relative
// file's record length at 25, then the data. Sources: SD fatops.c; SD README, "P00 files".

#define X00_HEADER_SIZE 26

// The type letter 'P', 'S', 'U' or 'R' an x00 extension announces; `x00_name` takes a whole path.
// A name alone does not make a wrapper; the header has to be read too.
bool x00_extension(const char *ext, char *type_letter);
bool x00_name(const char *path, char *type_letter);

// The header of an x00 file, checked. cbm_name takes 17 bytes and record_length one.
bool x00_header(const uint8_t *header, uint32_t length, char *cbm_name, uint8_t *record_length);

// Opens the file at `path` and reads its header, when the path has an x00 name.
bool x00_read_header(FileManager *fm, const char *path, char *cbm_name, uint8_t *record_length);

// Cuts a header name at its first shifted space or control byte, as the CBM image reader does,
// because the screen reads $1B as an escape. Answers the length left; 0 means nothing to show.
int x00_shown_name(char *cbm_name);

// Writes the new name into the header and renames the host file after it into `dir`, counting
// the two digits up past taken spellings; with all hundred taken only the header changes.
FRESULT x00_rename(FileManager *fm, const char *path, const char *dir, const char *cbm_name,
                   mstring *renamed = NULL);

// Moves an open file past its header and answers the header size, or 0 with the file left at the
// start. The path is needed for the name; cbm_name, when given, takes 17 bytes.
uint32_t x00_skip_header(File *f, const char *path, uint8_t *record_length, char *cbm_name = NULL);

#endif /* X00_WRAPPER_H */
