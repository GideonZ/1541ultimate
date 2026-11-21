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

// Timeouts used ONLY on the Reset Recovery and the second attempt. A healthy device answers each of these in
// milliseconds; a dead one must not cost more than master did. Control transfers: ticks of 200 Hz per wait
// (25 = 125 ms; a stage waits at most twice, once more after the abort request). Bulk phases: ticks of 8 kHz
// (2000 = 250 ms, 8000 = 1 s for the data phase, which may legitimately NAK while the device commits a write).
// Added worst case: recovery 6 stages x 250 ms = 1.5 s, retry CBW 0.25 + data 1 + status 0.25 = 1.5 s: 3 s.
static const int c_bot_recovery_ticks = 25;
static const int c_bot_retry_short_timeout = 2000;
static const int c_bot_retry_data_timeout = 8000;

static inline bool bot_retry_after_status_fault(int status, bool already_recovered, uint16_t elapsed_ms, uint8_t opcode)
{
    return (status < 0) && !already_recovered && (elapsed_ms < c_bot_fast_fault_ms) && (opcode != c_scsi_inquiry);
}

#endif
