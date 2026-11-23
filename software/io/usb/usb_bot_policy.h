#ifndef USB_BOT_POLICY_H
#define USB_BOT_POLICY_H

#include <stdint.h>

// Bulk-Only Transport: when does a failed status phase get a Reset Recovery and one more attempt?
// Only after a FAST protocol fault (a short or invalid CSW), once per command. An attempt that already
// spent a NAK timeout (a dead or hung device) is not retried: the caller then waits no longer than it
// did before Reset Recovery existed. elapsed_ms is the time the attempt took (modulo 2^16).
// A command that sends data to the device (WRITE) is never reset or retried here: it fails as it always did. On the
// bench a SanDisk Ultra Fit wedged when a Mass Storage Reset and a retry followed a faulted write (reads and other
// sticks were fine), and a lost status of a write cannot be told from a completed one anyway.
// INQUIRY runs with short timeouts (1 s, 3 retries: 4 s) and is never retried: a retry plus recovery could
// take longer than that. It is only issued while a device is being set up, and a failure there is final.
static const uint16_t c_bot_fast_fault_ms = 1000;

static const uint8_t c_scsi_inquiry = 0x12;

// Timeouts of the Reset Recovery and the second attempt. A healthy device answers each of these in milliseconds; a
// dead one must not cost more than master did. Control transfers keep the default 100 ticks of 200 Hz (0.5 s per wait,
// a stage waits at most twice, once more after the abort request). They were shortened to 125 ms once and a SanDisk
// stick wedged on hardware, twice: aborting a Mass Storage Reset mid-way can leave the device hung. Bulk phases: ticks of 8 kHz
// (8000 = 1 s for the CBW only). The data and status phases of the second attempt keep the normal 3 s: a
// device that is executing the command (a write commit) NAKs the status for a while, and a 250 ms timeout there
// aborted the command mid-flight and hung a SanDisk stick on hardware.
// Added worst case after a fast fault on a dead device: recovery 6 stages x 1 s = 6 s, retry CBW 1 + data 3 + status 3 = 7 s: 13 s.
static const int c_bot_recovery_ticks = 100;

// t_pipe.DevEP keeps only the endpoint number; CLEAR_FEATURE(ENDPOINT_HALT) needs the endpoint address,
// whose bit 7 is the direction (set for IN).
static inline uint8_t bot_in_endpoint(uint16_t dev_ep)  { return uint8_t((dev_ep & 0x0F) | 0x80); }
static inline uint8_t bot_out_endpoint(uint16_t dev_ep) { return uint8_t(dev_ep & 0x0F); }
static const int c_bot_retry_cbw_timeout = 8000;
static const int c_bot_retry_phase_timeout = 24000;

static inline bool bot_retry_after_status_fault(int status, bool already_recovered, uint16_t elapsed_ms, uint8_t opcode, bool data_out)
{
    return (status < 0) && !already_recovered && (elapsed_ms < c_bot_fast_fault_ms) && (opcode != c_scsi_inquiry) && !data_out;
}

// Bulk IN: a packet shorter than requested ends a transfer, but only when the packet was accepted (the data toggle
// flipped). A packet the controller discarded as a duplicate (toggle unchanged, nothing stored) says nothing about the
// device: the caller must poll again.
static inline bool bot_in_stop_on_short(bool packet, uint16_t toggle_before, uint16_t toggle_after, int transferred, int requested)
{
    return packet && (transferred < requested) && (toggle_before != toggle_after);
}

// A device may end the data phase early and send the CSW in place of the missing data. The host then has taken the CSW
// for data and must not read a status that is already gone. The last 13 bytes of a short data phase are a CSW when they
// carry the signature "USBS" and the tag of this command (little endian).
//   e_tail_not_csw:    the short data is data; read the status phase as usual.
//   e_tail_retry_read: a CSW with status 0 and residue 0, but bytes are missing. The device claims success while it
//                      delivered less than it was asked for, and it is in step (its status was sent): issue the
//                      command once more, without a Reset Recovery. Only once per command.
//   e_tail_use_csw:    any other CSW (status 1 or 2, a residue, or a second occurrence): it is the status of this
//                      command; the status phase must not be read, and the usual error handling applies.
enum BotShortTail { e_tail_not_csw, e_tail_retry_read, e_tail_use_csw };

static inline uint32_t bot_le32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static inline BotShortTail bot_short_data_tail(const uint8_t *data, int received, int requested, uint32_t tag, bool already_retried)
{
    if ((received < 13) || (received >= requested))
        return e_tail_not_csw;
    const uint8_t *csw = data + received - 13;
    if ((bot_le32(csw) != 0x53425355) || (bot_le32(csw + 4) != tag))
        return e_tail_not_csw;
    uint32_t residue = bot_le32(csw + 8);
    if ((csw[12] == 0) && (residue == 0) && !already_retried)
        return e_tail_retry_read;
    return e_tail_use_csw;
}

// After a Reset Recovery the unit is read-only until it is re-enumerated (a replug makes a new UsbScsi). On the bench a
// SanDisk Ultra Fit hung on the first write after a Bulk-Only reset, while reads were fine and another stick wrote fine.
// A locked unit reports STA_PROTECT (FatFs then refuses a write open with FR_WRITE_PROTECTED) and refuses a write
// before it touches the device, so a recovery can never tear a write.
// A command that sends data to the device is refused once the unit is read-only. The check runs where the driver
// mutex is taken, so a write that queued on the mutex while another command recovered is refused too.
static inline bool bot_refuse_write(bool read_only, bool data_out)
{
    return read_only && data_out;
}

struct BotWriteLock
{
    bool locked;
    BotWriteLock() : locked(false) { }
    void on_recovery() { locked = true; }
    bool refuses_write() const { return locked; }
};

#endif
