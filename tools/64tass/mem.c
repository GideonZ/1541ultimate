/*
    $Id: mem.c 1510 2017-04-30 21:51:04Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include "mem.h"
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#endif
#include <errno.h>
#include "error.h"
#include "file.h"
#include "64tass.h"
#include "listing.h"
#include "arguments.h"

struct memblock_s { /* starts and sizes */
    size_t p;
    address_t addr, len;
    struct memblocks_s *ref;
};

static int memblockcomp(const void *a, const void *b) {
    const struct memblock_s *aa = (const struct memblock_s *)a;
    const struct memblock_s *bb = (const struct memblock_s *)b;
    address_t ad = aa->addr, bd = bb->addr;
    if (ad < bd) return -1;
    return (ad > bd) ? 1 : 0;
}

static void memcomp(struct memblocks_s *memblocks) {
    unsigned int i, j, k;
    if (memblocks->compressed) return;
    memblocks->compressed = true;
    memjmp(memblocks, 0);

    for (j = 0; j < memblocks->p; j++) { /* replace references with real copies */
        struct memblocks_s *b = memblocks->data[j].ref;
        if (b != NULL) {
            size_t rest;
            memcomp(b);
            rest = memblocks->p - j - 1;
            memblocks->p += b->p - 1;
            if (memblocks->p >= memblocks->len) {
                memblocks->len = memblocks->p + 64;
                if (/*memblocks->len < 64 ||*/ memblocks->len > SIZE_MAX / sizeof *memblocks->data) err_msg_out_of_memory(); /* overflow */
                memblocks->data = (struct memblock_s *)reallocx(memblocks->data, memblocks->len * sizeof *memblocks->data);
            }
            memmove(&memblocks->data[j + b->p], &memblocks->data[j + 1], rest * sizeof *memblocks->data);
            for (k = 0; k < b->p; k++) {
                struct memblock_s *b2 = &memblocks->data[j + k];
                b2->p = memblocks->mem.p;
                b2->len = b->data[k].len;
                b2->ref = NULL;
                b2->addr = b->data[k].addr;
                memblocks->mem.p += b2->len;
                if (memblocks->mem.p >= memblocks->mem.len) {
                    memblocks->mem.len = memblocks->mem.p + 0x1000;
                    if (memblocks->mem.len < 0x1000) err_msg_out_of_memory(); /* overflow */
                    memblocks->mem.data = (uint8_t *)reallocx(memblocks->mem.data, memblocks->mem.len);
                }
                memcpy(&memblocks->mem.data[b2->p], &b->mem.data[b->data[k].p], b2->len);
            }
            j--;
        }
    }
    if (memblocks->p < 2) return;
    if (arguments.output.mode == OUTPUT_XEX ||
            arguments.output.mode == OUTPUT_IHEX ||
            arguments.output.mode == OUTPUT_SREC) return;

    for (k = j = 0; j < memblocks->p; j++) {
        struct memblock_s *bj = &memblocks->data[j];
        if (bj->len != 0) {
            for (i = j + 1; i < memblocks->p; i++) if (memblocks->data[i].len != 0) {
                struct memblock_s *bi = &memblocks->data[i];
                if (bj->addr <= bi->addr && (bj->addr + bj->len) > bi->addr) {
                    size_t overlap = (bj->addr + bj->len) - bi->addr;
                    if (overlap > bi->len) overlap = bi->len;
                    memcpy(memblocks->mem.data + bj->p + (bi->addr - bj->addr), memblocks->mem.data + bi->p, overlap);
                    bi->len -= overlap;
                    bi->p += overlap;
                    bi->addr += overlap;
                    continue;
                }
                if (bi->addr <= bj->addr && (bi->addr + bi->len) > bj->addr) {
                    size_t overlap = bi->addr + bi->len - bj->addr;
                    if (overlap > bj->len) overlap = bj->len;
                    bj->addr += overlap;
                    bj->p += overlap;
                    bj->len -= overlap;
                    if (bj->len == 0) break;
                }
            }
            if (bj->len != 0) {
                if (j != k) memblocks->data[k] = *bj;
                k++;
            }
        }
    }
    memblocks->p = k;
    qsort(memblocks->data, memblocks->p, sizeof *memblocks->data, memblockcomp);
}

void memjmp(struct memblocks_s *memblocks, address_t adr) {
    struct memblock_s *block;
    if (memblocks->mem.p == memblocks->lastp) {
        memblocks->lastaddr = adr;
        return;
    }
    if (memblocks->p >= memblocks->len) {
        memblocks->len += 64;
        if (/*memblocks->len < 64 ||*/ memblocks->len > SIZE_MAX / sizeof *memblocks->data) err_msg_out_of_memory(); /* overflow */
        memblocks->data = (struct memblock_s *)reallocx(memblocks->data, memblocks->len * sizeof *memblocks->data);
    }
    block = &memblocks->data[memblocks->p++];
    block->len = (address_t)(memblocks->mem.p - memblocks->lastp);
    block->p = memblocks->lastp;
    block->ref = NULL;
    block->addr = memblocks->lastaddr;
    memblocks->lastp = memblocks->mem.p;
    memblocks->lastaddr = adr;
}

void memref(struct memblocks_s *memblocks, struct memblocks_s *ref) {
    struct memblock_s *block;
    if (memblocks->p >= memblocks->len) {
        memblocks->len += 64;
        if (/*memblocks->len < 64 ||*/ memblocks->len > SIZE_MAX / sizeof *memblocks->data) err_msg_out_of_memory(); /* overflow */
        memblocks->data = (struct memblock_s *)reallocx(memblocks->data, memblocks->len * sizeof *memblocks->data);
    }
    block = &memblocks->data[memblocks->p++];
    block->len = 0;
    block->p = memblocks->lastp;
    block->ref = ref;
    block->addr = memblocks->lastaddr;
}

static void printrange(address_t start, address_t end) {
    char temp[10], temp2[10];
    sprintf(temp, "$%04" PRIaddress, start);
    sprintf(temp2, "$%04" PRIaddress, end-1);
    printf("Memory range: %10s-%-7s $%04" PRIaddress "\n", temp, temp2, end-start);
}

void memprint(struct memblocks_s *memblocks) {
    unsigned int i;
    bool over;
    address_t start, end;

    memcomp(memblocks);

    if (memblocks->p == 0) return;

    i = 0;
    start = memblocks->data[i].addr;
    end = memblocks->data[i].addr + memblocks->data[i].len;
    over = end < start;
    for (i++; i < memblocks->p; i++) {
        const struct memblock_s *block = &memblocks->data[i];
        if (block->addr != end || over) {
            printrange(start, end);
            start = block->addr;
        }
        end = block->addr + block->len;
        over = end < block->addr;
    }
    printrange(start, end);
}

static void putlw(unsigned int w, FILE *f) {
    putc(w, f);
    putc(w >> 8, f);
}

static void padding(size_t size, FILE *f) {
    while (size >= 0x80000000) {
        if (fseek(f, 0x40000000, SEEK_CUR) != 0) goto err;
        size -= 0x40000000;
    }
    if ((long)size > 256 && fseek(f, (long)size, SEEK_CUR) == 0) {
        return;
    }
err:while ((size--) != 0) if (putc(0, f) == EOF) break;
}

static void output_mem_c64(FILE *fout, const struct memblocks_s *memblocks, const struct output_s *output) {
    address_t pos, end;
    unsigned int i;

    if (memblocks->p != 0) {
        pos = memblocks->data[0].addr;
        if (output->mode == OUTPUT_CBM || output->mode == OUTPUT_APPLE) {
            putlw(pos, fout);
        }
        if (output->mode == OUTPUT_APPLE) {
            end = memblocks->data[memblocks->p - 1].addr + memblocks->data[memblocks->p - 1].len;
            end -= pos;
            putlw(end, fout);
        } else if (output->mode == OUTPUT_CBM) {
            if (output->longaddr) putc(pos >> 16,fout);
        }
        for (i = 0; i < memblocks->p; i++) {
            const struct memblock_s *block = &memblocks->data[i];
            padding(block->addr - pos, fout);
            if (fwrite(memblocks->mem.data + block->p, block->len, 1, fout) == 0) return;
            pos = block->addr + block->len;
        }
    }
}

static void output_mem_nonlinear(FILE *fout, const struct memblocks_s *memblocks, bool longaddr) {
    address_t start, size;
    unsigned int i, last;

    if (memblocks->p != 0) {
        i = 0;
        start = memblocks->data[i].addr;
        size = memblocks->data[i].len;
        last = i;
        for (i++; i < memblocks->p; i++) {
            const struct memblock_s *block = &memblocks->data[i];
            if (block->addr != start + size) {
                putlw(size,fout);
                if (longaddr) putc(size >> 16,fout);
                putlw(start,fout);
                if (longaddr) putc(start >> 16,fout);
                while (last < i) {
                    const struct memblock_s *b = &memblocks->data[last++];
                    if (fwrite(memblocks->mem.data + b->p, b->len, 1, fout) == 0) return;
                }
                start = block->addr;
                size = 0;
            }
            size += block->len;
        }
        putlw(size,fout);
        if (longaddr) putc(size >> 16, fout);
        putlw(start, fout);
        if (longaddr) putc(start >> 16,fout);
        while (last < i) {
            const struct memblock_s *b = &memblocks->data[last++];
            if (fwrite(memblocks->mem.data + b->p, b->len, 1, fout) == 0) return;
        }
    }
    putlw(0, fout);
    if (longaddr) putc(0, fout);
}

static void output_mem_flat(FILE *fout, const struct memblocks_s *memblocks) {
    address_t pos;
    unsigned int i;

    for (pos = i = 0; i < memblocks->p; i++) {
        const struct memblock_s *block = &memblocks->data[i];
        padding(block->addr - pos, fout);
        if (fwrite(memblocks->mem.data + block->p, block->len, 1, fout) == 0) return;
        pos = block->addr + block->len;
    }
}

static void output_mem_atari_xex(FILE *fout, const struct memblocks_s *memblocks) {
    address_t start, size;
    unsigned int i, last;

    if (memblocks->p != 0) {
        i = 0;
        start = memblocks->data[i].addr;
        size = memblocks->data[i].len;
        last = i;
        for (i++; i < memblocks->p; i++) {
            const struct memblock_s *block = &memblocks->data[i];
            if (block->addr != start + size) {
                if (last == 0 || start == 0xffff) putlw(0xffff, fout);
                putlw(start, fout);
                putlw(start + size - 1, fout);
                while (last < i) {
                    const struct memblock_s *b = &memblocks->data[last++];
                    if (fwrite(memblocks->mem.data + b->p, b->len, 1, fout) == 0) return;
                }
                start = block->addr;
                size = 0;
            }
            size += block->len;
        }
        if (last == 0 || start == 0xffff) putlw(0xffff, fout);
        putlw(start, fout);
        putlw(start + size - 1, fout);
        while (last < i) {
            const struct memblock_s *b = &memblocks->data[last++];
            if (fwrite(memblocks->mem.data + b->p, b->len, 1, fout) == 0) return;
        }
    }
}

static void hexput(FILE *fout, unsigned int b) {
    const char *hex = "0123456789ABCDEF";
    putc(hex[(b >> 4) & 0xf], fout);
    putc(hex[b & 0xf], fout);
}

struct ihex_s {
    FILE *file;
    address_t address, segment;
    uint8_t data[32];
    unsigned int length;
};

static void output_mem_ihex_line(FILE *fout, unsigned int length, address_t address, unsigned int type, const uint8_t *data) {
    unsigned int i, sum = length + address + (address >> 8) + type;
    putc(':', fout);
    hexput(fout, length);
    hexput(fout, address >> 8);
    hexput(fout, address);
    hexput(fout, type);
    for (i = 0; i < length; i++) {
        hexput(fout, data[i]);
        sum += data[i];
    }
    hexput(fout, -sum);
    putc('\n', fout);
}

static void output_mem_ihex_data(struct ihex_s *ihex) {
    uint8_t *data = ihex->data;
    if ((ihex->address & 0xffff) + ihex->length > 0x10000) {
        uint16_t length = (uint16_t)-ihex->address;
        unsigned int remains = ihex->length - length;
        ihex->length = length;
        output_mem_ihex_data(ihex);
        data += length;
        ihex->length = remains;
    }
    if (((ihex->address ^ ihex->segment) & ~(address_t)0xffff) != 0) {
        uint8_t ez[2];
        ez[0] = (uint8_t)(ihex->address >> 24);
        ez[1] = (uint8_t)(ihex->address >> 16);
        output_mem_ihex_line(ihex->file, sizeof ez, 0, 4, ez);
        ihex->segment = ihex->address;
    }
    output_mem_ihex_line(ihex->file, ihex->length, ihex->address, 0, data);
    ihex->address += ihex->length;
    ihex->length = 0;
}

static void output_mem_ihex(FILE *fout, const struct memblocks_s *memblocks) {
    struct ihex_s ihex;
    size_t i;

    if (memblocks->p != 0) {
        ihex.file = fout;
        ihex.address = 0;
        ihex.segment = 0;
        ihex.length = 0;
        for (i = 0; i < memblocks->p; i++) {
            const struct memblock_s *b = &memblocks->data[i];
            const uint8_t *d = memblocks->mem.data + b->p;
            address_t addr = b->addr;
            size_t blen = b->len;
            if (blen != 0 && ihex.address + ihex.length != addr) {
                if (ihex.length != 0) output_mem_ihex_data(&ihex);
                ihex.address = addr;
            }
            while (blen != 0) {
                size_t left = (sizeof ihex.data) - ihex.length;
                size_t copy = blen > left ? left : blen;
                memcpy(ihex.data + ihex.length, d, copy); 
                ihex.length += copy; 
                d += copy;
                blen -= copy;
                if (ihex.length == sizeof ihex.data) {
                    output_mem_ihex_data(&ihex);
                }
            }
        }
        if (ihex.length != 0) output_mem_ihex_data(&ihex);
        output_mem_ihex_line(fout, 0, 0, 1, NULL);
    }
}

struct srecord_s {
    FILE *file;
    unsigned int type;
    address_t address;
    uint8_t data[32];
    unsigned int length;
};

static void output_mem_srec_line(struct srecord_s *srec) {
    unsigned int i;
    unsigned int sum = srec->length + srec->address + (srec->address >> 8) + (srec->address >> 16) + (srec->address >> 24);
    putc('S', srec->file);
    putc(srec->length ? ('1' + srec->type) : ('9' - srec->type), srec->file);
    sum += srec->type + 3;
    hexput(srec->file, srec->length + srec->type + 3);
    if (srec->type > 1) hexput(srec->file, srec->address >> 24);
    if (srec->type > 0) hexput(srec->file, srec->address >> 16);
    hexput(srec->file, srec->address >> 8);
    hexput(srec->file, srec->address);
    for (i = 0; i < srec->length; i++) {
        hexput(srec->file, srec->data[i]);
        sum += srec->data[i];
    }
    hexput(srec->file, ~sum);
    putc('\n', srec->file);
    srec->address += srec->length;
    srec->length = 0;
}

static void output_mem_srec(FILE *fout, const struct memblocks_s *memblocks) {
    struct srecord_s srec;
    size_t i;

    if (memblocks->p != 0) {
        srec.file = fout;
        srec.type = 0;
        srec.address = 0;
        srec.length = 0;
        for (i = 0; i < memblocks->p; i++) {
            const struct memblock_s *b = &memblocks->data[i];
            size_t end = b->addr + b->len - 1;
            if (end >= 0x10000 && srec.type < 1) srec.type = 1;
            if (end >= 0x1000000 && srec.type < 2) {
                srec.type = 2;
                break;
            }
        }
        for (i = 0; i < memblocks->p; i++) {
            const struct memblock_s *b = &memblocks->data[i];
            const uint8_t *d = memblocks->mem.data + b->p;
            address_t addr = b->addr;
            size_t blen = b->len;
            if (blen != 0 && srec.address + srec.length != addr) {
                if (srec.length != 0) output_mem_srec_line(&srec);
                srec.address = addr;
            }
            while (blen != 0) {
                size_t left = (sizeof srec.data) - srec.length;
                size_t copy = blen > left ? left : blen;
                memcpy(srec.data + srec.length, d, copy); 
                srec.length += copy; 
                d += copy;
                blen -= copy;
                if (srec.length == sizeof srec.data) {
                    output_mem_srec_line(&srec);
                }
            }
        }
        if (srec.length != 0) output_mem_srec_line(&srec);
        srec.address = memblocks->data[0].addr;
        output_mem_srec_line(&srec);
    }
}

void output_mem(struct memblocks_s *memblocks, const struct output_s *output) {
    FILE* fout;
    struct linepos_s nopoint = {0, 0};
    bool binary = (output->mode != OUTPUT_IHEX) && (output->mode != OUTPUT_SREC);
    int err;

    memcomp(memblocks);

    if (memblocks->mem.p == 0) return;

    if (dash_name(output->name)) {
#ifdef _WIN32
        if (binary) setmode(fileno(stdout), O_BINARY);
#endif
        fout = stdout;
    } else {
        fout = file_open(output->name, binary ? "wb" : "wt");
    }
    if (fout == NULL) {
        err_msg_file(ERROR_CANT_WRTE_OBJ, output->name, &nopoint);
        return;
    }
    clearerr(fout); errno = 0;
    switch (output->mode) {
    case OUTPUT_FLAT: output_mem_flat(fout, memblocks); break;
    case OUTPUT_NONLINEAR: output_mem_nonlinear(fout, memblocks, output->longaddr); break;
    case OUTPUT_XEX: output_mem_atari_xex(fout, memblocks); break;
    case OUTPUT_RAW:
    case OUTPUT_APPLE:
    case OUTPUT_CBM: output_mem_c64(fout, memblocks, output); break;
    case OUTPUT_IHEX: output_mem_ihex(fout, memblocks); break;
    case OUTPUT_SREC: output_mem_srec(fout, memblocks); break;
    }
    err = ferror(fout);
    err |= (fout != stdout) ? fclose(fout) : fflush(fout);
    if (err != 0 && errno != 0) err_msg_file(ERROR_CANT_WRTE_OBJ, output->name, &nopoint);
#ifdef _WIN32
    setmode(fileno(stdout), O_TEXT);
#endif
}

FAST_CALL void write_mem(struct memblocks_s *memblocks, unsigned int c) {
    if (memblocks->mem.p >= memblocks->mem.len) {
        memblocks->mem.len += 0x1000;
        if (memblocks->mem.len < 0x1000) err_msg_out_of_memory(); /* overflow */
        memblocks->mem.data = (uint8_t *)reallocx(memblocks->mem.data, memblocks->mem.len);
    }
    memblocks->mem.data[memblocks->mem.p++] = (uint8_t)c;
}

static size_t omemp;
static size_t ptextaddr;
static address_t oaddr, oaddr2;

void mark_mem(const struct memblocks_s *memblocks, address_t adr, address_t adr2) {
    ptextaddr = memblocks->mem.p;
    omemp = memblocks->p;
    oaddr = adr;
    oaddr2 = adr2;
}

void get_mem(const struct memblocks_s *memblocks, size_t *memp, size_t *membp) {
    *memp = memblocks->mem.p;
    *membp = memblocks->p;
}

int read_mem(const struct memblocks_s *memblocks, size_t memp, size_t membp, size_t offs) {
    size_t len;
    if (memp >= memblocks->mem.p) return -1;
    for (;;) {
        if (membp < memblocks->p) {
            len = memblocks->data[membp].len - (memp - memblocks->data[membp].p);
        } else {
            len = memblocks->mem.p - memp;
        }
        if (offs < len) return memblocks->mem.data[memp + offs];
        offs -= len;
        memp += len;
        if (membp + 1 < memblocks->p) {
            len = memblocks->data[membp + 1].addr - memblocks->data[membp].addr - memblocks->data[membp].len;
        } else if (membp < memblocks->p) {
            len = memblocks->lastaddr - memblocks->data[membp].addr - memblocks->data[membp].len;
        } else return -1;
        if (offs < len) return -1;
        offs -= len;
        membp++;
    }
}

void write_mark_mem(struct memblocks_s *memblocks, unsigned int c) {
    memblocks->mem.data[ptextaddr] = (uint8_t)c;
}

void list_mem(const struct memblocks_s *memblocks, bool dooutput) { 
    address_t myaddr;
    size_t len;
    bool first = true, print = true;

    for (;omemp <= memblocks->p;omemp++, first = false) {
        if (omemp < memblocks->p) {
            if (first && oaddr < memblocks->data[omemp].addr) {
                len = 0; myaddr = oaddr; omemp--;
            } else {
                len = memblocks->data[omemp].len - (ptextaddr - memblocks->data[omemp].p);
                myaddr = (memblocks->data[omemp].addr + memblocks->data[omemp].len - len) & all_mem;
            }
        } else {
            if (first && oaddr < memblocks->lastaddr) {
                len = 0; myaddr = oaddr; omemp--;
            } else {
                myaddr = memblocks->lastaddr + (address_t)(ptextaddr - memblocks->lastp);
                len = memblocks->mem.p - ptextaddr;
                if (len == 0) {
                    if (!print) continue;
                    if (first) myaddr = oaddr;
                    else myaddr = (memblocks->data[omemp-1].addr + memblocks->data[omemp-1].len) & all_mem;
                }
            }
        }
        listing_mem(listing, memblocks->mem.data + ptextaddr, dooutput ? len : 0, myaddr, ((oaddr2 + myaddr - oaddr) & 0xffff) | (oaddr2 & ~(address_t)0xffff));
        print = false;
        ptextaddr += len;
    }
}

void restart_memblocks(struct memblocks_s *memblocks, address_t address) {
    memblocks->mem.p = 0;
    memblocks->lastp = 0;
    memblocks->p = 0;
    memblocks->lastaddr = address;
    memblocks->compressed = false;
}

void init_memblocks(struct memblocks_s *memblocks) {
    memblocks->mem.p = 0;
    memblocks->mem.len = 0;
    memblocks->mem.data = NULL;
    memblocks->p = 0;
    memblocks->len = 0;
    memblocks->lastp = 0;
    memblocks->lastaddr = 0;
    memblocks->data = NULL;
    memblocks->compressed = false;
}

void destroy_memblocks(struct memblocks_s *memblocks) {
    free(memblocks->mem.data);
    free(memblocks->data);
}
