#include "c64_crt.h"
#include "dump_hex.h"

extern uint8_t _eapi_65_start;

#define CRTHDR_HDRSIZE 0x10
#define CRTHDR_TYPE    0x16
#define CRTHDR_EXROM   0x18
#define CRTHDR_GAME    0x19
#define CRTHDR_SUBTYPE 0x14

#define CRTCHP_PKTLEN  0x04
#define CRTCHP_TYPE    0x08
#define CRTCHP_BANK    0x0A
#define CRTCHP_LOAD    0x0C
#define CRTCHP_SIZE    0x0E

const struct C64_CRT::t_cart C64_CRT::c_recognized_c64_carts[] = {
    {  0, CART_NORMAL,    "Normal cartridge" },
    {  1, CART_ACTION,    "Action Replay" },
    {  2, CART_KCS,       "KCS Power Cartridge" },
    {  3, CART_FINAL3,    "Final Cartridge III" },
    {  4, CART_SBASIC,    "Simons Basic" },
    {  5, CART_OCEAN,     "Ocean type 1 (256 and 128 Kb)" },
    {  6, CART_NOT_IMPL,  "Expert Cartridge" },
    {  7, CART_NOT_IMPL,  "Fun Play" },
    {  8, CART_SUPERGAMES,"Super Games" },
    {  9, CART_NORDIC,    "Atomic Power" },
    { 10, CART_EPYX,      "Epyx Fastload" },
    { 11, CART_WESTERMANN,"Westermann" },
    { 12, CART_NOT_IMPL,  "Rex" },
    { 13, CART_FINAL12,   "Final Cartridge I" },
    { 14, CART_NOT_IMPL,  "Magic Formel" },
    { 15, CART_SYSTEM3,   "C64 Game System" },
    { 16, CART_NOT_IMPL,  "Warpspeed" },
    { 17, CART_NOT_IMPL,  "Dinamic" },
    { 18, CART_NOT_IMPL,  "Zaxxon" },
    { 19, CART_DOMARK,    "Magic Desk, Domark, HES Australia" },
    { 20, CART_SUPERSNAP, "Super Snapshot 5" },
    { 21, CART_COMAL80,   "COMAL 80" },
    { 32, CART_EASYFLASH, "EasyFlash" },
    { 33, CART_NOT_IMPL,  "EasyFlash X-Bank" },
    { 34, CART_NOT_IMPL,  "Capture" },
    { 35, CART_NOT_IMPL,  "Action Replay 3" },
    { 36, CART_RETRO,     "Retro Replay" },
    { 37, CART_NOT_IMPL,  "MMC64" },
    { 38, CART_NOT_IMPL,  "MMC Replay" },
    { 39, CART_NOT_IMPL,  "IDE64" },
    { 40, CART_NOT_IMPL,  "Super Snapshot V4" },
    { 41, CART_NOT_IMPL,  "IEEE 488" },
    { 42, CART_NOT_IMPL,  "Game Killer" },
    { 43, CART_NOT_IMPL,  "Prophet 64" },
    { 44, CART_EXOS,      "EXOS" },
    { 45, CART_NOT_IMPL,  "Freeze Frame" },
    { 46, CART_NOT_IMPL,  "Freeze Machine" },
    { 47, CART_NOT_IMPL,  "Snapshot64" },
    { 48, CART_NOT_IMPL,  "Super Explode V5" },
    { 49, CART_NOT_IMPL,  "Magic Voice" },
    { 50, CART_NOT_IMPL,  "Action Replay 2" },
    { 51, CART_NOT_IMPL,  "MACH 5" },
    { 52, CART_NOT_IMPL,  "Diashow Maker" },
    { 53, CART_PAGEFOX,   "Pagefox" },
    { 54, CART_BBASIC,    "Kingsoft Business Basic" },
    { 55, CART_NOT_IMPL,  "Silver Rock 128" },
    { 56, CART_NOT_IMPL,  "Formel 64" },
    { 57, CART_NOT_IMPL,  "RGCD" },
    { 58, CART_NOT_IMPL,  "RR-Net MK3" },
    { 59, CART_NOT_IMPL,  "Easy Calc" },
    { 60, CART_NOT_IMPL,  "GMod2" },
    { 0xFFFF, CART_NOT_IMPL, "" } };

const struct C64_CRT::t_cart C64_CRT::c_recognized_c128_carts[] = {
    { 0, CART_NORMAL_C128, "C128 Cartridge" },
    { 0xFFFF, CART_NOT_IMPL, "" } };

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
    load_at_a000 = false;
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
            if (local_type == CART_NOT_IMPL) {
                printf("%s - Not implemented\n", cart_list->cart_name);
                return -4;
            }
            if (local_type == CART_KCS) {
                /* Bug in many KCS CRT images, wrong header size */
                crt_header[CRTHDR_HDRSIZE] = crt_header[CRTHDR_HDRSIZE+1] = crt_header[CRTHDR_HDRSIZE+2] = 0;
                crt_header[CRTHDR_HDRSIZE+3] = 0x40;
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

    if (bank > 0x7f)
        return -5;

    if ((local_type == CART_OCEAN) && (load == 0xA000))
        bank &= 0xEF; // max 128k per region

    /*
     if (bank > max_bank)
     max_bank = bank;
     */

    bool split = false;
    if (size > 0x2000)
        if ((local_type == CART_OCEAN) || (local_type == CART_EASYFLASH) || (local_type == CART_DOMARK)
                || (local_type == CART_SYSTEM3))
            split = true;

    uint8_t *mem_addr = cart_memory;

    if (local_type == CART_KCS) {
        mem_addr += load - 0x8000;
    } else if (local_type == CART_NORMAL) {
        mem_addr += (load & 0x2000); // use bit 13 of the load address
    } else if (local_type == CART_FINAL3) {
        mem_addr += 0x4000 * uint32_t(bank) + (load & 0x2000);
    } else if (split) {
        mem_addr += 0x2000 * uint32_t(bank);
    } else {
        mem_addr += uint32_t(size) * uint32_t(bank);
    }

    if (load == 0xA000 && local_type != CART_KCS && local_type != CART_FINAL3) {
        mem_addr += 512 * 1024; // interleaved mode (TODO: make it the same in hardware as well, currently only for EasyFlash)
        load_at_a000 = true;
    }
    printf("Reading chip data bank %d to $%4x with size $%4x to 0x%8x. %s\n", bank, load, size, mem_addr,
            (split) ? "Split" : "Contiguous");

    if (size) {
        if (split) {
            res = f->read((void *)mem_addr, 0x2000, &bytes_read);
            total_read += uint32_t(bytes_read);
            if (bytes_read != 0x2000) {
                printf("Just read %4x bytes, expecting 8K\n", bytes_read);
                return -5;
            }
            mem_addr += 512 * 1024; // interleaved
            res = f->read((void *)mem_addr, size - 0x2000, &bytes_read);
            total_read += bytes_read;
        } else {
            res = f->read((void *)mem_addr, size, &bytes_read);
            total_read += bytes_read;
            if (bytes_read != size) {
                printf("Just read %4x bytes\n", bytes_read);
                return -5;
            }
        }
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
        uint8_t* eapi = cart_memory + 512 * 1024 + 0x1800;
        if (eapi[0] == 0x65 && eapi[1] == 0x61 && eapi[2] == 0x70 && eapi[3] == 0x69) {
            memcpy(eapi, &_eapi_65_start, 768);
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
                cart_type = CART_TYPE_16K_UMAX;
            } else if ((crt_header[CRTHDR_EXROM] == 0) && (crt_header[CRTHDR_GAME] == 0)) {
                cart_type = CART_TYPE_16K;
            } else if ((crt_header[CRTHDR_EXROM] == 0) && (crt_header[CRTHDR_GAME] == 1)) {
                cart_type = CART_TYPE_8K;
            }
            break;
        case CART_ACTION:
            cart_type = CART_TYPE_ACTION;
            break;
        case CART_RETRO:
            cart_type = CART_TYPE_RETRO;
            break;
        case CART_DOMARK:
            cart_type = CART_TYPE_DOMARK;
            break;
        case CART_OCEAN:
            if (load_at_a000) {
                cart_type = CART_TYPE_OCEAN256; // Ocean 256K
            } else {
                cart_type = CART_TYPE_OCEAN128; // Ocean 128K/512K
            }
            break;
        case CART_EASYFLASH:
            cart_type = CART_TYPE_EASY_FLASH; // EasyFlash
            require = CART_UCI_DE1C;
            break;
        case CART_SUPERSNAP:
            cart_type = CART_TYPE_SS5; // Snappy
            break;
        case CART_EPYX:
            cart_type = CART_TYPE_EPYX; // Epyx
            break;
        case CART_FINAL3:
            cart_type = CART_TYPE_FC3PLUS; // Final3 TODO: Check with HW
            break;
        case CART_SYSTEM3:
            cart_type = CART_TYPE_SYSTEM3; // System3
            break;
        case CART_KCS:
            cart_type = CART_TYPE_KCS; // System3
            break;
        case CART_FINAL12:
            cart_type = CART_TYPE_FINAL12; // System3
            break;
        case CART_COMAL80:
            if (total_read > 65536)
                cart_type = CART_TYPE_COMAL80PAKMA; // Comal 80 Pakma
            else
                cart_type = CART_TYPE_COMAL80; // Comal 80
            break;
        case CART_SBASIC:
            cart_type = CART_TYPE_SBASIC; // Simons Basic
            break;
        case CART_WESTERMANN:
            cart_type = CART_TYPE_WESTERMANN; // Westermann
            break;
        case CART_BBASIC:
            cart_type = CART_TYPE_BBASIC; // Business Basic
            break;
        case CART_PAGEFOX:
            cart_type = CART_TYPE_PAGEFOX; // Business Basic
            break;
        case CART_SUPERGAMES:
            cart_type = CART_TYPE_SUPERGAMES; // Super Games, 16K banks
            break;
        case CART_NORDIC:
            cart_type = CART_TYPE_NORDIC;
            require |= CART_WMIRROR;
            break;
        case CART_EXOS:
            require |= CART_KERNAL;
            break;
            /*
             if (total_read > 8192) {
             C64_KERNAL_ENABLE = 3;
             uint8_t *src = (uint8_t *) (((uint32_t)C64_CARTRIDGE_ROM_BASE) << 16);
             for (int i = 16383; i >= 0; i--)
             *(src + 2 * i + 1) = *(src + i);
             } else {
             uint8_t *src = (uint8_t *) (((uint32_t)C64_CARTRIDGE_ROM_BASE) << 16);
             int fastreset = C64 :: getMachine()->get_cfg_value(CFG_C64_FASTRESET);
             C64 :: getMachine()->enable_kernal(src, fastreset);
             }
             break;
             */
        case CART_NORMAL_C128:
            if (crt_header[CRTHDR_SUBTYPE] == 1) {
                cart_type = CART_TYPE_128; // witH IO: TODO
            } else {
                cart_type = CART_TYPE_128;
            }
            break;

        default:
            break;
    }
    printf("*%b*\n", cart_type);

    def->type = cart_type;
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
