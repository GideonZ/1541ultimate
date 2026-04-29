// Firmware-only implementation of the monitor file/jump helpers.
// Included from userinterface.cc so it shares the same translation unit.
// Host-test builds supply a stub instead — see software/test/user_interface/src/.

#include "monitor_file_io.h"
#include "machine_monitor.h"
#include "userinterface.h"
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "browsable_root.h"
#include "user_file_interaction.h"
#include "filemanager.h"
#include "file.h"
#include "mystring.h"
#include "host.h"
#include "memory_backend.h"
#ifndef RECOVERYAPP
#ifndef UPDATER
#include "subsys.h"
#include "c64.h"
#endif
#endif

namespace {

// Persistent path used to resume the monitor's file picker in the directory
// last seen across LOAD/SAVE invocations. File-scope to avoid touching the
// MachineMonitorState header that's shared with the host test.
mstring s_monitor_browse_path("/");

} // namespace

bool monitor_io::pick_file(UserInterface *ui, const char *title,
                           char *path_out, int path_max,
                           char *name_out, int name_max,
                           bool save_mode)
{
    (void)title; // Browser draws its own header; title is reserved for future use.

    if (path_out && path_max > 0) path_out[0] = 0;
    if (name_out && name_max > 0) name_out[0] = 0;

    Browsable *root = new BrowsableRoot();
    TreeBrowser *browser = new TreeBrowser(ui, root);
    browser->allow_exit = true;
    browser->pick_mode = save_mode ? TreeBrowser::PICK_SAVE : TreeBrowser::PICK_LOAD;
    browser->init();
    if (s_monitor_browse_path.length() > 1) {
        browser->cd(s_monitor_browse_path.c_str());
    }

    int ret = 0;
    GenericHost *h = ui->host;
    while (!ret && (!h || h->exists())) {
        ret = browser->poll(0);
    }

    bool picked = browser->picked;
    if (picked) {
        if (path_out) {
            strncpy(path_out, browser->picked_path.c_str(), path_max - 1);
            path_out[path_max - 1] = 0;
        }
        if (name_out) {
            strncpy(name_out, browser->picked_name.c_str(), name_max - 1);
            name_out[name_max - 1] = 0;
        }
        s_monitor_browse_path = browser->picked_path;
    }

    browser->deinit();
    delete browser;
    delete root;
    return picked;
}

const char *monitor_io::load_into_memory(const char *path, const char *name,
                                         MemoryBackend *backend, uint16_t start_addr,
                                         bool use_prg_addr, uint32_t offset,
                                         bool length_auto, uint32_t length,
                                         uint16_t *out_start_addr, uint32_t *out_bytes)
{
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    FRESULT res = fm->fopen(path, name, FA_READ, &f);
    if (res != FR_OK || !f) {
        return "OPEN FAILED";
    }
    uint32_t size = f->get_size();
    uint32_t header_skip = 0;
    if (use_prg_addr) {
        if (size < 2) {
            fm->fclose(f);
            return "NOT A PRG";
        }
        uint8_t hdr[2];
        uint32_t got = 0;
        if (f->read(hdr, 2, &got) != FR_OK || got != 2) {
            fm->fclose(f);
            return "READ FAILED";
        }
        start_addr = (uint16_t)(hdr[0] | (hdr[1] << 8));
        header_skip = 2;
        size -= 2;
    }
    uint32_t effective = 0;
    MonitorError verr = monitor_validate_load_size(size, offset, length_auto, length, &effective);
    if (verr == MONITOR_RANGE && !length_auto && length > 65536) {
        fm->fclose(f);
        return "LOAD TOO LARGE (>64K)";
    }
    if (verr == MONITOR_RANGE && length_auto && size > offset && (size - offset) > 65536) {
        fm->fclose(f);
        return "LOAD TOO LARGE (>64K)";
    }
    if (verr != MONITOR_OK) {
        fm->fclose(f);
        return monitor_error_text(verr);
    }
    if (f->seek(header_skip + offset) != FR_OK) {
        fm->fclose(f);
        return "SEEK FAILED";
    }

    uint8_t buf[512];
    uint32_t remaining = effective;
    uint32_t mem_pos = 0;
    backend->begin_session();
    while (remaining > 0) {
        uint32_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        uint32_t got = 0;
        if (f->read(buf, want, &got) != FR_OK || got == 0) {
            break;
        }
        for (uint32_t i = 0; i < got; i++) {
            backend->write((uint16_t)(start_addr + mem_pos + i), buf[i]);
        }
        mem_pos += got;
        remaining -= got;
    }
    // BASIC PRG fix-up: when loading into the BASIC text area at $0801, refresh
    // the runtime pointers so RUN/LIST do not walk into garbage. Without this
    // the C64 routinely crashes when the user runs a freshly-loaded PRG —
    // which in turn can take down the application hosting the monitor.
    if (start_addr == 0x0801 && mem_pos > 0) {
        uint32_t end_addr32 = (uint32_t)start_addr + mem_pos;
        if (end_addr32 > 0x10000) end_addr32 = 0x10000;
        uint16_t end_addr = (uint16_t)end_addr32;
        uint8_t lo = (uint8_t)(end_addr & 0xFF);
        uint8_t hi = (uint8_t)((end_addr >> 8) & 0xFF);
        backend->write(0x002D, lo); backend->write(0x002E, hi); // VARTAB
        backend->write(0x002F, lo); backend->write(0x0030, hi); // ARYTAB
        backend->write(0x0031, lo); backend->write(0x0032, hi); // STREND
        backend->write(0x00AE, lo); backend->write(0x00AF, hi); // last load addr
    }
    backend->end_session();
    fm->fclose(f);
    if (remaining > 0) {
        return "READ FAILED";
    }
    if (out_start_addr) *out_start_addr = start_addr;
    if (out_bytes) *out_bytes = mem_pos;
    return NULL;
}

const char *monitor_io::save_from_memory(UserInterface *ui, const char *path, const char *name,
                                         MemoryBackend *backend,
                                         uint16_t start, uint16_t end)
{
    FileManager *fm = FileManager::getFileManager();
    File *f = NULL;
    FRESULT res = create_file_ask_if_exists(fm, ui, path, name, &f);
    if (res != FR_OK || !f) {
        return "CREATE FAILED";
    }
    // Write a standard C64 PRG header: little-endian start address. This makes
    // the file round-trip correctly through LOAD with PRG-address mode.
    uint8_t hdr[2];
    hdr[0] = (uint8_t)(start & 0xFF);
    hdr[1] = (uint8_t)((start >> 8) & 0xFF);
    {
        uint32_t written = 0;
        if (f->write(hdr, 2, &written) != FR_OK || written != 2) {
            fm->fclose(f);
            return "WRITE FAILED";
        }
    }
    uint8_t buf[512];
    uint32_t total = (uint32_t)end - (uint32_t)start + 1;
    uint32_t pos = 0;
    backend->begin_session();
    while (pos < total) {
        uint32_t want = total - pos;
        if (want > sizeof(buf)) want = sizeof(buf);
        for (uint32_t i = 0; i < want; i++) {
            buf[i] = backend->read((uint16_t)(start + pos + i));
        }
        uint32_t written = 0;
        if (f->write(buf, want, &written) != FR_OK || written != want) {
            backend->end_session();
            fm->fclose(f);
            return "WRITE FAILED";
        }
        pos += want;
    }
    backend->end_session();
    fm->fclose(f);
    return NULL;
}

void monitor_io::jump_to(uint16_t address)
{
#if !defined(RECOVERYAPP) && !defined(UPDATER)
    // Reuse the existing boot-cart DMA-jump pathway. The buffer's first two
    // bytes are the JMP target ($00AA/$00AB on the C64), and the remainder is
    // loaded as data — empty here, since GOTO must not modify memory. The
    // boot cart's `start_jump` then JMP ($00AA)s into the user's program.
    static uint8_t jump_buffer[2];
    jump_buffer[0] = (uint8_t)(address & 0xFF);
    jump_buffer[1] = (uint8_t)((address >> 8) & 0xFF);
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER,
                                           RUNCODE_DMALOAD_JUMP,
                                           (void *)jump_buffer, 2);
    cmd->execute();
#else
    (void)address;
#endif
}
