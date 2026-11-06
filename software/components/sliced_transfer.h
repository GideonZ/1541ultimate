#ifndef SLICED_TRANSFER_H
#define SLICED_TRANSFER_H

#include "filemanager.h"

// FatFs holds the volume lock for the whole of one f_read or f_write, and any
// other task that needs that volume gives up after 5 s (FR_TIMEOUT). A long
// transfer between a file and memory (a 16 MB REU image, a RAM disk) therefore
// goes in slices, so others get the volume in between. 64 KB is a whole number
// of sectors, so FatFs still moves the middle of each slice without copying.
#define SLICED_TRANSFER_SIZE 0x10000

// Reads (write = false) or writes 'len' bytes at 'mem'. Before each slice,
// stop() (if given) is asked whether to give up; the transfer then ends early.
// *transferred holds the bytes moved, as with File::read and File::write.
static inline FRESULT sliced_transfer(File *file, uint8_t *mem, uint32_t len, bool write,
                                      uint32_t *transferred, bool (*stop)(void))
{
    FRESULT res = FR_OK;
    *transferred = 0;
    while (*transferred < len) {
        if (stop && stop()) {
            break;
        }
        uint32_t part = len - *transferred;
        if (part > SLICED_TRANSFER_SIZE) {
            part = SLICED_TRANSFER_SIZE;
        }
        uint32_t done = 0;
        if (write) {
            res = file->write(mem + *transferred, part, &done);
        } else {
            res = file->read(mem + *transferred, part, &done);
        }
        *transferred += done;
        if ((res != FR_OK) || (done != part)) {
            break;
        }
    }
    return res;
}

#endif
