// UCI CTRL_CMD_U64_SAVEMEM (ControlTarget::save_u64_memory, built with U64): it saves the C64's
// 64K through the machine's own whole-RAM copy (C64::get_all_memory), the one "Save C64 Memory" uses.
// That copy stops the C64 and holds the scheduler, not the interrupts, for the ~68 ms (see
// test/c64mem); SAVEMEM must not reach into the memory window by itself, which would copy from the
// running machine.
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "uci_test_util.h"
#include "control_target.h"

static uint8_t machine_ram[65536];
static int copies;

static void machine_copy(uint8_t *pb)
{
    copies++;
    memcpy(pb, machine_ram, 65536);
}

int main()
{
    uci_fake::init();
    uci_fake::reset();
    for (int i = 0; i < 65536; i++) machine_ram[i] = (uint8_t)((i * 7 + 3) ^ (i >> 8));
    uci_fake_all_memory_hook = machine_copy;
    uci_fake::fs_add_dir("/Usb0");

    Cmd c(4, CTRL_CMD_U64_SAVEMEM);
    c.str("/Usb0/mem.bin");
    Result r = run(c);
    const std::vector<uint8_t> *f = uci_fake::fs_file("/Usb0/mem.bin");
    bool content = f && f->size() == 65536 && memcmp(f->data(), machine_ram, 65536) == 0;
    char d[200];
    snprintf(d, sizeof d, "status %s, file %s, machine copies %d", r.status.c_str(),
             !f ? "missing" : content ? "= the machine's RAM" : "differs", copies);
    check(r.status.compare(0, 2, "00") == 0 && content && copies == 1, "savemem_saves_the_machines_whole_ram_copy", d);

    printf("u64 savemem: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
