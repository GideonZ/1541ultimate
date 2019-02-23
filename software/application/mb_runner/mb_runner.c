/*
 * mb_runner.cc (for Ultimate-II+ or U64)
 *
 *  Created on: Feb 13, 2019
 *      Author: gideon
 */

#include <stdio.h>
#include "itu.h"
#include "u64.h"
#include "dump_hex.h"

extern uint32_t _ultimate_bin_start;
extern uint32_t _ultimate_bin_end;

extern uint32_t _jumper_bin_start;
extern uint32_t _jumper_bin_end;

static void swapcpy(void *d, const void *s, int length)
{
    uint8_t *dest = (uint8_t *)d;
    const uint8_t *source = (uint8_t *)s;

    while(length > 0) {
        *(dest++) = source[3];
        *(dest++) = source[2];
        *(dest++) = source[1];
        *(dest++) = source[0];
        length -= 4;
        source += 4;
    }
}

void memclear(void)
{
    uint32_t *dest = (uint32_t *)0x2000000;
    for(int i=0;i<0x0800000;i++) {
        *(dest++) = 0xF0000000;
    }
}

int main(int argc, char *argv[])
{
    int len1, len2;

    U64_MB_RESET = 1;

    printf("Clearing memory.\n");
    memclear();

    uint8_t *dest = (uint8_t *)0x2010000;

    len1 = (int)&_ultimate_bin_end - (int)&_ultimate_bin_start;
    swapcpy(dest, &_ultimate_bin_start, len1);
    memcpy(&_ultimate_bin_start, dest, len1); // copy back

    dest = (uint8_t *)0x2000000;

    len2 = (int)&_jumper_bin_end - (int)&_jumper_bin_start;
    swapcpy(dest, &_jumper_bin_start, len2);

    uint32_t *d32 = (uint32_t *)0x2001000;
    *d32 = 0;

    ioWrite8(ITU_IRQ_DISABLE, 0xFF);
    ioWrite8(ITU_IRQ_CLEAR, 0xFF);
    ioWrite8(ITU_MISC_IO, 0x4); // disable caches
    U64_MB_RESET = 0;


    U64_MB_RESET = 1;
    printf("Verifying...\n");

    dest = (uint8_t *)0x2010000;
    dump_hex_verify(&_ultimate_bin_start, dest, len1);

    while(1)
        ;
}
