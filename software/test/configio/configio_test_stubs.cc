/*
 * configio_test_stubs.cc
 *
 * The three symbols configio.cc needs that only exist on the device. All of
 * them belong to menu handlers the tests do not drive: the debug-log actions
 * and "clear config flash".
 *
 * outbyte is the exception and is real work: the firmware's small_printf
 * routes every character through it, and on the device that is what reaches
 * the syslog. Sending it to stdout here is what lets a test see the warnings
 * this change adds.
 */

#include <stdio.h>

#include "stream_textlog.h"
#include "subsys.h"

extern "C" void outbyte(int c)
{
    fputc(c, stdout);
}

// The global debug log, defined in the application on the device.
StreamTextLog textLog(16384);

// Only reached by the "clear config flash" handler, which asks the C64 to
// power off. Nothing here executes commands.
SubsysResultCode_t SubsysCommand::execute(void)
{
    SubsysResultCode_t result;
    result.status = SSRET_OK;
    return result;
}
