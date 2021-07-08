#include "c64_crt.h"
#include "dump_hex.h"

extern uint8_t _eapi_65_start;

#define CRTHDR_HDRSIZE 0x10
#define CRTHDR_TYPE    0x16
#define CRTHDR_EXROM   0x18
#define CRTHDR_GAME    0x19
#define CRTHDR_SUBTYPE 0x1A

#define CRTCHP_PKTLEN  0x04
#define CRTCHP_TYPE    0x08
#define CRTCHP_BANK    0x0A
#define CRTCHP_LOAD    0x0C
#define CRTCHP_SIZE    0x0E

const struct C64_CRT::t_cart C64_CRT::c_recognized_c64_carts[] = {
    {  0, 0xFF, CART_NORMAL,    "Normal cartridge" },
    {  1, 0xFF, CART_ACTION,    "Action Replay" }, // max 4 banks of 8K
    {  2, 0xFF, CART_KCS,       "KCS Power Cartridge" },
    {  3, 0xFF, CART_FINAL3,    "Final Cartridge III" }, // max 16 banks (FC3+)
    {  4, 0xFF, CART_SBASIC,    "Simons Basic" },
    {  5, 0xFF, CART_OCEAN_8K,  "Ocean type 1" }, // max 64 banks of 8K, with the exception of the 16K carts, which are limited to 16 banks of 16K
    {  6, 0xFF, CART_NOT_IMPL,  "Expert Cartridge" },
    {  7, 0xFF, CART_NOT_IMPL,  "Fun Play" },
    {  8, 0xFF, CART_SUPERGAMES,"Super Games" },
    {  9, 0xFF, CART_NORDIC,    "Atomic Power" }, // max 8 banks of 8K
    { 10, 0xFF, CART_EPYX,      "Epyx Fastload" },
    { 11, 0xFF, CART_WESTERMANN,"Westermann" },
    { 12, 0xFF, CART_NOT_IMPL,  "Rex" },
    { 13, 0xFF, CART_FINAL12,   "Final Cartridge I" },
    { 14, 0xFF, CART_NOT_IMPL,  "Magic Formel" },
    { 15, 0xFF, CART_SYSTEM3,   "C64 Game System" },
    { 16, 0xFF, CART_NOT_IMPL,  "Warpspeed" },
    { 17, 0xFF, CART_NOT_IMPL,  "Dinamic" },
    { 18, 0xFF, CART_ZAXXON,    "Zaxxon" },
    { 19, 0xFF, CART_DOMARK,    "Magic Desk, Domark, HES Australia" }, // max 16 banks of 16K
    { 20, 0xFF, CART_SUPERSNAP, "Super Snapshot 5" },
    { 21, 0xFF, CART_COMAL80,   "COMAL 80" },
    { 32, 0xFF, CART_EASYFLASH, "EasyFlash" }, // max 64 banks
    { 33, 0xFF, CART_NOT_IMPL,  "EasyFlash X-Bank" },
    { 34, 0xFF, CART_NOT_IMPL,  "Capture" },
    { 35, 0xFF, CART_NOT_IMPL,  "Action Replay 3" },
    { 36, 0xFF, CART_RETRO,     "Retro Replay" }, // max 8 banks of 8K
    { 37, 0xFF, CART_NOT_IMPL,  "MMC64" },
    { 38, 0xFF, CART_NOT_IMPL,  "MMC Replay" },
    { 39, 0xFF, CART_NOT_IMPL,  "IDE64" },
    { 40, 0xFF, CART_NOT_IMPL,  "Super Snapshot V4" },
    { 41, 0xFF, CART_NOT_IMPL,  "IEEE 488" },
    { 42, 0xFF, CART_NOT_IMPL,  "Game Killer" },
    { 43, 0xFF, CART_NOT_IMPL,  "Prophet 64" },
    { 44, 0xFF, CART_EXOS,      "EXOS" }, // Currently max 1 bank
    { 45, 0xFF, CART_NOT_IMPL,  "Freeze Frame" },
    { 46, 0xFF, CART_NOT_IMPL,  "Freeze Machine" },
    { 47, 0xFF, CART_NOT_IMPL,  "Snapshot64" },
    { 48, 0xFF, CART_NOT_IMPL,  "Super Explode V5" },
    { 49, 0xFF, CART_NOT_IMPL,  "Magic Voice" },
    { 50, 0xFF, CART_NOT_IMPL,  "Action Replay 2" },
    { 51, 0xFF, CART_NOT_IMPL,  "MACH 5" },
    { 52, 0xFF, CART_NOT_IMPL,  "Diashow Maker" },
    { 53, 0xFF, CART_PAGEFOX,   "Pagefox" },
    { 54, 0xFF, CART_BBASIC,    "Kingsoft Business Basic" },
    { 55, 0xFF, CART_NOT_IMPL,  "Silver Rock 128" },
    { 56, 0xFF, CART_NOT_IMPL,  "Formel 64" },
    { 57, 0xFF, CART_NOT_IMPL,  "RGCD" },
    { 58, 0xFF, CART_NOT_IMPL,  "RR-Net MK3" },
    { 59, 0xFF, CART_NOT_IMPL,  "Easy Calc" },
    { 60, 0xFF, CART_GMOD2,     "GMod2" },
    { 61, 0xFF, CART_NOT_IMPL,  "MAX Basic" },
    { 62, 0xFF, CART_NOT_IMPL,  "GMod3" },
    { 63, 0xFF, CART_NOT_IMPL,  "ZIPP-CODE 48" },
    { 64, 0xFF, CART_BLACKBOX8, "Blackbox V8" },
    { 65, 0xFF, CART_BLACKBOX3, "Blackbox V3" },
    { 66, 0xFF, CART_BLACKBOX4, "Blackbox V4" },
    { 67, 0xFF, CART_NOT_IMPL,  "REX RAM-Floppy" },
    { 68, 0xFF, CART_NOT_IMPL,  "BIS-Plus" },
    { 69, 0xFF, CART_NOT_IMPL,  "SD-BOX" },
    { 70, 0xFF, CART_NOT_IMPL,  "MultiMAX" },
    { 71, 0xFF, CART_NOT_IMPL,  "Blackbox V9" },
    { 72, 0xFF, CART_NOT_IMPL,  "Lt. Kernal Host Adaptor" },
    { 73, 0xFF, CART_NOT_IMPL,  "RAMLink" },
    { 74, 0xFF, CART_NOT_IMPL,  "H.E.R.O." },
    { 0xFFFF, 0xFF, CART_NOT_IMPL, "" } };

const struct C64_CRT::t_cart C64_CRT::c_recognized_c128_carts[] = {
    { 0, 0xFF, CART_C128_STD,    "C128 Cartridge" },
    { 1, 0xFF, CART_C128_STD_IO, "C128 Cartridge with I/O Mirror" },
    { 0xFFFF, 0xFF, CART_NOT_IMPL, "" } };

__inline static uint16_t get_word(uint8_t *p)
{
    return (uint16_t(p[0]) << 8) + p[1];
}

__inline static uint32_t get_dword(uint8_t *p)
{
    return uint32_t(get_word(p) << 16) + get_word(p + 2);
}

C64_CRT::C64_CRT(uint8_t *mem)
{
    cart_memory = mem;
    local_type = CART_NOT_IMPL;
    machine = 0;
    total_read = 0;
    max_bank = 0xFF;
    a000_seen = false;
    bank_multiplier = 16 * 1024;
}

int C64_CRT::check_header(File *f, cart_def *def)
{
    uint32_t bytes_read = 0;
    FRESULT res = f->read(crt_header, 0x20, &bytes_read);
    if (bytes_read != 0x20) {
        return -2; // unable to read file header
    }
    machine = 0;
    const struct t_cart *cart_list;
    if (strncmp((char*)crt_header, "C64 CARTRIDGE   ", 16) == 0) {
        machine = 64;
        cart_list = c_recognized_c64_carts;
    } else if (strncmp((char*)crt_header, "C128 CARTRIDGE  ", 16) == 0) {
        machine = 128;
        cart_list = c_recognized_c128_carts;
    } else {
        return -3; // unrecognized file header
    }

    uint16_t hw_type = get_word(crt_header + CRTHDR_TYPE);
    printf("CRT Hardware type: %d ", hw_type);
    local_type = CART_NOT_IMPL;

    while (cart_list->crt_type != 0xFFFF) {
        if (uint8_t(cart_list->crt_type) == hw_type) {
            def->name = cart_list->cart_name;
            local_type = cart_list->local_type;
            max_bank = cart_list->max_bank;
            if (local_type == CART_NOT_IMPL) {
                printf("%s - Not implemented\n", cart_list->cart_name);
                return -4;
            }
            return 0;
        }
        cart_list++;
    }
    return -4;
}

int C64_CRT::read_chip_packet(File *f)
{
    uint8_t chip_header[0x10];

    uint32_t bytes_read;
    FRESULT res = f->read(chip_header, 0x10, &bytes_read);
    // dump_hex_relative(chip_header, 0x10);
    if ((res != FR_OK) || (bytes_read != 0x10)) {
        return 1; // stop, no error
    }
    if (strncmp((char*)chip_header, "CHIP", 4)) {
        return 1; // stop, no error
    }

    uint32_t total_length = get_dword(chip_header + CRTCHP_PKTLEN);
    uint16_t bank = get_word(chip_header + CRTCHP_BANK);
    uint16_t load = get_word(chip_header + CRTCHP_LOAD);
    uint16_t size = get_word(chip_header + CRTCHP_SIZE);
    uint16_t type = get_word(chip_header + CRTCHP_TYPE);

    // Detect C128 mode carts
    if ((load == 0xC000) || (size == 0x8000)) {
        bank_multiplier = 32 * 1024;
    }

    if ((load == 0xA000) && !a000_seen) {
        a000_seen = true;
        if (bank > 0) { // strange; first time A000 is seen, it is not bank 0.
            max_bank = bank - 1;
        }
    }

    bank &= max_bank;

    uint32_t offset = uint32_t(bank) * bank_multiplier;
    offset += (load & 0x3FFF); // switch between 8000 and A000
    if (offset > 0x1000000) { // max 1 MB
        return -5;
    }

    // Valid for ROM only
    uint8_t *mem_addr = cart_memory;
    mem_addr += offset; // by default 16K; unless it is a C128 cart.
    printf("Reading chip data bank %d ($%4x) with size $%4x to 0x%8x.\n", bank, load, size, mem_addr);

    if (size && ((type == 0) || (type == 2))) {
        res = f->read(mem_addr, size, &bytes_read);
        total_read += bytes_read;
        if (bytes_read != size) {
            printf("Just read %4x bytes\n", bytes_read);
            return -5;
        }
        if (bytes_read < 8192) {
            // round up to the nearest multiple of 2, minimum 256 bytes
            for (int i=256; i<size; i*=2) {
                if (bytes_read < i) {
                    bytes_read = i;
                    break;
                }
            }
            // mirror data up to 8K block
            while (bytes_read < 8192) {
                printf("Mirroring %4x bytes from %p to %p.\n", bytes_read, mem_addr, mem_addr + bytes_read);
                memcpy(mem_addr + bytes_read, mem_addr, bytes_read);
                bytes_read <<= 1;
            }
        }
    } else {
        printf("Skipping Chip packet Type %d with size %4x.\n", type, size);
    }
    return 0;
}

void C64_CRT::clear_cart_mem(void)
{
    memset(cart_memory, 0xff, 1024 * 1024); // clear all cart memory
}

void C64_CRT::patch_easyflash_eapi(cart_def *def)
{
    if (local_type == CART_EASYFLASH) {
        uint8_t* eapi = cart_memory + 0x3800;
        if (eapi[0] == 0x65 && eapi[1] == 0x61 && eapi[2] == 0x70 && eapi[3] == 0x69) {
            memcpy(eapi, &_eapi_65_start, 768);
            printf("EAPI successfully patched!\n");
        }
    }
}

int C64_CRT::read_crt(File *file, cart_def *def)
{
    clear_cart_mem();

    int retval = check_header(file, def); // pointer is now at 0x20
    if (retval) { // something went wrong
        return retval;
    }
    uint32_t dw = get_dword(crt_header + CRTHDR_HDRSIZE);
    if (dw < 0x40) {
        dw = 0x40;
    }

    printf("Header OK. Now reading chip packets starting from %6x.\n", dw);
    FRESULT fres = file->seek(dw);
    if (fres != FR_OK) {
        return -5;
    }

    do {
        retval = read_chip_packet(file);
    } while (!retval);
    if (retval < 0) {
        return retval;
    }

    patch_easyflash_eapi(def);
    configure_cart(def);
    return 0;
}

void C64_CRT::configure_cart(cart_def *def)
{
    printf("Total ROM size read: %6x bytes.\n", total_read);

    uint16_t cart_type = CART_TYPE_NONE;
    uint16_t require = 0;
    uint16_t prohibit = 0;

    switch (local_type) {
        case CART_NORMAL:
            if ((crt_header[CRTHDR_EXROM] == 1) && (crt_header[CRTHDR_GAME] == 0)) {
                cart_type = CART_TYPE_UMAX;
            } else if ((crt_header[CRTHDR_EXROM] == 0) && (crt_header[CRTHDR_GAME] == 0)) {
                cart_type = CART_TYPE_16K;
            } else if ((crt_header[CRTHDR_EXROM] == 0) && (crt_header[CRTHDR_GAME] == 1)) {
                cart_type = CART_TYPE_8K;
            }
            break;
        case CART_ACTION:
            cart_type = CART_TYPE_ACTION;
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_RETRO:
            cart_type = CART_TYPE_RETRO;
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_DOMARK:
            cart_type = CART_TYPE_DOMARK;
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_OCEAN_8K:
            prohibit = CART_PROHIBIT_DEXX;
            if (a000_seen) { // special case!
                cart_type = CART_TYPE_OCEAN_16K | VARIANT_3; // 16 banks of 16K
            } else {
                cart_type = CART_TYPE_OCEAN_8K;
            }
            break;
        case CART_EASYFLASH:
            cart_type = CART_TYPE_EASY_FLASH; // EasyFlash
            prohibit = CART_PROHIBIT_ALL_BUT_REU;
            require = CART_UCI_DE1C;
            break;
        case CART_SUPERSNAP:
            cart_type = CART_TYPE_SS5; // Snappy
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_ZAXXON:
            cart_type = CART_TYPE_ZAXXON;
            // in this special case, the first chip chunk should be copied to both banks
            // the read chip chunk routine should already have doubled the 4K to 8K.
            printf("Mirroring 2000 bytes from %p to %p.\n", cart_memory, cart_memory + 0x4000);
            memcpy(cart_memory + 0x4000, cart_memory, 0x2000);
            break;
        case CART_EPYX:
            cart_type = CART_TYPE_EPYX; // Epyx
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_FINAL3:
            cart_type = CART_TYPE_FC3; // Final3
            if (crt_header[CRTHDR_SUBTYPE] == 1) {
                prohibit = CART_PROHIBIT_ALL_BUT_REU;
            } else {
                prohibit = CART_PROHIBIT_IO;
            }
            if (total_read > 65536) {
                cart_type |= VARIANT_1;
            }
            break;
        case CART_SYSTEM3:
            cart_type = CART_TYPE_SYSTEM3; // System3
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_KCS:
            cart_type = CART_TYPE_KCS; // System3
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_FINAL12:
            cart_type = CART_TYPE_FINAL12;
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_COMAL80:
            if (total_read > 65536) {
                cart_type = CART_TYPE_OCEAN_16K | VARIANT_1; // Comal 80 Pakma // V5: Type = Ocean_16K, Variant = 1
            } else {
                cart_type = CART_TYPE_OCEAN_16K; // Comal 80 // V5: Type = Ocean_16K, Variant = 0
            }
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_SBASIC:
            cart_type = CART_TYPE_SBASIC; // Simons Basic
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_WESTERMANN:
            cart_type = CART_TYPE_WESTERMANN; // Westermann
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_BBASIC:
            cart_type = CART_TYPE_BBASIC; // Business Basic
            prohibit = CART_PROHIBIT_DEXX;
            break;
        case CART_PAGEFOX:
            cart_type = CART_TYPE_PAGEFOX; // Pagefox
            // No prohibits!
            break;
        case CART_SUPERGAMES:
            cart_type = CART_TYPE_SUPERGAMES; // Super Games, 16K banks
            prohibit = CART_PROHIBIT_DFXX;
            break;
        case CART_NORDIC:
            cart_type = CART_TYPE_NORDIC;
            require = CART_WMIRROR | CART_DYNAMIC;
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_BLACKBOX3:
            cart_type = CART_TYPE_BLACKBOX_V3;
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_BLACKBOX4:
            cart_type = CART_TYPE_BLACKBOX_V4;
            prohibit = CART_PROHIBIT_IO;
            break;
        case CART_BLACKBOX8:
            cart_type = CART_TYPE_BLACKBOX_V8;
            prohibit = CART_PROHIBIT_DFXX;
            break;
        case CART_GMOD2:
            prohibit = CART_PROHIBIT_DEXX;
            cart_type = CART_TYPE_GMOD2;
            break;
        case CART_EXOS:
            require = CART_KERNAL;
            break;
        case CART_C128_STD:
            cart_type = CART_TYPE_128;
            break;

        case CART_C128_STD_IO:
            cart_type = CART_TYPE_128 | VARIANT_3; // with IO

            if (crt_header[CRTHDR_SUBTYPE] == 1) {
                prohibit = CART_PROHIBIT_ALL_BUT_REU;
                require = CART_UCI;
            } else {
                prohibit = CART_PROHIBIT_IO;
            }
            break;

        default:
            break;
    }
    printf("*%b*\n", cart_type);

    def->type = cart_type;
    def->prohibit = prohibit;
    def->require = require;
}

int C64_CRT::load_crt(const char *path, const char *filename, cart_def *def, uint8_t *mem)
{
    File *file = 0;
    FileInfo *inf;
    FileManager *fm = FileManager::getFileManager();

    memset(def, 0, sizeof(cart_def));
    def->type = CART_TYPE_NONE;
    def->name = "None";

    FRESULT fres = fm->fopen(path, filename, FA_READ, &file);
    if (fres != FR_OK) {
        return -1; // cannot read file
    }

    C64_CRT work(mem);
    int retval = work.read_crt(file, def);
    fm->fclose(file);
    return retval;
}

const char *C64_CRT::get_error_string(int retval)
{
    const char *errors[] = { "", "Unable to open file", "Unable to read header", "Unrecognized header",
            "Not Implemented", "Error reading chip packet" };
    return errors[-retval];
}
