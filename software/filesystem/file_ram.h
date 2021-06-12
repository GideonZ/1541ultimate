#ifndef FILE_RAM_H
#define FILE_RAM_H

#include "file.h"

/*
 * The FileRAM class is a wrapper for a RAM buffer, such that
 * file based functions can read/write to a pre-allocated
 * buffer. Abstraction is key.. ;-)
 */

class FileRAM : public File
{
    uint8_t *ram;
    uint32_t ram_size;
    uint32_t position;
    bool buffer_owned;
public:
    FileRAM(uint8_t *buffer, uint32_t buffer_size, bool owner) : File((FileSystem *)0x100) { // 100 is a dummy value, different from 0
        ram = buffer;
        ram_size = buffer_size;
        position = 0;
        buffer_owned = owner;
    }

    // functions for reading and writing files
    FRESULT close(void) {
        delete this;
        if (buffer_owned) {
            delete[] ram;
        }
        return FR_OK;
    }

    FRESULT sync(void) {
        return FR_OK;
    }

    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) {
        uint32_t max = ram_size - position;
        if (len > max) {
            len = max;
        }
        *transferred = len;
        if (len) {
            memcpy(buffer, ram + position, len);
        }
        return FR_OK;
    }

    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) {
        uint32_t max = ram_size - position;
        if (len > max) {
            len = max;
        }
        *transferred = len;
        if (len) {
            memcpy(ram + position, buffer, len);
        }
        return FR_OK;
    }

    FRESULT seek(uint32_t pos) {
        position = pos;
        if (position > ram_size) {
            position = ram_size;
            return FR_INVALID_PARAMETER;// EOF
        }
        return FR_OK;
    }

    uint32_t get_size(void) {
        return ram_size;
    }
};

#endif
