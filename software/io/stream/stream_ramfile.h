/*
 * stream_ramfile.h
 *
 *  Created on: December 26, 2022
 *      Author: Gideon
 * 
 * This is a variant on the stream_textlog, but is capable of growing
 * and returning the data in blocks until depleted.
 */

#ifndef IO_STREAM_STREAM_RAMFILE_H_
#define IO_STREAM_STREAM_RAMFILE_H_

#include "small_printf.h"
#include "indexed_list.h"
#include <string.h>

class StreamRamFile
{
    static void _put(char c, void **param) {
    	StreamRamFile *rf = (StreamRamFile *)param;
        rf->charout(c);
    }

    IndexedList<char *>blocks;
    int blocksize;
    char *write_block;
    int write_offset;
    char *read_block;
    int read_offset;
    int read_index;
public:
    StreamRamFile(int blocksize) : blocks(4, NULL) {
        write_block = new char[blocksize];
        blocks.append(write_block);
    	this->blocksize = blocksize;
    	write_offset = 0;
        read_block = NULL;
        read_index = 0;
        read_offset = 0;
    }

    ~StreamRamFile() {
        for(int i=0;i<blocks.get_elements();i++) {
            delete[] blocks[i];
        }
    }

    int getLength(void) {
        if (!write_block) {
            return 0;
        }
    	return blocksize * (blocks.get_elements() - 1) + write_offset;
    }

    void charout(char c)
    {
        if (write_offset >= blocksize) {
            write_block = new char[blocksize];
            blocks.append(write_block);
            write_offset = 0;
        }
        write_block[write_offset++] = c;
    }

    int format_ap(const char *fmt, va_list ap) {
        return _my_vprintf(StreamRamFile :: _put, (void **)this, fmt, ap);
    }

    int format(const char *fmt, ...) {
        va_list ap;
        int ret;

        va_start(ap, fmt);
        ret = _my_vprintf(StreamRamFile :: _put, (void **)this, fmt, ap);
        va_end(ap);

        return (ret);
    }

    int read(char *buf, int len)
    {
        if (!read_block) {
            read_block = blocks[read_index++];
            read_offset = 0;
        }
        if (!read_block) {
            return 0;
        }
        int limit = (read_block == write_block) ? write_offset : blocksize;
        int avail = limit - read_offset;
        if (avail < len) {
            len = avail;
        }
        memcpy(buf, read_block + read_offset, len);
        read_offset += len;
        if (read_offset == limit) {
            read_block = NULL;
        }
        return len;
    }
};


#endif /* IO_STREAM_STREAM_RAMFILE_H_ */
