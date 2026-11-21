#ifndef USB_BOT_POLICY_H
#define USB_BOT_POLICY_H

#include <stdint.h>

// Bulk-Only Transport: when does a failed status phase get a Reset Recovery and one more attempt?
// Only after a FAST protocol fault (a short or invalid CSW), once per command. An attempt that already
// spent a NAK timeout (a dead or hung device) is not retried: the caller then waits no longer than it
// did before Reset Recovery existed. elapsed_ms is the time the attempt took (modulo 2^16).
// INQUIRY runs with short timeouts (1 s, 3 retries: 4 s) and is never retried: a retry plus recovery could
// take longer than that. It is only issued while a device is being set up, and a failure there is final.
static const uint16_t c_bot_fast_fault_ms = 1000;

static const uint8_t c_scsi_inquiry = 0x12;

static inline bool bot_retry_after_status_fault(int status, bool already_recovered, uint16_t elapsed_ms, uint8_t opcode)
{
    return (status < 0) && !already_recovered && (elapsed_ms < c_bot_fast_fault_ms) && (opcode != c_scsi_inquiry);
}

#endif
