#include <stdio.h>
#include <string.h>
#include "../usb_bot_policy.h"

// One nano command moves packets until one is not full. Every accepted packet flips the data toggle (a discarded
// duplicate does not and stores nothing). Returns the stop decision for k full packets followed by the last packet.
enum LastPacket { e_short, e_zlp, e_discard };
static bool pass_stops(int k, int mps, LastPacket last, int toggle_start, int requested)
{
    int flips = k + ((last == e_discard) ? 0 : 1);
    int transferred = k * mps + ((last == e_short) ? mps / 2 : 0);
    uint16_t before = toggle_start ? 0x0800 : 0;
    uint16_t after = (toggle_start ^ (flips & 1)) ? 0x0800 : 0;
    return bot_in_stop_on_short(true, before, after, transferred, requested, mps);
}

static bool source_has(const char *path, const char *text)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return false; }
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, text) != NULL;
}

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    // the reporter's case: a 1-byte CSW arrives at once
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x28, false));
    // one NAK timeout (3 s) was already spent: no retry, same wait as before
    CHECK(!bot_retry_after_status_fault(-1, false, 3000, 0x28, false));
    CHECK(!bot_retry_after_status_fault(-1, false, 1000, 0x28, false));
    CHECK(bot_retry_after_status_fault(-1, false, 999, 0x28, false));
    // only once per command
    CHECK(!bot_retry_after_status_fault(-1, true, 10, 0x28, false));
    // a good status (0, 1, 2) is never retried
    CHECK(!bot_retry_after_status_fault(0, false, 10, 0x28, false));
    CHECK(!bot_retry_after_status_fault(1, false, 10, 0x28, false));
    CHECK(!bot_retry_after_status_fault(2, false, 10, 0x28, false));
    // INQUIRY has 4 s timeouts; retry plus recovery could exceed them, so it is never retried
    CHECK(!bot_retry_after_status_fault(-1, false, 10, 0x12, false));
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x00, false)); // TEST UNIT READY
    CHECK(!bot_retry_after_status_fault(-1, false, 10, 0x2A, true)); // WRITE(10): data out, never retried
    CHECK(!bot_retry_after_status_fault(-1, false, 10, 0x00, true)); // any data-out command
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x28, false)); // READ(10)
    CHECK(bot_retry_after_status_fault(-1, false, 10, 0x00, false)); // TEST UNIT READY (out flag set, no data)
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
    // read-only after a recovery, until the unit is replaced by a new one (re-enumeration)
    { BotWriteLock w; CHECK(!w.refuses_write()); w.on_recovery(); CHECK(w.refuses_write()); w.on_recovery(); CHECK(w.refuses_write());
      BotWriteLock fresh; CHECK(!fresh.refuses_write()); }
    // a write that queued on the driver mutex during a recovery is refused when it gets the mutex; reads and TUR are not
    CHECK(bot_refuse_write(true, true));
    CHECK(!bot_refuse_write(true, false));
    CHECK(!bot_refuse_write(false, true));
    CHECK(!bot_refuse_write(false, false));
    // stop_on_short: only an accepted packet (toggle flipped) ends the transfer
    CHECK(bot_in_stop_on_short(true, 0, 0x0800, 13, 499, 512));       // accepted short packet: stop
    CHECK(bot_in_stop_on_short(true, 0x0800, 0, 0, 512, 512));        // accepted zero length packet: stop
    CHECK(!bot_in_stop_on_short(true, 0, 0, 0, 13, 13));              // discarded duplicate (toggle unchanged): poll again
    CHECK(!bot_in_stop_on_short(true, 0x0800, 0x0800, 0, 512, 512));
    CHECK(!bot_in_stop_on_short(true, 0, 0x0800, 512, 512, 512));     // full packet: not short
    CHECK(!bot_in_stop_on_short(false, 0, 0x0800, 0, 512, 512));      // a NAK timeout is not a packet
    // k full packets, then a short packet, a zero length packet or a discarded duplicate, for both toggle starts
    {
        const int mpss[2] = { 512, 64 };
        for (int m = 0; m < 2; m++)
            for (int k = 0; k <= 3; k++)
                for (int t = 0; t < 2; t++)
                {
                    const int mps = mpss[m], req = 8 * mps;
                    CHECK(pass_stops(k, mps, e_short, t, req));    // accepted short packet: stop
                    CHECK(pass_stops(k, mps, e_zlp, t, req));      // accepted zero length packet: stop
                    CHECK(!pass_stops(k, mps, e_discard, t, req)); // discarded duplicate: poll again
                    // the pass ended because the requested length was reached: nothing to stop
                    CHECK(!bot_in_stop_on_short(true, 0, 0, req, req, mps));
                    CHECK(!bot_in_stop_on_short(true, 0, 0x0800, req, req, mps));
                }
    }
    // the call site: the data IN phase asks for stop_on_short and looks for a CSW in a short tail
    CHECK(source_has("../usb_scsi.cc", "host->bulk_in(&bulk_in, data, datalen, timeout, retries, true)"));
    CHECK(source_has("../usb_scsi.cc", "bot_short_data_tail((const uint8_t *)data, len, datalen, cbw.tag, short_retried)"));
    CHECK(source_has("../usb_base.cc", "bot_in_stop_on_short(is_packet, toggle_before, toggle_after, transferred, current_len, descr->maxTrans)"));
    // short data phase that ends in a CSW
    {
        uint8_t d[512 + 13 + 486];
        memset(d, 0xA5, sizeof d);
        const int got = 512 + 13, want = sizeof d;
        const uint32_t tag = 0x11223344;
        uint8_t *csw = d + 512;
        csw[0] = 'U'; csw[1] = 'S'; csw[2] = 'B'; csw[3] = 'S';
        memcpy(csw + 4, &tag, 4); // the device echoes the raw bytes of the CBW tag
        memset(csw + 8, 0, 5);
        // (i) matching tag, status 0, residue 0, bytes missing: retry the READ once, no reset recovery
        CHECK(bot_short_data_tail(d, got, want, tag, false) == e_tail_retry_read);
        CHECK(bot_short_data_tail(csw, 13, want, tag, false) == e_tail_retry_read);    // CSW is the first packet
        CHECK(bot_short_data_tail(d, got, want, tag, true) == e_tail_use_csw);         // only once per command
        // (ii) status 1, status 2 or a residue: the CSW is the status, fault handling as usual
        csw[12] = 1; CHECK(bot_short_data_tail(d, got, want, tag, false) == e_tail_use_csw);
        csw[12] = 2; CHECK(bot_short_data_tail(d, got, want, tag, false) == e_tail_use_csw);
        csw[12] = 0; csw[8] = 0x00; csw[9] = 0x02; CHECK(bot_short_data_tail(d, got, want, tag, false) == e_tail_use_csw);
        csw[8] = 0; csw[9] = 0;
        // (iii) not a CSW: wrong signature, wrong tag, too short, or not short at all
        csw[0] = 'X'; CHECK(bot_short_data_tail(d, got, want, tag, false) == e_tail_not_csw);
        csw[0] = 'U';
        CHECK(bot_short_data_tail(d, got, want, tag + 1, false) == e_tail_not_csw);
        CHECK(bot_short_data_tail(d, 12, want, tag, false) == e_tail_not_csw);
        CHECK(bot_short_data_tail(d, 0, want, tag, false) == e_tail_not_csw);
        CHECK(bot_short_data_tail(d, got, got, tag, false) == e_tail_not_csw);         // nothing missing
        CHECK(bot_short_data_tail(d, want, want, tag, false) == e_tail_not_csw);
        CHECK(bot_short_data_tail(d, got - 1, want, tag, false) == e_tail_not_csw);    // the 13 bytes do not end the data
    }
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("usb_bot_policy_test OK\n");
    return 0;
}
