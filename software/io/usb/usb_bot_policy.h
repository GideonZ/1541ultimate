#ifndef USB_BOT_POLICY_H
#define USB_BOT_POLICY_H

#include <stdint.h>

// Bulk-Only Transport: when does a failed status phase get a Reset Recovery and one more attempt?
// Only after a FAST protocol fault (a short or invalid CSW), once per command. An attempt that already
// spent a NAK timeout (a dead or hung device) is not retried: the caller then waits no longer than it
// did before Reset Recovery existed. elapsed_ms is the time the attempt took (modulo 2^16).
static const uint16_t c_bot_fast_fault_ms = 1000;

static inline bool bot_retry_after_status_fault(int status, bool already_recovered, uint16_t elapsed_ms)
{
    return (status < 0) && !already_recovered && (elapsed_ms < c_bot_fast_fault_ms);
}

#endif
