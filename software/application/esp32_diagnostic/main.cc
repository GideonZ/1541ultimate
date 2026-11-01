#include "controller_probe.h"
#include "platform.h"

static ControllerProbeHardware hardware;
extern "C" void diagnostic_main()
{
    diagnostic_line(1, "ESP32 IDENTITY - RAM DIAGNOSTIC");
    diagnostic_line(2, "ONE REQUEST. NO FLASH OR MODULE RESET.");
    ControllerIdentity identity;
    // Do not touch UART registers on a different FPGA build.
    ControllerProbeResult result = ControllerProbeResult::UnsupportedUart;
    if (*(volatile uint8_t *)0x1000000bu == 122)
        result = controller_probe(hardware, identity);
    if (result == ControllerProbeResult::Identified) {
        diagnostic_line(4, "IDENTIFIED (VERSION IS NOT IMAGE HASH)");
        diagnostic_line(5, identity.name);
        char version[] = "VERSION HEX: 0000.0000";
        const char hex[] = "0123456789ABCDEF";
        for (unsigned i = 0; i < 4; ++i) {
            version[13 + i] = hex[(identity.major >> (12 - i * 4)) & 15];
            version[18 + i] = hex[(identity.minor >> (12 - i * 4)) & 15];
        }
        diagnostic_line(6, version);
    } else {
        diagnostic_line(4, "UNKNOWN - DO NOT INSTALL");
        diagnostic_line(5, result == ControllerProbeResult::UnsupportedUart ? "UNSUPPORTED FPGA OR UART STATE" :
            result == ControllerProbeResult::Malformed ? "MALFORMED IDENTITY REPLY" :
            result == ControllerProbeResult::Overflow ? "UART OVERFLOW" : "IDENTITY REQUEST TIMED OUT");
        diagnostic_line(6, "NO AUTOMATIC RETRY");
    }
    diagnostic_line(8, "STOPPED. RECORD RESULT. MAINTENANCE EXIT.");
    for (;;) __asm__ volatile ("nop");
}
