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
#define FR_DISK_FULL 31
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

class FileInfo {};

class FileSystem {
public:
    static const char *get_error_string(FRESULT res) { return res == FR_OK ? "OK" : "error"; }
};

// A file over bytes the test registered by name for reading, or over a buffer
// the test owns for writing, so a saved cartridge can be inspected.
class File {
    static const std::vector<uint8_t> &no_data() { static const std::vector<uint8_t> e; return e; }
    const std::vector<uint8_t> &data;
    std::vector<uint8_t> *sink;
    size_t pos;
    size_t room;    // how much more a write may add, as a full medium has it
public:
    explicit File(const std::vector<uint8_t> &d) : data(d), sink(NULL), pos(0), room(SIZE_MAX) {}
    explicit File(std::vector<uint8_t> *out, size_t limit = SIZE_MAX)
        : data(no_data()), sink(out), pos(0), room(limit) {}
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) {
        size_t n = (pos < data.size()) ? std::min<size_t>(len, data.size() - pos) : 0;
        memcpy(buffer, data.data() + pos, n);
        pos += n;
        *transferred = (uint32_t)n;
        return FR_OK;
    }
    // A write past the room left stops short and still answers FR_OK, which
    // is the case a caller has to catch by the count.
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) {
        uint32_t n = (uint32_t)std::min<size_t>(len, room);
        if (sink) {
            const uint8_t *p = (const uint8_t *)buffer;
            sink->insert(sink->end(), p, p + n);
        }
        room -= n;
        *transferred = n;
        return FR_OK;
    }
    FRESULT seek(uint32_t offset) { pos = offset; return FR_OK; }
    uint32_t get_size(void) { return (uint32_t)data.size(); }
};

// Files by pathname, with the leading slash dropped, so that a path and a name
// reach the same file as the whole pathname does. Opening one for writing
// creates it empty, as FA_CREATE_ALWAYS does, and what is written lands in the
// map for the test to inspect. A pathname in `room` can take only that many
// bytes, as on a medium that fills up.
class FileManager {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    std::map<std::string, size_t> room;
    static FileManager *getFileManager() { static FileManager fm; return &fm; }
    static std::string key(const char *path, const char *name) {
        std::string k = name;
        if (name[0] != '/') {
            k = path;
            if (!k.empty() && (k.back() != '/')) {
                k += '/';
            }
            k += name;
        }
        while (!k.empty() && (k[0] == '/')) {
            k.erase(0, 1);
        }
        return k;
    }
    FRESULT fopen(const char *path, const char *name, int flags, File **f) {
        std::string k = key(path, name);
        if (flags & FA_CREATE_ALWAYS) {
            std::vector<uint8_t> &out = files[k];
            out.clear();
            auto limit = room.find(k);
            *f = new File(&out, limit == room.end() ? SIZE_MAX : limit->second);
            return FR_OK;
        }
        auto it = files.find(k);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        *f = new File(it->second);
        return FR_OK;
    }
    void fclose(File *f) { delete f; }
    FRESULT delete_file(const char *pathname) {
        return files.erase(key("", pathname)) ? FR_OK : FR_NO_FILE;
    }
    FRESULT rename(const char *old_name, const char *new_name) {
        auto it = files.find(key("", old_name));
        if ((it == files.end()) || files.count(key("", new_name))) {
            return FR_NO_FILE;     // the real one does not replace a file either
        }
        std::vector<uint8_t> data = it->second;
        files.erase(it);
        files[key("", new_name)] = data;
        return FR_OK;
    }
    FRESULT fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool) {
        auto it = files.find(key(path, filename));
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        files[key(dest, dest_filename)] = it->second;
        return FR_OK;
    }
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
