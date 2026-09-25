// Host build support for c64_crt_test.cc, force-included before c64_crt.cc.
//
// c64_crt.h includes filemanager.h, c64.h and subsys.h, which pull in the
// FreeRTOS file system and the whole C64 host. This header defines their
// include guards and provides only what the CRT loader uses. The cartridge
// type constants and cart_def come from c64.h itself, extracted by the
// Makefile into cart_defs.h, so the test sees the firmware's own values.
#ifndef C64_CRT_HOST_H
#define C64_CRT_HOST_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>

#define FILEMANAGER_H
#define C64_H
#define INFRA_SUBSYS_H_

#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#include "indexed_list.h"
#include "mystring.h"
#include "return_codes.h"
#include "cart_defs.h"

typedef int FRESULT;
#define FR_OK 0
#define FR_NO_FILE 4
#define FA_READ 0x01

class FileInfo {};

// A read-only file over bytes the test registered by name.
class File {
    const std::vector<uint8_t> &data;
    size_t pos;
public:
    explicit File(const std::vector<uint8_t> &d) : data(d), pos(0) {}
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) {
        size_t n = (pos < data.size()) ? std::min<size_t>(len, data.size() - pos) : 0;
        memcpy(buffer, data.data() + pos, n);
        pos += n;
        *transferred = (uint32_t)n;
        return FR_OK;
    }
    FRESULT write(const void *, uint32_t len, uint32_t *transferred) { *transferred = len; return FR_OK; }
    FRESULT seek(uint32_t offset) { pos = offset; return FR_OK; }
};

class FileManager {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    static FileManager *getFileManager() { static FileManager fm; return &fm; }
    FRESULT fopen(const char *, const char *name, int, File **f) {
        auto it = files.find(name);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        *f = new File(it->second);
        return FR_OK;
    }
    void fclose(File *f) { delete f; }
};

#define CAPAB_EEPROM 0x00400000
// The memory the REU uses, which is where a Magic Desk Plus keeps its store.
// Defined by the test, so that both it and the loader see the same bytes.
extern uint8_t host_reu_memory[256 * 1024];
#define REU_MEMORY_BASE host_reu_memory
extern uint32_t host_fpga_capabilities;
extern uint32_t host_cart_max_rom;
static inline uint32_t getFpgaCapabilities(void) { return host_fpga_capabilities; }

class C64 {
public:
    static uint32_t get_cartridge_max_rom(void) { return host_cart_max_rom; }
    static void set_eeprom_data(uint8_t *) {}
    static void get_eeprom_data(uint8_t *) {}
    static bool get_eeprom_dirty(void) { return false; }
};

#endif
