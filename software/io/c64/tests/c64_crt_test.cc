// Host test for the CRT loader (C64_CRT::load_crt).
//
// For every CRT type the firmware accepts, it checks which cartridge logic and
// I/O restrictions the loader selects, where each chip lands in cartridge
// memory, and how the image is repeated over the whole cartridge region. The
// FPGA reads that memory directly, so a change here changes what every
// cartridge of the affected types sees. The CRT images are built in code.
//
// Results go to stderr; the loader's own log goes to stdout.

#include "c64_crt.h"

uint8_t _eapi_65_start[768];
uint32_t host_fpga_capabilities = CAPAB_EEPROM;
uint32_t host_cart_max_rom = 0;
uint8_t host_reu_memory[256 * 1024];

static const uint32_t K = 1024;
static const uint32_t MB = 1024 * K;
static const uint32_t GUARD = 64 * K;
static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

struct Chip {
    uint16_t bank;
    uint16_t load;
    uint16_t type;
    std::vector<uint8_t> data;
};

// Every chip gets its own bytes, so a misplaced or missing chip is visible.
static std::vector<uint8_t> payload(uint32_t seed, size_t size)
{
    std::vector<uint8_t> d(size);
    uint32_t x = seed * 2654435761u + 1;
    for (auto &b : d) {
        x = x * 1103515245u + 12345u;
        b = uint8_t(x >> 16);
    }
    return d;
}

struct Crt {
    int machine = 64;
    uint16_t hw = 0;
    uint8_t exrom = 0;
    uint8_t game = 0;
    uint8_t subtype = 0;
    std::vector<Chip> chips;
    std::vector<uint8_t> trailer;

    Crt(uint16_t hw_type, int for_machine = 64) : machine(for_machine), hw(hw_type) {}

    Crt &chip(uint16_t bank, uint16_t load, uint16_t size, uint16_t type = 0) {
        chips.push_back({ bank, load, type, payload(uint32_t(chips.size()) * 131 + bank * 7 + load + hw * 1009, size) });
        return *this;
    }
    Crt &banks(int n, uint16_t load, uint16_t size) {
        for (int b = 0; b < n; b++) {
            chip(b, load, size);
        }
        return *this;
    }
    Crt &header(uint8_t e, uint8_t g, uint8_t s) {
        exrom = e;
        game = g;
        subtype = s;
        return *this;
    }

    std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out(0x40, 0);
        memcpy(out.data(), machine == 128 ? "C128 CARTRIDGE  " : "C64 CARTRIDGE   ", 16);
        out[0x13] = 0x40;
        out[0x14] = 1;
        out[0x16] = hw >> 8;
        out[0x17] = hw & 0xFF;
        out[0x18] = exrom;
        out[0x19] = game;
        out[0x1A] = subtype;
        for (const Chip &c : chips) {
            uint32_t len = 0x10 + c.data.size();
            const uint8_t h[16] = { 'C', 'H', 'I', 'P', uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len),
                                    uint8_t(c.type >> 8), uint8_t(c.type), uint8_t(c.bank >> 8), uint8_t(c.bank),
                                    uint8_t(c.load >> 8), uint8_t(c.load), uint8_t(c.data.size() >> 8), uint8_t(c.data.size()) };
            out.insert(out.end(), h, h + 16);
            out.insert(out.end(), c.data.begin(), c.data.end());
        }
        out.insert(out.end(), trailer.begin(), trailer.end());
        return out;
    }
};

struct Loaded {
    SubsysResultCode_e rc;
    uint16_t type;
    uint16_t prohibit;
    uint16_t require;
    std::vector<uint8_t> mem;   // cartridge region followed by a guard band
    bool guard_intact;

    const uint8_t *at(uint32_t offset) const { return mem.data() + offset; }
};

static Loaded load(const Crt &crt, uint32_t max_rom)
{
    FileManager::getFileManager()->files["test.crt"] = crt.bytes();
    host_cart_max_rom = max_rom;
    Loaded r;
    r.mem.assign(max_rom + GUARD, 0x5A);
    cart_def def;
    r.rc = C64_CRT::load_crt("/", "test.crt", &def, r.mem.data());
    r.type = def.type;
    r.prohibit = def.prohibit;
    r.require = def.require;
    r.guard_intact = std::all_of(r.mem.begin() + max_rom, r.mem.end(), [](uint8_t b) { return b == 0x5A; });
    return r;
}

static bool all_ff(const uint8_t *p, uint32_t n)
{
    return std::all_of(p, p + n, [](uint8_t b) { return b == 0xFF; });
}

// A 256K Ocean image as VICE's cartconv writes it: banks 0 to 15 at $8000 and
// banks 16 to 31 at $A000. The chips at $A000 need 16K mode.
static Crt ocean_256k(void)
{
    Crt crt(5);
    for (int b = 0; b < 32; b++) {
        crt.chip(b, b < 16 ? 0x8000 : 0xA000, 0x2000);
    }
    return crt;
}

// The cartridge logic and I/O restrictions each CRT type selects.
static void test_cartridge_types(void)
{
    struct Row {
        const char *what;
        Crt crt;
        uint16_t type;
        uint16_t prohibit;
        uint16_t require;
    };
    const int SMALL = 4;    // 32K of 8K chips
    const int LARGE = 16;   // 128K of 8K chips, above the 64K variant threshold
    Row rows[] = {
        { "normal 16K",              Crt(0).header(0, 0, 0).banks(1, 0x8000, 0x4000),     CART_TYPE_16K,             0, 0 },
        { "normal 8K",               Crt(0).header(0, 1, 0).banks(1, 0x8000, 0x2000),     CART_TYPE_8K,              0, 0 },
        { "normal Ultimax",          Crt(0).header(1, 0, 0).chip(0, 0xE000, 0x2000),      CART_TYPE_UMAX,            0, 0 },
        { "normal, both lines high", Crt(0).header(1, 1, 0).banks(1, 0x8000, 0x2000),     CART_TYPE_NONE,            0, 0 },
        { "Action Replay",           Crt(1).banks(SMALL, 0x8000, 0x2000),                 CART_TYPE_ACTION,          CART_PROHIBIT_IO, 0 },
        { "KCS Power Cartridge",     Crt(2).banks(2, 0x8000, 0x2000),                     CART_TYPE_KCS,             CART_PROHIBIT_IO, 0 },
        { "Final Cartridge III",     Crt(3).banks(SMALL, 0x8000, 0x4000),                 CART_TYPE_FC3,             CART_PROHIBIT_IO, 0 },
        { "Final Cartridge III, subtype 1", Crt(3).header(0, 0, 1).banks(SMALL, 0x8000, 0x4000), CART_TYPE_FC3,     CART_PROHIBIT_ALL_BUT_REU, 0 },
        { "Final Cartridge III+",    Crt(3).banks(LARGE, 0x8000, 0x4000),                 CART_TYPE_FC3PLUS,         CART_PROHIBIT_IO, 0 },
        { "Simons Basic",            Crt(4).banks(2, 0x8000, 0x2000),                     CART_TYPE_SBASIC,          CART_PROHIBIT_DEXX, 0 },
        { "Ocean, chips at $8000",   Crt(5).banks(LARGE, 0x8000, 0x2000),                 CART_TYPE_OCEAN_8K,        CART_PROHIBIT_DEXX, 0 },
        { "Ocean, 256K with chips at $A000", ocean_256k(),                                CART_TYPE_OCEAN_16K | VARIANT_3, CART_PROHIBIT_DEXX, 0 },
        { "Super Games",             Crt(8).banks(SMALL, 0x8000, 0x4000),                 CART_TYPE_SUPERGAMES,      CART_PROHIBIT_DFXX, 0 },
        { "Atomic Power",            Crt(9).banks(SMALL, 0x8000, 0x2000),                 CART_TYPE_NORDIC,          CART_PROHIBIT_IO, CART_WMIRROR | CART_DYNAMIC },
        { "Epyx FastLoad",           Crt(10).banks(1, 0x8000, 0x2000),                    CART_TYPE_EPYX,            CART_PROHIBIT_IO, 0 },
        { "Westermann",              Crt(11).banks(1, 0x8000, 0x4000),                    CART_TYPE_WESTERMANN,      CART_PROHIBIT_IO, 0 },
        { "Final Cartridge I",       Crt(13).banks(1, 0x8000, 0x4000),                    CART_TYPE_FINAL12,         CART_PROHIBIT_IO, 0 },
        { "C64 Game System",         Crt(15).banks(64, 0x8000, 0x2000),                   CART_TYPE_SYSTEM3,         CART_PROHIBIT_DEXX, 0 },
        { "Zaxxon",                  Crt(18).chip(0, 0x8000, 0x1000).chip(0, 0xA000, 0x2000).chip(1, 0xA000, 0x2000), CART_TYPE_ZAXXON, 0, 0 },
        { "Magic Desk",              Crt(19).banks(LARGE, 0x8000, 0x2000),                CART_TYPE_DOMARK,          CART_PROHIBIT_DEXX, 0 },
        { "Super Snapshot 5",        Crt(20).banks(SMALL, 0x8000, 0x4000),                CART_TYPE_SS5,             CART_PROHIBIT_DEXX, 0 },
        { "Super Snapshot 5, 128K",  Crt(20).banks(8, 0x8000, 0x4000),                    CART_TYPE_SS5 | VARIANT_1, CART_PROHIBIT_DEXX, 0 },
        { "COMAL 80",                Crt(21).banks(SMALL, 0x8000, 0x4000),                CART_TYPE_OCEAN_16K,       CART_PROHIBIT_DEXX, 0 },
        { "COMAL 80, 128K",          Crt(21).banks(8, 0x8000, 0x4000),                    CART_TYPE_OCEAN_16K | VARIANT_1, CART_PROHIBIT_DEXX, 0 },
        { "COMAL 80 grey, subtype 1", Crt(21).header(0, 0, 1).banks(SMALL, 0x8000, 0x4000), CART_TYPE_OCEAN_16K | VARIANT_2, CART_PROHIBIT_DEXX, 0 },
        { "COMAL 80 grey, 128K",     Crt(21).header(0, 0, 1).banks(8, 0x8000, 0x4000),    CART_TYPE_OCEAN_16K | VARIANT_2, CART_PROHIBIT_DEXX, 0 },
        { "EasyFlash",               Crt(32).header(1, 0, 0).banks(SMALL, 0x8000, 0x2000), CART_TYPE_EASY_FLASH,     CART_PROHIBIT_IO, CART_UCI_DE1C },
        { "EasyFlash, subtype 1",    Crt(32).header(1, 0, 1).banks(SMALL, 0x8000, 0x2000), CART_TYPE_EASY_FLASH,     CART_PROHIBIT_ALL_BUT_REU, CART_UCI_DE1C },
        { "Retro Replay",            Crt(36).banks(8, 0x8000, 0x2000),                    CART_TYPE_RETRO,           CART_PROHIBIT_DEXX, 0 },
        { "EXOS",                    Crt(44).header(1, 0, 0).chip(0, 0xE000, 0x2000),     CART_TYPE_NONE,            0, CART_KERNAL },
        { "Pagefox",                 Crt(53).banks(SMALL, 0x8000, 0x4000),                CART_TYPE_PAGEFOX,         0, 0 },
        { "Kingsoft Business Basic", Crt(54).banks(3, 0x8000, 0x2000),                    CART_TYPE_BBASIC,          CART_PROHIBIT_DEXX, 0 },
        { "GMod2",                   Crt(60).banks(SMALL, 0x8000, 0x2000),                CART_TYPE_GMOD2,           CART_PROHIBIT_DEXX, 0 },
        { "Blackbox V8",             Crt(64).banks(SMALL, 0x8000, 0x4000),                CART_TYPE_BLACKBOX_V8,     CART_PROHIBIT_DFXX, 0 },
        { "Blackbox V3",             Crt(65).banks(1, 0x8000, 0x2000),                    CART_TYPE_BLACKBOX_V3,     CART_PROHIBIT_IO, 0 },
        { "Blackbox V4",             Crt(66).banks(1, 0x8000, 0x4000),                    CART_TYPE_BLACKBOX_V4,     CART_PROHIBIT_IO, 0 },
        { "Blackbox V9",             Crt(71).banks(2, 0x8000, 0x4000),                    CART_TYPE_BLACKBOX_V9,     CART_PROHIBIT_DEXX, 0 },
        { "Megabyter",               Crt(86).banks(LARGE, 0x8000, 0x2000),                CART_TYPE_MEGABYTER,       CART_PROHIBIT_DEXX, 0 },
        { "TwoMegabyter",            Crt(88).banks(LARGE, 0x8000, 0x4000),                CART_TYPE_MEGABYTER | VARIANT_1, CART_PROHIBIT_DEXX, 0 },
        { "C128",                    Crt(0, 128).banks(1, 0x8000, 0x8000),                CART_TYPE_128,             0, 0 },
        { "C128 with I/O",           Crt(1, 128).banks(1, 0x8000, 0x8000),                CART_TYPE_128 | VARIANT_3, CART_PROHIBIT_IO, 0 },
        { "C128 with I/O, subtype 1", Crt(1, 128).header(0, 0, 1).banks(1, 0x8000, 0x8000), CART_TYPE_128 | VARIANT_3, CART_PROHIBIT_ALL_BUT_REU, CART_UCI },
        { "C128 with I/O, subtype 2", Crt(1, 128).header(0, 0, 2).banks(4, 0x8000, 0x8000), CART_TYPE_128 | VARIANT_7, CART_PROHIBIT_ALL_BUT_REU_AND_ACIA_DE, CART_UCI },
    };
    for (const Row &row : rows) {
        Loaded r = load(row.crt, 4 * MB);
        CHECK(r.rc == SSRET_OK, "%s: load returned %d", row.what, r.rc);
        CHECK(r.type == row.type, "%s: cartridge type $%02X, want $%02X", row.what, r.type, row.type);
        CHECK(r.prohibit == row.prohibit, "%s: prohibit $%03X, want $%03X", row.what, r.prohibit, row.prohibit);
        CHECK(r.require == row.require, "%s: require $%03X, want $%03X", row.what, r.require, row.require);
    }
}

// CRT types the firmware does not emulate, and files that are not CRT images.
static void test_refused(void)
{
    const uint16_t accepted[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 13, 15, 18, 19, 20, 21, 32, 36, 44, 53, 54, 60, 64, 65, 66, 71, 86, 87, 88 };
    for (uint16_t hw = 0; hw < 100; hw++) {
        if (std::find(std::begin(accepted), std::end(accepted), hw) != std::end(accepted)) {
            continue;
        }
        Loaded r = load(Crt(hw).banks(1, 0x8000, 0x2000), 4 * MB);
        CHECK(r.rc == SSRET_NOT_IMPLEMENTED, "CRT type %d: load returned %d, want not implemented", hw, r.rc);
    }
    Loaded r = load(Crt(2, 128).banks(1, 0x8000, 0x8000), 4 * MB);
    CHECK(r.rc == SSRET_NOT_IMPLEMENTED, "C128 CRT type 2: load returned %d, want not implemented", r.rc);

    host_fpga_capabilities = 0;
    r = load(Crt(60).banks(4, 0x8000, 0x2000), 4 * MB);
    CHECK(r.rc == SSRET_NOT_IMPLEMENTED, "GMod2 without EEPROM support: load returned %d, want not implemented", r.rc);
    host_fpga_capabilities = CAPAB_EEPROM;

    Crt bad(0);
    bad.banks(1, 0x8000, 0x2000);
    std::vector<uint8_t> bytes = bad.bytes();
    memcpy(bytes.data(), "C65 CARTRIDGE   ", 16);
    FileManager::getFileManager()->files["bad.crt"] = bytes;
    std::vector<uint8_t> mem(MB);
    cart_def def;
    host_cart_max_rom = MB;
    CHECK(C64_CRT::load_crt("/", "bad.crt", &def, mem.data()) == SSRET_ERROR_IN_FILE_FORMAT, "unknown signature is not refused");
}

// Packet-level results: a bank beyond the cartridge region, a first packet
// that is not CHIP, an oversized EEPROM chunk.
static void test_packets(void)
{
    Loaded r = load(Crt(32).header(1, 0, 0).banks(1, 0x8000, 0x2000).chip(64, 0x8000, 0x2000), MB);
    CHECK(r.rc == SSRET_ROM_IMAGE_TOO_LARGE, "bank 64 with 1 MB: load returned %d, want image too large", r.rc);
    r = load(Crt(32).header(1, 0, 0).banks(1, 0x8000, 0x2000).chip(255, 0x8000, 0x2000), 4 * MB);
    CHECK(r.rc == SSRET_OK, "bank 255 with 4 MB: load returned %d", r.rc);

    Crt no_chip(0);
    no_chip.trailer.assign(96, 0x1A);
    r = load(no_chip, MB);
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT, "first packet not CHIP: load returned %d, want format error", r.rc);

    r = load(Crt(60).banks(1, 0x8000, 0x2000).chip(0, 0xDE00, 0x1000), MB);
    CHECK(r.rc == SSRET_EEPROM_TOO_LARGE, "4K EEPROM chunk: load returned %d, want EEPROM too large", r.rc);
}

// A chip at bank B and load address L lands at B * 16K + (L & $2000), or at
// B * 32K for C128 images. Chips shorter than 8K repeat to 8K.
static void test_chip_placement(void)
{
    const uint16_t types[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 13, 15, 19, 20, 21, 32, 36, 44, 53, 60, 64, 65, 66, 71, 86, 87, 88 };
    for (uint16_t hw : types) {
        Crt crt(hw);
        for (int b = 0; b < 4; b++) {
            crt.chip(b, 0x8000, 0x2000).chip(b, 0xA000, 0x2000);
        }
        Loaded r = load(crt, MB);
        CHECK(r.rc == SSRET_OK, "CRT type %d: load returned %d", hw, r.rc);
        for (const Chip &c : crt.chips) {
            uint32_t offset = c.bank * 16 * K + (c.load & 0x2000);
            CHECK(!memcmp(r.at(offset), c.data.data(), c.data.size()), "CRT type %d: chip of bank %d at $%04X is not at offset $%05X", hw, c.bank, c.load, offset);
        }
    }

    Crt c128(1, 128);
    c128.banks(3, 0x8000, 0x8000);
    Loaded r = load(c128, MB);
    for (const Chip &c : c128.chips) {
        CHECK(!memcmp(r.at(c.bank * 32 * K), c.data.data(), c.data.size()), "C128: chip of bank %d is not at offset $%05X", c.bank, c.bank * 32 * K);
    }

    Crt small(0);
    small.header(0, 1, 0).chip(0, 0x8000, 0x1000);
    r = load(small, MB);
    const std::vector<uint8_t> &half = small.chips[0].data;
    CHECK(!memcmp(r.at(0), half.data(), 0x1000) && !memcmp(r.at(0x1000), half.data(), 0x1000), "a 4K chip does not repeat to 8K");

    // Zaxxon: the 4K chip at $8000 serves both $A000 banks, so it is copied to bank 1.
    Crt zaxxon(18);
    zaxxon.chip(0, 0x8000, 0x1000).chip(0, 0xA000, 0x2000).chip(1, 0xA000, 0x2000);
    r = load(zaxxon, MB);
    CHECK(!memcmp(r.at(0x4000), r.at(0), 0x2000), "Zaxxon: bank 1 at $8000 is not a copy of bank 0");
    CHECK(!memcmp(r.at(0x2000), zaxxon.chips[1].data.data(), 0x2000) && !memcmp(r.at(0x6000), zaxxon.chips[2].data.data(), 0x2000),
          "Zaxxon: the $A000 chips are not in place");

    // Kingsoft Business Basic lists three 8K chips at $8000; the loader moves
    // banks 1 and 2 to the offsets the cartridge logic reads.
    Crt bbasic(54);
    bbasic.banks(3, 0x8000, 0x2000);
    r = load(bbasic, MB);
    CHECK(!memcmp(r.at(0x0000), bbasic.chips[0].data.data(), 0x2000) && !memcmp(r.at(0x2000), bbasic.chips[1].data.data(), 0x2000) &&
          !memcmp(r.at(0x6000), bbasic.chips[2].data.data(), 0x2000), "Business Basic: chips are not at $0000, $2000 and $6000");
}

// The image repeats over the whole cartridge region: its size is rounded up to
// a power of two banks, and that block is copied until the region is full.
// A bank register wider than the image then reads the image, as on hardware
// that does not decode the upper bits: an EasyFlash writing $78 to $DE00 reads
// bank $38.
static void test_mirroring(void)
{
    for (uint32_t max_rom : { MB, 4 * MB }) {
        for (int n : { 1, 3, 4, 5, 64, 100, 255 }) {
            uint32_t slots = max_rom / (16 * K);
            if ((uint32_t)n > slots) {
                continue;
            }
            Crt crt(32);
            crt.header(1, 0, 0);
            for (int b = 0; b < n; b++) {
                crt.chip(b, 0x8000, 0x2000).chip(b, 0xA000, 0x2000);
            }
            Loaded r = load(crt, max_rom);
            CHECK(r.rc == SSRET_OK, "EasyFlash, %d banks, %d MB: load returned %d", n, max_rom / MB, r.rc);
            CHECK(r.guard_intact, "EasyFlash, %d banks, %d MB: wrote past the cartridge region", n, max_rom / MB);
            uint32_t block = 1;
            while (block < (uint32_t)n) {
                block <<= 1;
            }
            int wrong = 0;
            uint32_t first_wrong = 0;
            for (uint32_t slot = 0; slot < slots; slot++) {
                uint32_t bank = slot % block;
                bool ok = (bank < (uint32_t)n)
                    ? !memcmp(r.at(slot * 16 * K), crt.chips[2 * bank].data.data(), 0x2000) &&
                      !memcmp(r.at(slot * 16 * K + 0x2000), crt.chips[2 * bank + 1].data.data(), 0x2000)
                    : all_ff(r.at(slot * 16 * K), 16 * K);
                if (!ok && !wrong++) {
                    first_wrong = slot;
                }
            }
            CHECK(!wrong, "EasyFlash, %d banks, %d MB: %d of %d banks do not repeat the image, first bank %d",
                  n, max_rom / MB, wrong, slots, first_wrong);
        }
    }

    // C128 images use 32K banks and are not repeated.
    Crt c128(0, 128);
    c128.banks(3, 0x8000, 0x8000);
    Loaded r = load(c128, 4 * MB);
    CHECK(all_ff(r.at(3 * 32 * K), 4 * MB - 3 * 32 * K), "C128: memory after the image is not $FF");
}

// Magic Desk Plus (GideonZ/1541ultimate#727) is CRT hardware type 87, the id
// VICE assigns it. Its store may travel in CHIP chunks at $DF00, the address of
// the window the machine reaches it through, and the bank field says which
// piece a chunk is, because the size field cannot — it is 16 bits, so the 128K
// of SRAM does not fit in one chunk. Bank 0 is the EEPROM, banks 1 to 4 the
// SRAM in address order. Both land in the memory the REU uses, which is why
// this cart prohibits the REU. A released image need not carry a store at all.
static Crt mdplus(int rom_banks, uint16_t eeprom, int sram_chunks)
{
    Crt crt(87);
    crt.banks(rom_banks, 0x8000, 0x2000);
    if (eeprom) {
        crt.chip(MDPLUS_BANK_EEPROM, 0xDF00, eeprom);
    }
    for (int i = 1; i <= sram_chunks; i++) {
        crt.chip(uint16_t(i), 0xDF00, MDPLUS_SRAM_CHUNK);
    }
    return crt;
}

static uint32_t store_offset(const Chip &c)
{
    return (c.bank == MDPLUS_BANK_EEPROM)
        ? 0
        : MDPLUS_SRAM_OFFSET + (c.bank - 1) * MDPLUS_SRAM_CHUNK;
}

// Fill the store with a byte the file never brings, so a chunk the loader did
// not place is visible as what the last cartridge left behind.
static Loaded load_with_store(const Crt &crt, uint32_t max_rom = 4 * MB)
{
    memset(host_reu_memory, 0x5A, sizeof(host_reu_memory));
    return load(crt, max_rom);
}

static void check_store(const char *what, const Crt &crt)
{
    for (const Chip &c : crt.chips) {
        if (c.load != 0xDF00) {
            continue;
        }
        uint32_t offset = store_offset(c);
        CHECK(!memcmp(host_reu_memory + offset, c.data.data(), c.data.size()),
              "%s: the store chunk of bank %d is not at offset $%05X", what, c.bank, offset);
    }
}

static void test_magic_desk_plus(void)
{
    // Type 19 stays a plain Magic Desk, whatever it carries.
    Loaded r = load_with_store(Crt(19).banks(16, 0x8000, 0x2000));
    CHECK(r.rc == SSRET_OK && r.type == CART_TYPE_DOMARK && r.prohibit == CART_PROHIBIT_DEXX,
          "Magic Desk: type $%02X and prohibit $%03X, want $%02X and $%03X",
          r.type, r.prohibit, CART_TYPE_DOMARK, CART_PROHIBIT_DEXX);

    // The released games carry 32 ROM banks and no store: VICE keeps the SRAM
    // and the EEPROM in files beside the image. The header is what says this is
    // a Magic Desk Plus, and what the file does not bring has to read as an
    // erased device rather than as what the last cartridge left in the REU.
    r = load_with_store(mdplus(32, 0, 0));
    CHECK(r.rc == SSRET_OK && r.type == CART_TYPE_MDPLUS,
          "Magic Desk Plus without a store: type $%02X, want $%02X", r.type, CART_TYPE_MDPLUS);
    CHECK(all_ff(host_reu_memory, MDPLUS_EEPROM_32K),
          "a Magic Desk Plus without an EEPROM image does not find it erased");
    CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET, MDPLUS_SRAM_CHUNK * MDPLUS_SRAM_CHUNKS),
          "a Magic Desk Plus without an SRAM image does not find it erased");

    // The EEPROM image size chooses the page mask and so chooses the variant,
    // exactly as it does in VICE: 8K masks the page register to $1F, 32K to
    // $7F. A cart that brought only SRAM gets the 8K mask.
    struct Row { const char *what; uint16_t eeprom; int sram; uint16_t type; };
    const Row rows[] = {
        { "Magic Desk Plus, SRAM only",   0,                 MDPLUS_SRAM_CHUNKS, CART_TYPE_MDPLUS },
        { "Magic Desk Plus, 8K EEPROM",   MDPLUS_EEPROM_8K,  MDPLUS_SRAM_CHUNKS, CART_TYPE_MDPLUS },
        { "Magic Desk Plus, 32K EEPROM",  MDPLUS_EEPROM_32K, MDPLUS_SRAM_CHUNKS, CART_TYPE_MDPLUS | VARIANT_1 },
        { "Magic Desk Plus, EEPROM only", MDPLUS_EEPROM_32K, 0,                  CART_TYPE_MDPLUS | VARIANT_1 },
    };
    for (const Row &row : rows) {
        Crt crt = mdplus(16, row.eeprom, row.sram);
        r = load_with_store(crt);
        CHECK(r.rc == SSRET_OK, "%s: load returned %d", row.what, r.rc);
        CHECK(r.type == row.type, "%s: cartridge type $%02X, want $%02X", row.what, r.type, row.type);
        // The window is all of $DF00..$DFFF and the registers sit at $DE00 to
        // $DE03, so nothing else may have either I/O page: not the UCI at
        // $DF1C, not the sampler, not an ACIA, and not the REU whose memory
        // this cart borrows.
        CHECK(r.prohibit == CART_PROHIBIT_IO, "%s: prohibit $%03X, want $%03X",
              row.what, r.prohibit, CART_PROHIBIT_IO);
        CHECK(r.guard_intact, "%s: wrote past the cartridge region", row.what);
        check_store(row.what, crt);
    }

    // What the file does not bring reads as an erased device rather than as
    // whatever the cartridge before it left in the REU.
    Crt partial = mdplus(16, MDPLUS_EEPROM_8K, 1);
    r = load_with_store(partial);
    CHECK(all_ff(host_reu_memory + MDPLUS_EEPROM_8K, MDPLUS_EEPROM_32K - MDPLUS_EEPROM_8K),
          "an 8K EEPROM leaves the rest of the EEPROM area unerased");
    CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET + MDPLUS_SRAM_CHUNK,
                 MDPLUS_SRAM_CHUNK * (MDPLUS_SRAM_CHUNKS - 1)),
          "one SRAM chunk leaves the other three unerased");

    // The ROM half is a Magic Desk with one more bank bit: 128 banks of 8K,
    // and the cartridge logic reads bank N at N * 16K, as it does for every 8K
    // banker. That holds whatever order the chunks arrive in — and a store
    // chunk is $8000 bytes, which is also how the loader recognises a C128
    // image.
    for (int store_first = 0; store_first < 2; store_first++) {
        Crt crt(87);
        if (!store_first) {
            crt.banks(128, 0x8000, 0x2000);
        }
        for (int i = 1; i <= MDPLUS_SRAM_CHUNKS; i++) {
            crt.chip(uint16_t(i), 0xDF00, MDPLUS_SRAM_CHUNK);
        }
        if (store_first) {
            crt.banks(128, 0x8000, 0x2000);
        }
        const char *what = store_first ? "Magic Desk Plus with its store before the ROM"
                                       : "Magic Desk Plus with its store after the ROM";
        r = load_with_store(crt);
        CHECK(r.rc == SSRET_OK, "%s: load returned %d", what, r.rc);
        int wrong = 0;
        uint16_t first_wrong = 0;
        for (const Chip &c : crt.chips) {
            if (c.load != 0x8000) {
                continue;
            }
            if (memcmp(r.at(c.bank * 16 * K), c.data.data(), c.data.size()) && !wrong++) {
                first_wrong = c.bank;
            }
        }
        CHECK(!wrong, "%s: %d of 128 ROM banks are not at bank * 16K, first bank %d",
              what, wrong, first_wrong);
        check_store(what, crt);
    }

    // Sizes and bank numbers the store cannot have. VICE accepts an EEPROM
    // image only at 8K or 32K, and there are four SRAM chunks, never five.
    r = load_with_store(mdplus(16, 0x1000, 0));
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "a 4K EEPROM chunk: load returned %d, want format error", r.rc);
    r = load_with_store(mdplus(16, 0x4000, 0));
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "a 16K EEPROM chunk: load returned %d, want format error", r.rc);

    Crt short_sram(87);
    short_sram.banks(16, 0x8000, 0x2000).chip(1, 0xDF00, 0x4000);
    r = load_with_store(short_sram);
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "a 16K SRAM chunk: load returned %d, want format error", r.rc);

    Crt fifth(87);
    fifth.banks(16, 0x8000, 0x2000).chip(MDPLUS_SRAM_CHUNKS + 1, 0xDF00, MDPLUS_SRAM_CHUNK);
    r = load_with_store(fifth);
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "a fifth SRAM chunk: load returned %d, want format error", r.rc);

    Crt twice_eeprom(87);
    twice_eeprom.banks(16, 0x8000, 0x2000)
        .chip(MDPLUS_BANK_EEPROM, 0xDF00, MDPLUS_EEPROM_8K)
        .chip(MDPLUS_BANK_EEPROM, 0xDF00, MDPLUS_EEPROM_8K);
    r = load_with_store(twice_eeprom);
    CHECK(r.rc == SSRET_EEPROM_ALREADY_DEFINED,
          "two EEPROM chunks: load returned %d, want already defined", r.rc);

    Crt twice_sram(87);
    twice_sram.banks(16, 0x8000, 0x2000)
        .chip(2, 0xDF00, MDPLUS_SRAM_CHUNK)
        .chip(2, 0xDF00, MDPLUS_SRAM_CHUNK);
    r = load_with_store(twice_sram);
    CHECK(r.rc == SSRET_EEPROM_ALREADY_DEFINED,
          "the same SRAM chunk twice: load returned %d, want already defined", r.rc);
}

int main()
{
    test_cartridge_types();
    test_refused();
    test_packets();
    test_chip_placement();
    test_mirroring();
    test_magic_desk_plus();
    fprintf(stderr, "c64_crt_test: %s (%d checks, %d failed)\n", failures ? "FAIL" : "OK", checks, failures);
    return failures ? 1 : 0;
}
