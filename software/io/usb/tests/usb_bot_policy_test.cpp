#include <stdio.h>
#include "../usb_bot_policy.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    // the reporter's case: a 1-byte CSW arrives at once
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x28));
    // one NAK timeout (3 s) was already spent: no retry, same wait as before
    CHECK(!bot_retry_after_status_fault(-1, false, 3000, 0x28));
    CHECK(!bot_retry_after_status_fault(-1, false, 1000, 0x28));
    CHECK(bot_retry_after_status_fault(-1, false, 999, 0x28));
    // only once per command
    CHECK(!bot_retry_after_status_fault(-1, true, 10, 0x28));
    // a good status (0, 1, 2) is never retried
    CHECK(!bot_retry_after_status_fault(0, false, 10, 0x28));
    CHECK(!bot_retry_after_status_fault(1, false, 10, 0x28));
    CHECK(!bot_retry_after_status_fault(2, false, 10, 0x28));
    // INQUIRY has 4 s timeouts; retry plus recovery could exceed them, so it is never retried
    CHECK(!bot_retry_after_status_fault(-1, false, 10, 0x12));
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x00)); // TEST UNIT READY
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x2A)); // WRITE(10)
    // the cost the recovery and the retry can add (hardware timeouts; see usb_bot_policy.h)
    int recovery_ms = 6 * 2 * c_bot_recovery_ticks * 5;               // 6 stages, 2 waits each, 200 Hz ticks
    int retry_ms = (c_bot_retry_cbw_timeout + 2 * c_bot_retry_phase_timeout) / 8; // CBW, data, status; 8 kHz ticks
    CHECK(recovery_ms == 6000);
    CHECK(retry_ms == 7000);
    CHECK(c_bot_recovery_ticks == 100); // the control transfers keep the default; 25 wedged a SanDisk on hardware
    CHECK(recovery_ms + retry_ms <= 13000);
    // CLEAR_FEATURE needs the direction bit: IN endpoint 1 is 0x81, OUT endpoint 2 is 0x02
    CHECK(bot_in_endpoint(1) == 0x81);
    CHECK(bot_in_endpoint(0x81) == 0x81);
    CHECK(bot_out_endpoint(2) == 0x02);
    CHECK(bot_out_endpoint(0x82) == 0x02);
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("usb_bot_policy_test OK\n");
    return 0;
}
