// UCI CTRL_CMD_LOAD_CONFIG (ControlTarget::load_config) takes the name of the file to load from
// the command. The command buffer is not cleared between commands, so the name has to end where
// the command ends, not at the first zero a longer command left behind it.
#include <string>
#include <vector>
#include <stdio.h>
#include "uci_test_util.h"
#include "control_target.h"

int main()
{
    uci_fake::init();
    uci_fake::reset();
    const char *text = "[fake]\n";
    uci_fake::fs_add_file("/Usb0/b.cfg", std::vector<uint8_t>(text, text + strlen(text)));

    Cmd c(4, CTRL_CMD_LOAD_CONFIG);
    c.str("/Usb0/b.cfg");
    Result r = run(c);
    check(r.status.compare(0, 2, "88") != 0, "load_config_opens_the_named_file", "status " + r.status);

    Cmd c2(4, CTRL_CMD_LOAD_CONFIG);
    c2.str("/Usb0/b.cfg");
    r = run_after(c2, "\x04\x50/Usb0/a-much-longer-name.cfg");
    check(r.status.compare(0, 2, "88") != 0, "load_config_name_ends_with_the_command", "status " + r.status);

    return finish("load config");
}
