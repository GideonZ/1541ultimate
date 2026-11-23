// Resource and memory-safety cases of the UCI DOS target, driven only through
// UCI commands:
//   - DOS OPEN while a file is already open dropped the old handle
//   - LOAD_INTO_RAMDISK leaked a heap FileInfo on every command
//   - SAVE_RAMDISK deleted an uninitialised pointer when the RAM drive type
//     does not match the file extension (heap corruption)
// The leak and the bad delete are found by AddressSanitizer / LeakSanitizer
// (`make test SAN=1`); the open handle shows in open_file_count() without it.
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include "uci_test_util.h"
#include "dos.h"
#include "control_target.h"

static void ramdisk_setup(int drv, int type)
{
    uci_fake::mp3_ramdrive_type[drv] = type;
    uci_fake::reu()[0x7dc7 + drv] = 0x10;
}

// No DOS_CMD_CHANGE_DIR: its directory listing is only released at the next
// listing, so it would show up as a leak of the test run itself. File names
// are absolute instead.
static void cd_usb0(void)
{
    uci_fake::fs_add_dir("/Usb0");
}

static void t_open_while_open(void)
{
    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/a.prg", pattern(100, 1));
    uci_fake::fs_add_file("/Usb0/b.prg", pattern(100, 2));
    cd_usb0();
    Cmd o1(1, DOS_CMD_OPEN_FILE); o1.u8(FA_READ).str("/Usb0/a.prg");
    run(o1);
    Cmd o2(1, DOS_CMD_OPEN_FILE); o2.u8(FA_READ).str("/Usb0/b.prg");
    Result r = run(o2);
    Cmd close(1, DOS_CMD_CLOSE_FILE);
    run(close);
    check(r.status == "00,OK" && uci_fake::open_file_count() == 0, "dos_open_while_open_keeps_no_stale_handle",
          "status " + r.status + ", open files " + std::to_string(uci_fake::open_file_count()));
}

static void t_reset_closes_file(void)
{
    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/a.prg", pattern(100, 1));
    cd_usb0();
    Cmd o1(1, DOS_CMD_OPEN_FILE); o1.u8(FA_READ).str("/Usb0/a.prg");
    run(o1);
    command_targets[1]->c64_reset();
    check(uci_fake::open_file_count() == 0, "dos_c64_reset_closes_the_open_file",
          "open files " + std::to_string(uci_fake::open_file_count()));
}

static void t_load_ramdisk_each_exit(void)
{
    // every exit of LOAD_INTO_RAMDISK: what-if, success, missing file, wrong type
    uci_fake::reset();
    const uint32_t size = 819200;
    uci_fake::fs_add_file("/Usb0/disk.d81", pattern(size, 24));
    ramdisk_setup(0, 1581);
    cd_usb0();
    Cmd whatif(1, CTRL_CMD_LOAD_INTO_RAMDISK); whatif.u8(0x80).str("/Usb0/disk.d81");
    Result r1 = run(whatif);
    Cmd load(1, CTRL_CMD_LOAD_INTO_RAMDISK); load.u8(0).str("/Usb0/disk.d81");
    Result r2 = run(load);
    Cmd missing(1, CTRL_CMD_LOAD_INTO_RAMDISK); missing.u8(0).str("/Usb0/nothere.d81");
    Result r3 = run(missing);
    check(r1.status == "00,OK" && r2.status == "00,OK" && r3.status != "00,OK" && uci_fake::open_file_count() == 0,
          "ramdisk_load_every_exit_completes", r1.status + " / " + r2.status + " / " + r3.status);
}

static void t_save_ramdisk_type_mismatch(void)
{
    // RAM drive is a 1581, the name says .D64: refused, and nothing is deleted
    uci_fake::reset();
    ramdisk_setup(0, 1581);
    cd_usb0();
    Cmd save(1, CTRL_CMD_SAVE_RAMDISK); save.u8(0).str("/Usb0/OUT.D64");
    Result r = run(save);
    check(r.status.compare(0, 2, "91") == 0, "ramdisk_save_type_mismatch_is_refused", r.status);
}

// optional argument: run one case (open, reset, load, save); the sanitizers stop at
// the first error, so each defect gets its own run
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    uci_fake::init();
    const char *only = argc > 1 ? argv[1] : "";
    bool all = !*only;
    if (all || !strcmp(only, "open")) t_open_while_open();
    if (all || !strcmp(only, "reset")) t_reset_closes_file();
    if (all || !strcmp(only, "load")) t_load_ramdisk_each_exit();
    if (all || !strcmp(only, "save")) t_save_ramdisk_type_mismatch();
    return finish("dos_resource");
}
