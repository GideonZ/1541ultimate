#ifndef _C64_CRT_H
#define _C64_CRT_H

#include "filemanager.h"
#include "c64.h"
#include "subsys.h"

#define CARTS_DIRECTORY "/flash/carts"

// Magic Desk Plus keeps its non-volatile memory in the CRT file, in chunks at
// DF00, the address of the window they are reached through. The bank field says
// which piece a chunk is, because the size field cannot: it is 16 bits, so the
// 128K of SRAM does not fit in one chunk and is carried in four.
//
//   bank 0      the EEPROM, 8K or 32K, the two sizes the format allows
//   bank 1..4   the SRAM, in quarters, in address order
//
// The cartridge logic reaches the EEPROM in the first half of its area and the
// SRAM in the second.
#define MDPLUS_EEPROM_8K     0x2000
#define MDPLUS_EEPROM_32K    0x8000
#define MDPLUS_EEPROM_AREA   0x20000
#define MDPLUS_SRAM_CHUNK    0x8000
#define MDPLUS_SRAM_CHUNKS   4
#define MDPLUS_SRAM_OFFSET   0x20000
#define MDPLUS_BANK_EEPROM   0

// Local definitions, NOT hardware select!
typedef enum {
    CART_NOT_IMPL,
    CART_NORMAL,
    CART_ACTION,
    CART_RETRO,
    CART_DOMARK,
    CART_MDPLUS,
    CART_OCEAN_8K,
    CART_OCEAN_16K,
    CART_EASYFLASH,
    CART_SUPERSNAP,
    CART_EPYX,
    CART_FINAL3,
    CART_SYSTEM3,
    CART_ZAXXON,
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
    CART_BLACKBOX9,
    CART_MEGABYTER,
    CART_TWOMEGABYTER,
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

    struct t_crt_chip_chunk
    {
        uint8_t header[0x10];
        uint8_t *ram_location;
        bool mandatory; // if true, then it is saved always - even when empty. Required for GMOD2, used for everything non easyflash.
        bool last;
    };

    const static struct t_cart c_recognized_c64_carts[];
    const static struct t_cart c_recognized_c128_carts[];

    // Local Variables
    uint8_t     *cart_memory;
    uint32_t     max_cart;
    int          bank_multiplier;
    int          machine; // 64 or 128
    bool         a000_seen;
    uint8_t      mdp_sram_parts; // bit per Magic Desk Plus SRAM chunk seen
    uint32_t     mdp_eeprom_size; // size of its EEPROM image, 0 when it has none
    e_known_cart local_type;
    uint8_t      max_bank;
    uint8_t      highest_bank;
    uint32_t     total_read;

    // CRT File Structure
    uint8_t      crt_header[0x40]; // includes name field
    IndexedList<t_crt_chip_chunk *>chip_chunks;

    // Auxiliary Data chunks
    uint8_t     *eeprom_buffer;
    int          eeprom_size;
    uint8_t     *original_eapi;

    static C64_CRT *get_instance(void); // singleton

    C64_CRT();
    void initialize(uint8_t *mem, uint32_t max_size);
    void cleanup(void);

    SubsysResultCode_e check_header(File *f, cart_def *def);
    SubsysResultCode_e read_chip_packet(File *f, t_crt_chip_chunk *chunk);
    void clear_cart_mem(void);
    void patch_easyflash_eapi();
    void unpatch_easyflash_eapi();
    void regenerate_easyflash_chunks();
    SubsysResultCode_e read_crt(File *file, cart_def *def);
    void configure_cart(cart_def *def);
    void find_eeprom(void);
    void auto_mirror(void);
public:
    static SubsysResultCode_e load_crt(const char *path, const char *filename, cart_def *def, uint8_t *mem);
    static SubsysResultCode_e save_crt(File *f);
    static int clear_crt(void);
    static bool is_valid(void);

    static void clear_definition(cart_def *def);
};

#endif
