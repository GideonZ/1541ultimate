#ifndef SLICED_TRANSFER_H
#define SLICED_TRANSFER_H

#include "filemanager.h"

// Between slices the task yields, so an FTP or HTTP task of the same priority that
// is waiting for the volume gets it: giving the FatFs mutex back does not switch to
// an equal-priority waiter, and the next slice would take it again at once.
// Lower-priority tasks (the menu, telnet) are not waited for: they are housekeeping,
// while the C64 waits on this transfer.
#ifndef SLICED_TRANSFER_YIELD
#ifdef OS
#include "FreeRTOS.h"
#include "task.h"
#define SLICED_TRANSFER_YIELD() taskYIELD()
#else
#define SLICED_TRANSFER_YIELD()
#endif
#endif

// FatFs holds the volume lock for the whole of one f_read or f_write, and any
// other task that needs that volume gives up after 5 s (FR_TIMEOUT). A long
// transfer between a file and memory (a 16 MB REU image, a RAM disk) therefore
// goes in slices, so others get the volume in between. 64 KB is a whole number
// of sectors, so FatFs still moves the middle of each slice without copying.
// Writes (saves) are given no stop function: a save is never cut short.
#define SLICED_TRANSFER_SIZE 0x10000

// Reads (write = false) or writes 'len' bytes at 'mem'. Before each slice,
// stop() (if given) is asked whether to give up; the transfer then ends early.
// Stops early, too, when the file ends (a short read).
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
        if (*transferred < len) {
            SLICED_TRANSFER_YIELD();
        }
    }
    return res;
}

#endif
