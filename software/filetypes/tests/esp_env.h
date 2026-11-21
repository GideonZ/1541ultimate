/*
 * esp_env.h
 *
 * The world FileTypeESP :: execute (cut out of filetype_esp.cc as it is, see the Makefile)
 * runs in on the host: a file held in memory, a scripted ESP32 that records what it was
 * asked to flash and keeps what it was given, and a user interface that logs its popups.
 */
#ifndef ESP_ENV_H_
#define ESP_ENV_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>
#include <string>
#include <vector>

#include "../../io/usb/tests/host_test/host_test.h"
#define CHECK(c, msg) EXPECT_TRUE((c) && (msg))

#define BUTTON_OK 0x01
#define FA_READ   0x01

typedef int FRESULT;
#define FR_OK 0
#define FR_NO_FILE 4

enum SubsysResultCode_e { SSRET_OK = 0, SSRET_CANNOT_OPEN_FILE, SSRET_INTERNAL_ERROR };

typedef void (*EspDownloadCallback_t)(void *context);

struct FileInfo {
    uint32_t size;
    FileInfo(int) : size(0) { }
};

struct FileSystem {
    static const char *get_error_string(FRESULT) { return "file error"; }
};

// Everything the case sets, and everything the code under test did
struct EspWorld {
    std::vector<uint8_t> content;   // what the file holds
    uint32_t reported_size;         // what the file system says its size is (get_size and fstat)
    int download_status;
    int flash_status;
    bool verify_ok;                 // the ESP32's MD5 of its flash matches
    int verify_status;              // when not 0, the verify command itself fails

    std::string log;                // popups and ESP32 calls, in order
    int opens, closes;
    long live_bytes;                // bytes of operator new[] not yet deleted
    struct Written { uint32_t address; std::vector<uint8_t> data; };
    std::vector<Written> written;
};
extern EspWorld world;
void reset_world();
void set_image(const std::vector<uint8_t> &img);   // content and size agree

struct File {
    uint32_t pos;
    File() : pos(0) { }
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) {
        uint32_t avail = pos < world.content.size() ? world.content.size() - pos : 0;
        uint32_t n = len < avail ? len : avail;
        if (n) memcpy(buffer, world.content.data() + pos, n);
        pos += n;
        *transferred = n;
        return FR_OK;
    }
    FRESULT seek(uint32_t p) { pos = p; return FR_OK; }
    uint32_t get_size() { return world.reported_size; }
};

struct FileManager {
    static FileManager *getFileManager() { static FileManager fm; return &fm; }
    FRESULT fstat(const char *, const char *, FileInfo &inf) { inf.size = world.reported_size; return FR_OK; }
    FRESULT fopen(const char *, const char *, uint8_t, File **f) { *f = new File(); world.opens++; return FR_OK; }
    void fclose(File *f) { world.closes++; delete f; }
};

struct UserInterface {
    int popup(const char *msg, uint8_t) { world.log += std::string("[") + msg + "]"; return 0; }
    void show_progress(const char *, int) { world.log += "<progress>"; }
    void update_progress(const char *, int) { }
    void hide_progress() { world.log += "<hide>"; }
};

struct SubsysCommand {
    std::string path, filename;
    UserInterface *user_interface;
};

struct Esp32 {
    void StopApp()       { world.log += "{stop}"; }
    void EnableRunMode() { world.log += "{run}"; }
    void StartApp()      { world.log += "{start}"; }
    int  Download()      { world.log += "{download}"; return world.download_status; }
    int  Flash(const uint8_t *binary, uint32_t address, uint32_t length, EspDownloadCallback_t, void *) {
        world.log += "{flash}";
        EspWorld::Written w; w.address = address; w.data.assign(binary, binary + length);
        world.written.push_back(w);
        return world.flash_status;
    }
    // Checks what is in the ESP32's flash against the image (the fixed code calls it)
    int  VerifyFlash(const uint8_t *binary, uint32_t address, uint32_t length) {
        world.log += "{verify}";
        if (world.verify_status) return world.verify_status;
        return world.verify_ok ? 0 : -8;
    }
};
extern Esp32 esp32;

class FileTypeESP {
public:
    static SubsysResultCode_e execute(SubsysCommand *cmd);
};

#endif
