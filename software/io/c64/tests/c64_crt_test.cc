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
    // A header a real tool wrote, used verbatim in place of the built one, so
    // that a test can ask what the loader makes of an actual file rather than
    // of this builder's idea of one.
    std::vector<uint8_t> raw_header;

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
        if (raw_header.size() == 0x40) {
            memcpy(out.data(), raw_header.data(), 0x40);
        } else {
            memcpy(out.data(), machine == 128 ? "C128 CARTRIDGE  " : "C64 CARTRIDGE   ", 16);
            out[0x13] = 0x40;
            out[0x14] = 1;
            out[0x16] = hw >> 8;
            out[0x17] = hw & 0xFF;
            out[0x18] = exrom;
            out[0x19] = game;
            out[0x1A] = subtype;
        }
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

// The chip packet for one bank and load address in a saved CRT file.
static const uint8_t *saved_chip(const std::vector<uint8_t> &file, uint16_t bank, uint16_t load, uint16_t *size_out)
{
    size_t pos = 0x40;
    while (pos + 16 <= file.size()) {
        const uint8_t *h = file.data() + pos;
        if (memcmp(h, "CHIP", 4)) {
            break;
        }
        uint32_t packet = (uint32_t(h[4]) << 24) | (uint32_t(h[5]) << 16) | (uint32_t(h[6]) << 8) | h[7];
        if ((packet < 16) || (pos + packet > file.size())) {
            break;
        }
        if ((((h[10] << 8) | h[11]) == bank) && (((h[12] << 8) | h[13]) == load)) {
            *size_out = (h[14] << 8) | h[15];
            return h + 16;
        }
        pos += packet;
    }
    return NULL;
}

// Saving must leave the cartridge the C64 is running alone, and the file must
// carry the EAPI the cartridge came with, not the one the firmware patched in.
static void test_save_keeps_memory(void)
{
    for (int i = 0; i < 768; i++) {
        _eapi_65_start[i] = uint8_t(0x10 + (i & 7));
    }
    Crt crt(32);
    crt.header(1, 0, 0);
    crt.chip(0, 0x8000, 0x2000).chip(0, 0xA000, 0x2000).chip(1, 0x8000, 0x2000).chip(1, 0xA000, 0x2000);

    // The EAPI of the cartridge sits at $B800, which is bank 0 at $A000 plus $1800.
    std::vector<uint8_t> &romh = crt.chips[1].data;
    const uint8_t signature[4] = { 'e', 'a', 'p', 'i' };
    memcpy(&romh[0x1800], signature, 4);
    for (int i = 4; i < 768; i++) {
        romh[0x1800 + i] = uint8_t(0xC0 + (i & 0x1F));
    }
    std::vector<uint8_t> original_eapi(romh.begin() + 0x1800, romh.begin() + 0x1800 + 768);

    Loaded r = load(crt, MB);
    CHECK(r.rc == SSRET_OK, "EasyFlash with EAPI: load returned %d", r.rc);
    CHECK(!memcmp(r.at(0x3800), _eapi_65_start, 768), "the firmware's EAPI is not in cartridge memory after loading");

    std::vector<uint8_t> file;
    File out(&file);
    SubsysResultCode_e rc = C64_CRT::save_crt(&out);
    CHECK(rc == SSRET_OK, "save returned %d", rc);
    CHECK(!memcmp(r.at(0x3800), _eapi_65_start, 768), "saving replaced the EAPI in cartridge memory");

    uint16_t size = 0;
    const uint8_t *chip = saved_chip(file, 0, 0xA000, &size);
    CHECK(chip && (size == 0x2000), "the saved file has no bank 0 chip at $A000");
    if (chip && (size == 0x2000)) {
        CHECK(!memcmp(chip + 0x1800, original_eapi.data(), 768), "the saved file carries the patched EAPI, not the original");
        CHECK(!memcmp(chip, r.at(0x2000), 0x1800), "the bytes before the EAPI are not the ones in memory");
        CHECK(!memcmp(chip + 0x1B00, r.at(0x3B00), 0x2000 - 0x1B00), "the bytes after the EAPI are not the ones in memory");
    }
}

// Magic Desk Plus (GideonZ/1541ultimate#727) is CRT hardware type 87, the id
// VICE assigns it. Its store does not travel in the CRT: VICE keeps the SRAM
// and the EEPROM as raw images beside the cartridge and refuses a CRT with
// anything but ROM chunks, so the firmware reads and writes them the same way,
// named after the CRT -- test.crt goes with test.sram and test.eeprom. Both
// land in the memory the REU uses, which is why this cart prohibits the REU.
static Crt mdplus(int rom_banks, uint8_t revision = MDPLUS_REV_SRAM_EEPROM_32K)
{
    Crt crt(87);
    crt.header(0, 1, revision);
    crt.banks(rom_banks, 0x8000, 0x2000);
    return crt;
}

static const char *SRAM_FILE = "test.sram";
static const char *EEPROM_FILE = "test.eeprom";

// Put the store files beside test.crt, or take them away when empty, and fill
// the store with a byte neither the files nor an erased device bring, so a part
// the loader did not place is visible as what the last cartridge left behind.
static Loaded load_with_store(const Crt &crt, const std::vector<uint8_t> &sram = {},
                              const std::vector<uint8_t> &eeprom = {}, uint32_t max_rom = 4 * MB)
{
    auto &files = FileManager::getFileManager()->files;
    files.erase(SRAM_FILE);
    files.erase(EEPROM_FILE);
    if (!sram.empty()) {
        files[SRAM_FILE] = sram;
    }
    if (!eeprom.empty()) {
        files[EEPROM_FILE] = eeprom;
    }
    memset(host_reu_memory, 0x5A, sizeof(host_reu_memory));
    return load(crt, max_rom);
}

static void test_magic_desk_plus(void)
{
    // Type 19 stays a plain Magic Desk, whatever it carries.
    Loaded r = load_with_store(Crt(19).banks(16, 0x8000, 0x2000));
    CHECK(r.rc == SSRET_OK && r.type == CART_TYPE_DOMARK && r.prohibit == CART_PROHIBIT_DEXX,
          "Magic Desk: type $%02X and prohibit $%03X, want $%02X and $%03X",
          r.type, r.prohibit, CART_TYPE_DOMARK, CART_PROHIBIT_DEXX);

    // Without files beside it the store reads as an erased device rather than
    // as what the last cartridge left in the REU.
    r = load_with_store(mdplus(32));
    CHECK(r.rc == SSRET_OK && r.type == (CART_TYPE_MDPLUS | VARIANT_1),
          "Magic Desk Plus without store files: type $%02X, want $%02X",
          r.type, CART_TYPE_MDPLUS | VARIANT_1);
    CHECK(all_ff(host_reu_memory, MDPLUS_EEPROM_32K),
          "a Magic Desk Plus without an EEPROM file does not find it erased");
    CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET, MDPLUS_SRAM_SIZE),
          "a Magic Desk Plus without an SRAM file does not find it erased");

    // The revision byte says which parts are fitted, and with them the page
    // mask: a 32K EEPROM has 128 pages and masks the page register to $7F, an
    // 8K one has 32 and masks it to $1F. It also says which files are read: a
    // part the cart does not have is not read even when its file is there.
    // VICE r46239 settled the numbering.
    struct Row { const char *what; uint8_t rev; uint32_t eeprom; bool sram; uint16_t type; };
    const Row rows[] = {
        { "Magic Desk Plus rev 0, SRAM and 32K EEPROM", MDPLUS_REV_SRAM_EEPROM_32K,
          MDPLUS_EEPROM_32K, true,  CART_TYPE_MDPLUS | VARIANT_1 },
        { "Magic Desk Plus rev 1, SRAM and 8K EEPROM",  MDPLUS_REV_SRAM_EEPROM_8K,
          MDPLUS_EEPROM_8K,  true,  CART_TYPE_MDPLUS },
        { "Magic Desk Plus rev 2, 32K EEPROM only",     MDPLUS_REV_EEPROM_32K,
          MDPLUS_EEPROM_32K, false, CART_TYPE_MDPLUS | VARIANT_1 },
        { "Magic Desk Plus rev 3, 8K EEPROM only",      MDPLUS_REV_EEPROM_8K,
          MDPLUS_EEPROM_8K,  false, CART_TYPE_MDPLUS },
        { "Magic Desk Plus rev 4, SRAM only",           MDPLUS_REV_SRAM,
          0,                 true,  CART_TYPE_MDPLUS },
    };
    const std::vector<uint8_t> sram = payload(1001, MDPLUS_SRAM_SIZE);
    for (const Row &row : rows) {
        const std::vector<uint8_t> eeprom = payload(2002, row.eeprom ? row.eeprom : MDPLUS_EEPROM_8K);
        r = load_with_store(mdplus(16, row.rev), sram, eeprom);
        CHECK(r.rc == SSRET_OK, "%s: load returned %d", row.what, r.rc);
        CHECK(r.type == row.type, "%s: cartridge type $%02X, want $%02X", row.what, r.type, row.type);
        // The window is all of $DF00..$DFFF and the registers sit at $DE00 to
        // $DE03, so nothing else may have either I/O page: not the UCI at
        // $DF1C, not the sampler, not an ACIA, and not the REU whose memory
        // this cart borrows.
        CHECK(r.prohibit == CART_PROHIBIT_IO, "%s: prohibit $%03X, want $%03X",
              row.what, r.prohibit, CART_PROHIBIT_IO);
        CHECK(r.guard_intact, "%s: wrote past the cartridge region", row.what);
        if (row.sram) {
            CHECK(!memcmp(host_reu_memory + MDPLUS_SRAM_OFFSET, sram.data(), MDPLUS_SRAM_SIZE),
                  "%s: the SRAM file is not in the store", row.what);
        } else {
            CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET, MDPLUS_SRAM_SIZE),
                  "%s: an SRAM file was read for a cart without SRAM", row.what);
        }
        if (row.eeprom) {
            CHECK(!memcmp(host_reu_memory, eeprom.data(), row.eeprom),
                  "%s: the EEPROM file is not in the store", row.what);
            CHECK(all_ff(host_reu_memory + row.eeprom, MDPLUS_EEPROM_32K - row.eeprom),
                  "%s: the EEPROM area past the part is not erased", row.what);
        } else {
            CHECK(all_ff(host_reu_memory, MDPLUS_EEPROM_32K),
                  "%s: an EEPROM file was read for a cart without EEPROM", row.what);
        }
    }

    // A file of the wrong size is left alone rather than half applied, and the
    // load still succeeds: the game runs with an erased part, as in VICE.
    const uint32_t wrong_sizes[] = { MDPLUS_EEPROM_8K, MDPLUS_EEPROM_32K + 1, 0x4000 };
    for (uint32_t size : wrong_sizes) {
        r = load_with_store(mdplus(16, MDPLUS_REV_SRAM_EEPROM_32K), payload(3, MDPLUS_SRAM_SIZE - 1),
                            payload(4, size));
        CHECK(r.rc == SSRET_OK, "store files of the wrong size: load returned %d", r.rc);
        CHECK(all_ff(host_reu_memory, MDPLUS_EEPROM_32K),
              "a %d byte EEPROM file for a 32K part was read", size);
        CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET, MDPLUS_SRAM_SIZE),
              "an SRAM file one byte short was read");
    }

    // A store inside the CRT is refused, as VICE refuses it: a chunk at the
    // DF00 window is not a ROM bank, and taken as one it would land on one.
    Crt in_crt = mdplus(16);
    in_crt.chip(0, 0xDF00, MDPLUS_EEPROM_32K);
    r = load_with_store(in_crt);
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "a Magic Desk Plus with a $DF00 chunk: load returned %d, want format error", r.rc);

    // The ROM half is a Magic Desk with one more bank bit: 128 banks of 8K,
    // and the cartridge logic reads bank N at N * 16K, as it does for every 8K
    // banker.
    Crt full = mdplus(128);
    r = load_with_store(full);
    CHECK(r.rc == SSRET_OK, "Magic Desk Plus with 128 banks: load returned %d", r.rc);
    int wrong = 0;
    uint16_t first_wrong = 0;
    for (const Chip &c : full.chips) {
        if (memcmp(r.at(c.bank * 16 * K), c.data.data(), c.data.size()) && !wrong++) {
            first_wrong = c.bank;
        }
    }
    CHECK(!wrong, "Magic Desk Plus: %d of 128 ROM banks are not at bank * 16K, first bank %d",
          wrong, first_wrong);

    // A revision the format does not define leaves nothing to fit.
    r = load_with_store(mdplus(16, MDPLUS_REV_MAX + 1));
    CHECK(r.rc == SSRET_ERROR_IN_FILE_FORMAT,
          "Magic Desk Plus revision %d: load returned %d, want format error",
          MDPLUS_REV_MAX + 1, r.rc);
}

// Save Cartridge writes the CRT with its ROM alone, and the store beside it
// under the name the CRT was saved as, one raw image per part the cart has. A
// load of what was saved gets the same store back.
static void test_magic_desk_plus_save(void)
{
    auto &files = FileManager::getFileManager()->files;
    struct Row { const char *what; uint8_t rev; uint32_t eeprom; bool sram; };
    const Row rows[] = {
        { "rev 0", MDPLUS_REV_SRAM_EEPROM_32K, MDPLUS_EEPROM_32K, true },
        { "rev 1", MDPLUS_REV_SRAM_EEPROM_8K,  MDPLUS_EEPROM_8K,  true },
        { "rev 3", MDPLUS_REV_EEPROM_8K,       MDPLUS_EEPROM_8K,  false },
        { "rev 4", MDPLUS_REV_SRAM,            0,                 true },
    };
    for (const Row &row : rows) {
        Crt crt = mdplus(16, row.rev);
        Loaded r = load_with_store(crt);
        CHECK(r.rc == SSRET_OK, "save %s: load returned %d", row.what, r.rc);

        // What the game wrote while it ran.
        const std::vector<uint8_t> sram = payload(5005, MDPLUS_SRAM_SIZE);
        const std::vector<uint8_t> eeprom = payload(6006, MDPLUS_EEPROM_32K);
        memcpy(host_reu_memory + MDPLUS_SRAM_OFFSET, sram.data(), MDPLUS_SRAM_SIZE);
        memcpy(host_reu_memory, eeprom.data(), MDPLUS_EEPROM_32K);

        files.erase("saved.sram");
        files.erase("saved.eeprom");
        std::vector<uint8_t> file;
        File out(&file);
        SubsysResultCode_e rc = C64_CRT::save_crt(&out);
        CHECK(rc == SSRET_OK, "save %s: save_crt returned %d", row.what, rc);
        CHECK(file == crt.bytes(), "save %s: the saved CRT is not the ROM image it was loaded from", row.what);
        rc = C64_CRT::save_store("/", "saved.crt");
        CHECK(rc == SSRET_OK, "save %s: save_store returned %d", row.what, rc);

        if (row.sram) {
            CHECK(files.count("saved.sram") && (files["saved.sram"] == sram),
                  "save %s: saved.sram is not the SRAM", row.what);
        } else {
            CHECK(!files.count("saved.sram"), "save %s: wrote an SRAM file for a cart without SRAM", row.what);
        }
        if (row.eeprom) {
            CHECK(files.count("saved.eeprom") &&
                  (files["saved.eeprom"] == std::vector<uint8_t>(eeprom.begin(), eeprom.begin() + row.eeprom)),
                  "save %s: saved.eeprom is not the %d bytes of the EEPROM", row.what, row.eeprom);
        } else {
            CHECK(!files.count("saved.eeprom"), "save %s: wrote an EEPROM file for a cart without EEPROM", row.what);
        }

        // Load what was saved, and the store comes back.
        files["saved.crt"] = file;
        memset(host_reu_memory, 0x5A, sizeof(host_reu_memory));
        std::vector<uint8_t> mem(4 * MB + GUARD, 0x5A);
        cart_def def;
        rc = C64_CRT::load_crt("/", "saved.crt", &def, mem.data());
        CHECK(rc == SSRET_OK, "save %s: loading the saved cartridge returned %d", row.what, rc);
        if (row.sram) {
            CHECK(!memcmp(host_reu_memory + MDPLUS_SRAM_OFFSET, sram.data(), MDPLUS_SRAM_SIZE),
                  "save %s: the SRAM did not survive save and load", row.what);
        }
        if (row.eeprom) {
            CHECK(!memcmp(host_reu_memory, eeprom.data(), row.eeprom),
                  "save %s: the EEPROM did not survive save and load", row.what);
        }
    }

    // Any other cartridge writes nothing beside its CRT.
    files.erase("other.sram");
    files.erase("other.eeprom");
    Loaded r = load(Crt(19).banks(16, 0x8000, 0x2000), 4 * MB);
    CHECK(r.rc == SSRET_OK, "Magic Desk: load returned %d", r.rc);
    CHECK(C64_CRT::save_store("/", "other.crt") == SSRET_OK, "Magic Desk: save_store failed");
    CHECK(!files.count("other.sram") && !files.count("other.eeprom"),
          "a plain Magic Desk wrote store files");
}

// The header of mdplustest.crt, byte for byte. The other tests here build their
// own headers, so they agree with this file's idea of a CRT rather than with a
// real one; this is the header a Magic Desk Plus actually arrives with.
static const uint8_t mdplus_real_header[0x40] = {
    0x43, 0x36, 0x34, 0x20, 0x43, 0x41, 0x52, 0x54,  // "C64 CART"
    0x52, 0x49, 0x44, 0x47, 0x45, 0x20, 0x20, 0x20,  // "RIDGE   "
    0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x57,  // header $40, v1.0, type 87
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // exrom 0, game 1: 8K mode
    0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x44, 0x65,  // "Magic De"
    0x73, 0x6B, 0x20, 0x50, 0x6C, 0x75, 0x73, 0x00,  // "sk Plus"
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// The shape of a Magic Desk Plus that exists, rather than one built to be
// tested: crystalct's mdplustest.crt, the Magic Desk Plus Manager, offered in
// GideonZ/1541ultimate#844 as the image to test the mapper with. It is 262720
// bytes of 32 ROM banks at $8000 and nothing else -- no $DF00 chunk, no EEPROM,
// no SRAM -- which is the released shape: the store is kept in files beside
// the image, and this test puts none there.
//
// The bank payloads are this file's own, since what is regressed is the shape
// and the header, not the manager's 6502. A store the image does not bring has
// to read as an erased device: it lives in the memory the REU uses, and the
// manager's own SRAM and EEPROM detection is what reads it first.
//
// https://github.com/crystalct/MagicDeskPlus/tree/main/CRT_TEST
static void test_magic_desk_plus_real_image(void)
{
    Crt crt(87);
    crt.raw_header.assign(mdplus_real_header, mdplus_real_header + 0x40);
    crt.banks(32, 0x8000, 0x2000);

    // 1 MB is what a cartridge region is unless a target asks for more, and the
    // released shape has to load there: 32 banks at 16K each is 512K.
    const uint32_t regions[] = { 1 * MB, 4 * MB };
    for (uint32_t max_rom : regions) {
        Loaded r = load_with_store(crt, {}, {}, max_rom);
        CHECK(r.rc == SSRET_OK, "mdplustest.crt in %dK: load returned %d", max_rom / K, r.rc);

        // Header byte $1A is 0, which fits a 32K EEPROM, so the page register
        // masks to $7F. The file carries no EEPROM chunk at all: reading the
        // size from the chunk rather than the header is exactly what would get
        // this image wrong, and did until VICE r46239 settled the numbering.
        CHECK(r.type == (CART_TYPE_MDPLUS | VARIANT_1),
              "mdplustest.crt in %dK: cartridge type $%02X, want $%02X (revision 0, the 32K EEPROM mask)",
              max_rom / K, r.type, CART_TYPE_MDPLUS | VARIANT_1);
        CHECK(r.prohibit == CART_PROHIBIT_IO,
              "mdplustest.crt in %dK: prohibit $%03X, want $%03X",
              max_rom / K, r.prohibit, CART_PROHIBIT_IO);
        CHECK(r.guard_intact, "mdplustest.crt in %dK: wrote past the cartridge region", max_rom / K);

        // The cartridge logic reads bank N at N * 16K: $DE00 bits 0..6 land in
        // bank_bits(21 downto 14), and bit 13 stays low in 8K mode, so only the
        // low 8K of each 16K step is ever addressed.
        int wrong = 0;
        uint16_t first_wrong = 0;
        for (const Chip &c : crt.chips) {
            if (memcmp(r.at(c.bank * 16 * K), c.data.data(), c.data.size()) && !wrong++) {
                first_wrong = c.bank;
            }
        }
        CHECK(!wrong, "mdplustest.crt in %dK: %d of 32 ROM banks are not at bank * 16K, first bank %d",
              max_rom / K, wrong, first_wrong);

        CHECK(all_ff(host_reu_memory, MDPLUS_EEPROM_32K),
              "mdplustest.crt in %dK: the EEPROM the image does not carry is not erased", max_rom / K);
        CHECK(all_ff(host_reu_memory + MDPLUS_SRAM_OFFSET, MDPLUS_SRAM_SIZE),
              "mdplustest.crt in %dK: the SRAM the image does not carry is not erased", max_rom / K);
    }
}

int main()
{
    test_cartridge_types();
    test_refused();
    test_packets();
    test_chip_placement();
    test_mirroring();
    test_save_keeps_memory();
    test_magic_desk_plus();
    test_magic_desk_plus_save();
    test_magic_desk_plus_real_image();
    fprintf(stderr, "c64_crt_test: %s (%d checks, %d failed)\n", failures ? "FAIL" : "OK", checks, failures);
    return failures ? 1 : 0;
}
