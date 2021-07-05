#ifndef _C64_CRT_H
#define _C64_CRT_H

#include "filemanager.h"
#include "c64.h"

#define CARTS_DIRECTORY "/flash/carts"

// Local definitions, NOT hardware select!
typedef enum {
    CART_NOT_IMPL,
    CART_NORMAL,
    CART_ACTION,
    CART_RETRO,
    CART_DOMARK,
    CART_OCEAN_8K,
    CART_OCEAN_16K,
    CART_EASYFLASH,
    CART_SUPERSNAP,
    CART_EPYX,
    CART_FINAL3,
    CART_SYSTEM3,
    CART_KCS,
    CART_FINAL12,
    CART_COMAL80,
    CART_SBASIC,
    CART_WESTERMANN,
    CART_BBASIC,
    CART_PAGEFOX,
    CART_EXOS,
    CART_SUPERGAMES,
    CART_NORDIC,
    CART_GMOD2,
    CART_BLACKBOX3,
    CART_BLACKBOX4,
    CART_BLACKBOX8,
    CART_C128_STD,
    CART_C128_STD_IO,
} e_known_cart;

class C64_CRT
{
    struct t_cart {
        uint16_t     crt_type;
        uint8_t      max_bank;
        e_known_cart local_type;
        const char  *cart_name;
    };

    const static struct t_cart c_recognized_c64_carts[];
    const static struct t_cart c_recognized_c128_carts[];

    uint8_t     *cart_memory;
    int          bank_multiplier;
    int          machine;
    bool         a000_seen;
    e_known_cart local_type;
    uint8_t      max_bank;
    uint32_t     total_read;
    uint8_t      crt_header[0x20];

    C64_CRT(uint8_t *mem);

    int  check_header(File *f, cart_def *def);
    int  read_chip_packet(File *f);
    void clear_cart_mem(void);
    void patch_easyflash_eapi(cart_def *def);
    int  read_crt(File *file, cart_def *def);
    void configure_cart(cart_def *def);

public:
    static int load_crt(const char *path, const char *filename, cart_def *def, uint8_t *mem);
    static const char *get_error_string(int retval);
};

#endif
