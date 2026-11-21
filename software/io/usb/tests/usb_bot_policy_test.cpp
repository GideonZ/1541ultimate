#include <stdio.h>
#include "../usb_bot_policy.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    // the reporter's case: a 1-byte CSW arrives at once
    CHECK(bot_retry_after_status_fault(-1, false, 10));
    // one NAK timeout (3 s) was already spent: no retry, same wait as before
    CHECK(!bot_retry_after_status_fault(-1, false, 3000));
    CHECK(!bot_retry_after_status_fault(-1, false, 1000));
    CHECK(bot_retry_after_status_fault(-1, false, 999));
    // only once per command
    CHECK(!bot_retry_after_status_fault(-1, true, 10));
    // a good status (0, 1, 2) is never retried
    CHECK(!bot_retry_after_status_fault(0, false, 10));
    CHECK(!bot_retry_after_status_fault(1, false, 10));
    CHECK(!bot_retry_after_status_fault(2, false, 10));
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("usb_bot_policy_test OK\n");
    return 0;
}
