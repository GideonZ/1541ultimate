// Firmware-only implementation of the monitor file/jump helpers.
// Included from userinterface.cc so it shares the same translation unit.
// Host-test builds supply a stub instead — see software/test/monitor/.

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
#include "c64.h"
#include "subsys.h"
#if defined(U64) && (U64)
#include "u64_machine.h"
#include "itu.h"
#endif
#endif
#endif

namespace {

// Persistent path used to resume the monitor's file picker in the directory
// last seen across LOAD/SAVE invocations. File-scope to avoid touching the
// MachineMonitorState header that's shared with the host test.
mstring s_monitor_browse_path("/");

static const uint16_t c_monitor_nmi_vector = 0x0318;
static const uint16_t c_monitor_jump_trampoline = 0x033C;
static const uint16_t c_monitor_cpu_ddr = 0x0000;
static const uint16_t c_monitor_cpu_port = 0x0001;
static const uint16_t c_monitor_hard_nmi_vector = 0xFFFA;
static const uint8_t c_monitor_hard_nmi_cpu_port = 0x05;

#if !defined(RECOVERYAPP) && !defined(UPDATER) && defined(U64) && (U64)
static uint8_t monitor_effective_cpu_port(const DebugContext *context)
{
    if (context && context->valid) {
        if (context->live_cpu_port_valid) {
            return (uint8_t)(context->live_cpu_port & 0x07);
        }
        if (context->cpu_port_registers_valid) {
            return (uint8_t)(((context->cpu_port_latch & context->cpu_ddr) |
                ((uint8_t)(~context->cpu_ddr) & 0x07)) & 0x07);
        }
    }
    return 0x07;
}

static bool monitor_cpu_port_maps_kernal_ram(uint8_t cpu_port)
{
    return (cpu_port & 0x02) == 0;
}

static void monitor_append_lda_sta_abs(uint8_t *bytes, unsigned *pos,
                                       uint16_t address, uint8_t value)
{
    bytes[(*pos)++] = 0xA9;
    bytes[(*pos)++] = value;
    bytes[(*pos)++] = 0x8D;
    bytes[(*pos)++] = (uint8_t)(address & 0xFF);
    bytes[(*pos)++] = (uint8_t)(address >> 8);
}

static void monitor_append_lda_sta_zp(uint8_t *bytes, unsigned *pos,
                                      uint8_t address, uint8_t value)
{
    bytes[(*pos)++] = 0xA9;
    bytes[(*pos)++] = value;
    bytes[(*pos)++] = 0x85;
    bytes[(*pos)++] = address;
}

static bool run_u64_nmi_trampoline(U64Machine *machine,
                                   const uint8_t *body, unsigned body_len,
                                   const DebugContext *context = 0,
                                   bool pulse_nmi = true)
{
    if (!machine || !body) {
        return false;
    }
    if (!pulse_nmi && !machine->is_stopped()) {
        return false;
    }

    bool stopped_it = machine->begin_stopped_session();
    uint8_t old_nmi_lo = machine->peek_visible(c_monitor_nmi_vector + 0);
    uint8_t old_nmi_hi = machine->peek_visible(c_monitor_nmi_vector + 1);
    bool restore_cpu_port = context && context->valid &&
        context->cpu_port_registers_valid;
    uint8_t effective_cpu_port = monitor_effective_cpu_port(context);
    bool install_hard_nmi = context && context->valid &&
        monitor_cpu_port_maps_kernal_ram(effective_cpu_port);
    uint8_t old_hard_nmi_lo = 0;
    uint8_t old_hard_nmi_hi = 0;
    uint8_t trampoline[64];
    unsigned pos = 0;

    monitor_append_lda_sta_abs(trampoline, &pos, c_monitor_nmi_vector + 0,
                               old_nmi_lo);
    monitor_append_lda_sta_abs(trampoline, &pos, c_monitor_nmi_vector + 1,
                               old_nmi_hi);

    if (install_hard_nmi) {
        old_hard_nmi_lo = machine->peek_cpu(c_monitor_hard_nmi_vector + 0,
                                            c_monitor_hard_nmi_cpu_port);
        old_hard_nmi_hi = machine->peek_cpu(c_monitor_hard_nmi_vector + 1,
                                            c_monitor_hard_nmi_cpu_port);
        monitor_append_lda_sta_abs(trampoline, &pos,
                                   c_monitor_hard_nmi_vector + 0,
                                   old_hard_nmi_lo);
        monitor_append_lda_sta_abs(trampoline, &pos,
                                   c_monitor_hard_nmi_vector + 1,
                                   old_hard_nmi_hi);
    }

    if (restore_cpu_port) {
        monitor_append_lda_sta_zp(trampoline, &pos,
                                  (uint8_t)c_monitor_cpu_ddr,
                                  context->cpu_ddr);
        monitor_append_lda_sta_zp(trampoline, &pos,
                                  (uint8_t)c_monitor_cpu_port,
                                  context->cpu_port_latch);
    }

    if (pos + body_len > sizeof(trampoline)) {
        machine->end_stopped_session(stopped_it);
        return false;
    }
    for (unsigned i = 0; i < body_len; i++) {
        trampoline[pos++] = body[i];
    }

    for (unsigned i = 0; i < pos; i++) {
        machine->poke_visible_preserving_freeze_restore(
            (uint16_t)(c_monitor_jump_trampoline + i), trampoline[i]);
    }
    machine->poke_visible_preserving_freeze_restore(
        c_monitor_nmi_vector + 0,
        (uint8_t)(c_monitor_jump_trampoline & 0xFF));
    machine->poke_visible_preserving_freeze_restore(
        c_monitor_nmi_vector + 1,
        (uint8_t)(c_monitor_jump_trampoline >> 8));
    if (install_hard_nmi) {
        machine->poke_cpu(c_monitor_hard_nmi_vector + 0,
                          (uint8_t)(c_monitor_jump_trampoline & 0xFF),
                          c_monitor_hard_nmi_cpu_port);
        machine->poke_cpu(c_monitor_hard_nmi_vector + 1,
                          (uint8_t)(c_monitor_jump_trampoline >> 8),
                          c_monitor_hard_nmi_cpu_port);
    }

    if (pulse_nmi) {
        C64_MODE = C64_MODE_NMI;
    }
    machine->end_stopped_session(stopped_it);
    if (pulse_nmi) {
        C64_MODE = MODE_NORMAL;
    }
    return true;
}
#endif

} // namespace

bool monitor_io::pick_file(UserInterface *ui, const char *title,
                           char *path_out, int path_max,
                           char *name_out, int name_max,
                           bool save_mode)
{
    if (path_out && path_max > 0) path_out[0] = 0;
    if (name_out && name_max > 0) name_out[0] = 0;

    if (!ui) {
        return false;
    }

    // Browsable *root = save_mode ? static_cast<Browsable *>(new MonitorSaveBrowsableRoot())
    //                             : static_cast<Browsable *>(new BrowsableRoot());
    Browsable *root = new BrowsableRoot();
    TreeBrowser *browser = new TreeBrowser(ui, root);
    browser->allow_exit = true;
    browser->has_border = true;
    browser->use_ui_focus_stack = false;
    browser->title = title;
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
    // Reject loads that would wrap past $FFFF — silently corrupting low memory
    // (zero page, stack) is a far worse outcome than an explicit error.
    if ((uint32_t)start_addr + effective > 0x10000) {
        fm->fclose(f);
        return "LOAD WRAPS PAST $FFFF";
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
#if defined(U64) && (U64)
    U64Machine *machine = static_cast<U64Machine *>(C64::getMachine());
    const uint8_t body[] = {
        0x4C, (uint8_t)(address & 0xFF), (uint8_t)(address >> 8)
    };
    run_u64_nmi_trampoline(machine, body, sizeof(body));
#else
    uint8_t jump_buffer[2] = {
        (uint8_t)(address & 0xFF),
        (uint8_t)(address >> 8)
    };

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER,
                                           RUNCODE_DMALOAD_JUMP,
                                           jump_buffer, sizeof(jump_buffer));
    cmd->execute();
#endif
#else
    (void)address;
#endif
}

void monitor_io::resume_to_context(const DebugContext &context)
{
#if !defined(RECOVERYAPP) && !defined(UPDATER)
#if defined(U64) && (U64)
    U64Machine *machine = static_cast<U64Machine *>(C64::getMachine());
    const uint8_t body[] = {
        0xA2, context.sp,
        0x9A,
        0xA9, (uint8_t)(context.pc >> 8),
        0x48,
        0xA9, (uint8_t)(context.pc & 0xFF),
        0x48,
        0xA9, context.sr,
        0x48,
        0xA0, context.y,
        0xA2, context.x,
        0xA9, context.a,
        0x40
    };
    run_u64_nmi_trampoline(machine, body, sizeof(body), &context);
#else
    jump_to(context.pc);
#endif
#else
    (void)context;
#endif
}

bool monitor_io::stage_jump_to(uint16_t address)
{
#if !defined(RECOVERYAPP) && !defined(UPDATER) && defined(U64) && (U64)
    U64Machine *machine = static_cast<U64Machine *>(C64::getMachine());
    const uint8_t body[] = {
        0x4C, (uint8_t)(address & 0xFF), (uint8_t)(address >> 8)
    };
    return run_u64_nmi_trampoline(machine, body, sizeof(body), 0, false);
#else
    (void)address;
    return false;
#endif
}

bool monitor_io::stage_resume_to_context(const DebugContext &context)
{
#if !defined(RECOVERYAPP) && !defined(UPDATER) && defined(U64) && (U64)
    U64Machine *machine = static_cast<U64Machine *>(C64::getMachine());
    const uint8_t body[] = {
        0xA2, context.sp,
        0x9A,
        0xA9, (uint8_t)(context.pc >> 8),
        0x48,
        0xA9, (uint8_t)(context.pc & 0xFF),
        0x48,
        0xA9, context.sr,
        0x48,
        0xA0, context.y,
        0xA2, context.x,
        0xA9, context.a,
        0x40
    };
    return run_u64_nmi_trampoline(machine, body, sizeof(body), &context,
                                  false);
#else
    (void)context;
    return false;
#endif
}

void monitor_io::pulse_staged_nmi(void)
{
#if !defined(RECOVERYAPP) && !defined(UPDATER) && defined(U64) && (U64)
    C64_MODE = C64_MODE_NMI;
    wait_10us(1);
    C64_MODE = MODE_NORMAL;
#endif
}
