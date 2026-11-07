// UCI CTRL_CMD_U64_SAVEMEM (ControlTarget::save_u64_memory, built with U64): it copies the C64's
// 64K from the running machine (no stop: the issuing program keeps its music and raster) through
// the memory window, about 68 ms. That copy must run with the scheduler suspended, not in a
// critical section, so interrupts stay on, and it marks the copy (c64_ram_copy_begin/end) for the unlock
// interrupt handler while the scheduler is suspended.
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "uci_test_util.h"
#include "control_target.h"
#include "u64.h"

UciFakeU64 uci_fake_u64;
uint8_t uci_fake_u64_window[65536];
UciFakeMemonly uci_fake_u64_memonly;

int main()
{
    uci_fake::init();
    uci_fake::reset();
    for (int i = 0; i < 65536; i++) uci_fake_u64_window[i] = (uint8_t)((i * 7 + 3) ^ (i >> 8));
    memset(&uci_fake_u64, 0, sizeof uci_fake_u64);
    uci_fake::fs_add_dir("/Usb0");

    Cmd c(4, CTRL_CMD_U64_SAVEMEM);
    c.str("/Usb0/mem.bin");
    Result r = run(c);
    const std::vector<uint8_t> *f = uci_fake::fs_file("/Usb0/mem.bin");
    bool content = f && f->size() == 65536 && memcmp(f->data(), uci_fake_u64_window, 65536) == 0;
    char d[220];
    snprintf(d, sizeof d, "status %s, file %s; critical sections %d, suspends %d/resumes %d, MEMONLY=1 set %d time(s) "
             "suspended and %d not, mode now %d", r.status.c_str(), !f ? "missing" : content ? "= C64 RAM" : "differs",
             uci_fake_u64.critical, uci_fake_u64.suspends, uci_fake_u64.resumes, uci_fake_u64.memonly_on_inside,
             uci_fake_u64.memonly_on_outside, uci_fake_u64_memonly.v);
    check(r.status.compare(0, 2, "00") == 0 && content, "savemem_writes_the_c64_ram", d);
    check(uci_fake_u64.critical == 0 && uci_fake_u64.suspends == 1 && uci_fake_u64.resumes == 1 &&
          uci_fake_u64.suspended == 0 && uci_fake_u64.memonly_on_inside == 1 && uci_fake_u64.memonly_on_outside == 0 &&
          uci_fake_u64_memonly.v == 0,
          "savemem_copies_with_the_scheduler_suspended_not_interrupts_off", d);
    snprintf(d, sizeof d, "copy begin %d (suspended %d), end %d (suspended %d), resumed with the flag up %d, flag now %d",
             uci_fake_u64.begins, uci_fake_u64.begins_suspended, uci_fake_u64.ends, uci_fake_u64.ends_suspended,
             uci_fake_u64.resumed_with_flag, uci_fake_u64.copy_flag);
    check(uci_fake_u64.begins == 1 && uci_fake_u64.begins_suspended == 1 && uci_fake_u64.ends == 1 &&
          uci_fake_u64.ends_suspended == 1 && uci_fake_u64.resumed_with_flag == 0 && uci_fake_u64.copy_flag == 0,
          "savemem_raises_the_copy_flag_only_inside_the_suspended_stretch", d);

    printf("u64 savemem: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
