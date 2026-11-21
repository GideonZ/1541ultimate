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
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("usb_bot_policy_test OK\n");
    return 0;
}
