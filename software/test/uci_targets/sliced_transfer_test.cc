// Long UCI file transfers (DOS LOAD_REU/SAVE_REU, the RAM disk load/save, and
// control LOAD_REU/SAVE_REU through the REU preloader) go in slices of at most
// 64 KB, so other tasks get the FatFs volume in between, and stop at the next
// slice once the C64 aborts the command or is reset. A transfer that runs to
// the end gives the same bytes, reply and status as before.
#include <string>
#include <vector>
#include <stdio.h>
#include "uci_test_util.h"
#include "dos.h"
#include "control_target.h"
#include "reu_preloader.h"

extern REUPreloader *reu_preloader;

static const uint32_t SLICE = 0x10000;

static void open_file(const char *name, uint8_t mode)
{
    uci_fake::fs_add_dir("/Usb0");
    Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
    run(cd);
    Cmd open(1, DOS_CMD_OPEN_FILE); open.u8(mode).str(name);
    run(open);
}

static void close_file(void)
{
    Cmd close(1, DOS_CMD_CLOSE_FILE);
    run(close);
}

static int io_calls(bool is_write)
{
    int n = 0;
    for (size_t i = 0; i < uci_fake::io_log.size(); i++) {
        if (uci_fake::io_log[i].is_write == is_write) n++;
    }
    return n;
}

static std::string io_summary(void)
{
    char d[96];
    snprintf(d, sizeof d, "reads %d (max %u), writes %d (max %u)", io_calls(false), uci_fake::max_io_len(false),
             io_calls(true), uci_fake::max_io_len(true));
    return d;
}

// Raise the abort bit (or the reset flag) after the n-th read/write call.
static void stop_after(int n, bool reset)
{
    std::shared_ptr<int> count(new int(0));
    uci_fake::on_io = [count, n, reset](const uci_fake::IoEvent &) {
        if (++*count == n) {
            if (reset) {
                uci_fake::c64_reset_pending = true;
            } else {
                CMD_IF_STATUSBYTE |= CMD_ABORT_DATA;
            }
        }
    };
}

static void t_dos_load_reu_full(void)
{
    uci_fake::reset();
    const uint32_t len = 0x100000 + 1000;
    std::vector<uint8_t> bytes = pattern(len, 21);
    uci_fake::fs_add_file("/Usb0/r.reu", bytes);
    open_file("r.reu", FA_READ);
    Cmd load(1, DOS_CMD_LOAD_REU); load.le32(0x020000).le32(len);
    Result r = run(load);
    close_file();
    std::string reply(r.reply.begin(), r.reply.end());
    bool ok = memcmp(uci_fake::reu() + 0x020000, bytes.data(), len) == 0 && r.status == "00,OK" &&
              reply == "$1003e8 BYTES LOADED TO REU $ 20000" && uci_fake::max_io_len(false) <= SLICE;
    check(ok, "dos_load_reu_whole_file_in_64k_slices", "status " + r.status + ", reply '" + reply + "', " + io_summary());
}

static void t_dos_load_reu_stop(bool reset)
{
    uci_fake::reset();
    const uint32_t len = 0x400000;
    uci_fake::fs_add_file("/Usb0/r.reu", pattern(len, 22));
    open_file("r.reu", FA_READ);
    stop_after(3, reset);
    Cmd load(1, DOS_CMD_LOAD_REU); load.le32(0).le32(len);
    Result r = run(load);
    uci_fake::on_io = nullptr;
    close_file();
    bool ok = io_calls(false) == 3 && uci_fake::max_io_len(false) <= SLICE && r.status == "02,REQUEST TRUNCATED";
    check(ok, reset ? "dos_load_reu_stops_at_next_slice_after_reset" : "dos_load_reu_stops_at_next_slice_after_abort",
          "status " + r.status + ", " + io_summary());
}

static void t_dos_save_reu(void)
{
    uci_fake::reset();
    const uint32_t len = 0x80000;
    std::vector<uint8_t> bytes = pattern(len, 23);
    memcpy(uci_fake::reu() + 0x10000, bytes.data(), len);
    open_file("s.reu", FA_WRITE | FA_CREATE_ALWAYS);
    Cmd save(1, DOS_CMD_SAVE_REU); save.le32(0x10000).le32(len);
    Result r = run(save);
    close_file();
    const std::vector<uint8_t> *f = uci_fake::fs_file("/Usb0/s.reu");
    std::string reply(r.reply.begin(), r.reply.end());
    bool ok = f && *f == bytes && r.status == "00,OK" && reply == "$ 80000 BYTES SAVED FROM REU $ 10000" &&
              uci_fake::max_io_len(true) <= SLICE;
    check(ok, "dos_save_reu_whole_range_in_64k_slices", "status " + r.status + ", reply '" + reply + "', " + io_summary());

    uci_fake::reset();
    open_file("s.reu", FA_WRITE | FA_CREATE_ALWAYS);
    stop_after(2, false);
    Cmd save2(1, DOS_CMD_SAVE_REU); save2.le32(0).le32(0x400000);
    r = run(save2);
    uci_fake::on_io = nullptr;
    close_file();
    ok = io_calls(true) == 2 && r.status == "02,REQUEST TRUNCATED";
    check(ok, "dos_save_reu_stops_at_next_slice_after_abort", "status " + r.status + ", " + io_summary());
}

static void ramdisk_setup(int drv, int type)
{
    uci_fake::mp3_ramdrive_type[drv] = type;
    uci_fake::reu()[0x7dc7 + drv] = 0x10;   // RAM disk at REU $100000
}

static void t_ramdisk(void)
{
    uci_fake::reset();
    const uint32_t size = 819200;
    std::vector<uint8_t> img = pattern(size, 24);
    uci_fake::fs_add_file("/Usb0/disk.d81", img);
    ramdisk_setup(0, 1581);
    Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
    run(cd);
    Cmd load(1, CTRL_CMD_LOAD_INTO_RAMDISK); load.u8(0).str("disk.d81");
    Result r = run(load);
    bool ok = r.status == "00,OK" && memcmp(uci_fake::reu() + 0x100000, img.data(), size) == 0 &&
              uci_fake::max_io_len(false) <= SLICE && uci_fake::open_file_count() == 0;
    check(ok, "ramdisk_load_in_64k_slices", "status " + r.status + ", " + io_summary());

    uci_fake::reset();
    ramdisk_setup(0, 1581);
    std::vector<uint8_t> mem = pattern(size, 25);
    memcpy(uci_fake::reu() + 0x100000, mem.data(), size);
    uci_fake::fs_add_dir("/Usb0");
    Cmd cd2(1, DOS_CMD_CHANGE_DIR); cd2.str("/Usb0");
    run(cd2);
    Cmd save(1, CTRL_CMD_SAVE_RAMDISK); save.u8(0).str("OUT.D81");   // upper case: see RESULTS.md, X14 note on SAVE_RAMDISK
    r = run(save);
    const std::vector<uint8_t> *f = uci_fake::fs_file("/Usb0/OUT.D81");
    ok = r.status == "00,OK" && f && *f == mem && uci_fake::max_io_len(true) <= SLICE && uci_fake::open_file_count() == 0;
    check(ok, "ramdisk_save_in_64k_slices", "status " + r.status + ", " + io_summary());

    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/disk.d81", img);
    ramdisk_setup(0, 1581);
    Cmd cd3(1, DOS_CMD_CHANGE_DIR); cd3.str("/Usb0");
    run(cd3);
    stop_after(2, false);
    Cmd load2(1, CTRL_CMD_LOAD_INTO_RAMDISK); load2.u8(0).str("disk.d81");
    r = run(load2);
    uci_fake::on_io = nullptr;
    ok = io_calls(false) == 2 && uci_fake::open_file_count() == 0;
    check(ok, "ramdisk_load_stops_at_next_slice_after_abort", "status " + r.status + ", " + io_summary());
}

static void reu_cfg(const char *img)
{
    uci_fake::cfg.set(CFG_C64_REU_EN, 1);
    uci_fake::cfg.set(CFG_C64_REU_SIZE, 5);   // 4 MB
    uci_fake::cfg.set(CFG_C64_REU_OFFS, 0);
    uci_fake::cfg.set_string(CFG_C64_REU_IMG, img);
}

static void t_control_reu(void)
{
    uci_fake::reset();
    const uint32_t size = 0x400000;
    std::vector<uint8_t> img = pattern(size, 26);
    uci_fake::fs_add_file("/Usb0/img.reu", img);
    reu_cfg("/Usb0/img.reu");
    Cmd c(4, CTRL_CMD_LOAD_REU); c.u8(0).u8(0).u8(0);
    Result r = run(c);
    std::string reply(r.reply.begin() + 4, r.reply.end());
    bool ok = r.status == "00,OK" && memcmp(uci_fake::reu(), img.data(), size) == 0 &&
              reply == "REU LOAD: LOADED 4194304 BYTES TO $     0 FROM /USB0/IMG.REU" && uci_fake::max_io_len(false) <= SLICE;
    check(ok, "control_load_reu_in_64k_slices", "status " + r.status + ", reply '" + reply + "', " + io_summary());

    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/img.reu", img);
    reu_cfg("/Usb0/img.reu");
    stop_after(4, true);
    Cmd c2(4, CTRL_CMD_LOAD_REU); c2.u8(0).u8(0).u8(0);
    r = run(c2);
    uci_fake::on_io = nullptr;
    ok = io_calls(false) == 4 && uci_fake::open_file_count() == 0;
    check(ok, "control_load_reu_stops_at_next_slice_after_reset", "status " + r.status + ", " + io_summary());

    uci_fake::reset();
    std::vector<uint8_t> mem = pattern(size, 27);
    memcpy(uci_fake::reu(), mem.data(), size);
    uci_fake::fs_add_dir("/Usb0");
    reu_cfg("/Usb0/out.reu");
    Cmd c3(4, CTRL_CMD_SAVE_REU); c3.u8(0).u8(0).u8(0);
    r = run(c3);
    const std::vector<uint8_t> *f = uci_fake::fs_file("/Usb0/out.reu");
    ok = r.status == "00,OK" && f && *f == mem && uci_fake::max_io_len(true) <= SLICE;
    check(ok, "control_save_reu_in_64k_slices", "status " + r.status + ", " + io_summary());

    // The menu and the boot-time preload pass no stop function: a stray UCI
    // abort bit must not cut their transfer short.
    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/img.reu", img);
    reu_cfg("/Usb0/img.reu");
    CMD_IF_STATUSBYTE |= CMD_ABORT_DATA;
    char status[512];
    int n = reu_preloader->LoadREU(status);
    ok = n == (int)size && memcmp(uci_fake::reu(), img.data(), size) == 0;
    check(ok, "menu_load_reu_ignores_the_uci_abort_bit", std::string(status) + ", " + io_summary());
}

int main()
{
    uci_fake::init();
    t_dos_load_reu_full();
    t_dos_load_reu_stop(false);
    t_dos_load_reu_stop(true);
    t_dos_save_reu();
    t_ramdisk();
    t_control_reu();
    return finish("sliced_transfer");
}
