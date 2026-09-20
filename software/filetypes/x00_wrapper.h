#ifndef X00_WRAPPER_H
#define X00_WRAPPER_H

#include <stdint.h>
#include "file.h"

// A CBM file inside a host file, the format PC64 and its descendants write: the host name
// ends in P, S, U or R and two digits, the file starts with "C64File" and a zero, the CBM
// name is the 16 bytes at offset 8, a relative file's record length is at offset 25, and
// the data follows the header. Sources: SD fatops.c; SD README under "P00 files".
//
// Both the Software IEC drive and the C64 loader read these files, so the header lives
// here and neither of them carries its own copy.

#define X00_HEADER_SIZE 26

// The CBM file type the host name announces, as the letter 'P', 'S', 'U' or 'R', and
// false when the name is not an x00 name. The name alone does not make a wrapper; the
// header has to be read as well.
bool x00_name(const char *path, char *type_letter);

// The header of an x00 file, checked. cbm_name takes 17 bytes and record_length one.
bool x00_header(const uint8_t *header, uint32_t length, char *cbm_name, uint8_t *record_length);

// Moves an open file past its header and answers the size of the header, or leaves the
// file at the start and answers 0 when it carries none. The path is needed for the name.
uint32_t x00_skip_header(File *f, const char *path, uint8_t *record_length);

#endif /* X00_WRAPPER_H */
