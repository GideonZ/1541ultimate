/*
 * boot_test.cc
 *
 * The U64-II / C64 Ultimate's WiFi application when it finds its module (see
 * wifi_app_script.h).
 *
 * CASES_FOR: U64=2
 */

#include "wifi_app_script.h"

#if U64 == 2
// The U64-II / C64 Ultimate reads the input voltage when it finds its module: a module that
// has no reading to give says so, and that is not a low voltage
static void voltages_at_boot(void)
{
    voltages_result = 0x107;
    boot(true, 0, NULL, 0);
    CHECK(strchr(rlog, '!') == NULL, "no reading: no Low input voltage");
    voltages_result = 0;
    voltages_vbus = 7000;
    boot(true, 0, NULL, 0);
    CHECK(strchr(rlog, '!') != NULL, "a low reading: the message");
    voltages_vbus = 9000;
    boot(true, 0, NULL, 0);
    CHECK(strchr(rlog, '!') == NULL, "a good reading: none");
}
#endif


void run_cases(void)
{
#if U64 == 2
    voltages_at_boot();
#endif
}
