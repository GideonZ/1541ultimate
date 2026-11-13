// Regression tests for GideonZ/1541ultimate#1039: the UCI HTTP target dropped
// bytes when the server read slowly.
//
// send() may take only part of what it is given and return the count, and a
// non-blocking send() fails with EWOULDBLOCK while the send buffer is full.
// HttpRequest::send_request ignored the first and gave up on the second, so a
// slow printer got 21 of 23 KB and the request still counted as sent.
//
// The send() here is a model of lwIP's lwip_send on a TCP socket: each call
// takes at most a set number of bytes, and a MSG_DONTWAIT call meets a full
// buffer after a set number of calls. A blocking call waits instead, so it
// never meets one; that the wait really happens is shown against a real socket
// by --slow-server in software/io/command_interface/test_http_target.cc.

#include "../../usb/tests/host_test/host_test.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <vector>

namespace {

struct FakeSocket {
    int max_per_call = 1 << 20;   // bytes one call takes at most
    int full_after = -1;          // MSG_DONTWAIT calls before the buffer is full; -1: never
    int fail_at_call = -1;        // call that times out with nothing taken; -1: none
    int calls = 0;
    int interrupt_at_call = -1;
    std::vector<char> received;
};

FakeSocket *fake;

int fake_send(int, const void *data, size_t len, int flags)
{
    int call = fake->calls++;
    if (call == fake->interrupt_at_call) {
        errno = EINTR;
        return -1;
    }
    if (call == fake->fail_at_call) {
        errno = EWOULDBLOCK;    // what SO_SNDTIMEO gives when nothing was taken
        return -1;
    }
    if ((flags & MSG_DONTWAIT) && fake->full_after >= 0 && call >= fake->full_after) {
        errno = EWOULDBLOCK;
        return -1;
    }
    int now = ((int)len < fake->max_per_call) ? (int)len : fake->max_per_call;
    const char *p = (const char *)data;
    fake->received.insert(fake->received.end(), p, p + now);
    return now;
}

} // namespace

// <sys/socket.h> is already in, so the header's own include of it adds nothing
// and send() in it is the fake.
#define send fake_send
#ifdef SEND_ALL_LEGACY
#include "send_all_legacy.h"   // the pre-fix behaviour: the red half
#else
#include "../../../network/send_all.h"
#endif
#undef send

namespace {

// One chunk as send_request reads it from the request stream.
std::vector<char> chunk(int len)
{
    std::vector<char> data(len);
    for (int i = 0; i < len; i++) {
        data[i] = (char)(0x20 + (i % 0x5f));
    }
    return data;
}

} // namespace

TEST(SendAll, SendsTheRestWhenSendTakesOnlyPart)
{
    FakeSocket s;
    s.max_per_call = 50;
    fake = &s;
    std::vector<char> data = chunk(128);

    EXPECT_EQ(send_all(3, data.data(), (int)data.size()), 0);
    EXPECT_TRUE(s.received == data);
}

TEST(SendAll, WaitsForAFullBufferInsteadOfGivingUp)
{
    FakeSocket s;
    s.full_after = 0;
    fake = &s;
    std::vector<char> data = chunk(128);

    EXPECT_EQ(send_all(3, data.data(), (int)data.size()), 0);
    EXPECT_TRUE(s.received == data);
}

TEST(SendAll, ReportsATimeoutAfterPartOfTheChunk)
{
    FakeSocket s;
    s.max_per_call = 50;
    s.fail_at_call = 1;
    fake = &s;
    std::vector<char> data = chunk(128);

    EXPECT_TRUE(send_all(3, data.data(), (int)data.size()) < 0);
    EXPECT_EQ(s.calls, 2);
}

TEST(SendAll, InterruptedAfterPartialWriteKeepsTheUnsentSuffix)
{
    FakeSocket s;
    s.max_per_call = 50;
    s.interrupt_at_call = 1;
    fake = &s;
    std::vector<char> data = chunk(128);
    EXPECT_EQ(send_all(3, data.data(), (int)data.size()), 0);
    EXPECT_TRUE(s.received == data);
    EXPECT_EQ(s.calls, 4);
}

TEST(SendAll, ReportsAFailureWhenSendTakesNothing)
{
    FakeSocket s;
    s.max_per_call = 0;
    fake = &s;
    std::vector<char> data = chunk(128);

    EXPECT_TRUE(send_all(3, data.data(), (int)data.size()) < 0);
    EXPECT_EQ(s.calls, 1);
}

TEST(SendAll, SendsNothingForAnEmptyChunk)
{
    FakeSocket s;
    fake = &s;

    EXPECT_EQ(send_all(3, "", 0), 0);
    EXPECT_EQ(s.calls, 0);
}

#ifndef SEND_ALL_LEGACY
// The HTTP target's worker waits on a send in short slices, so that it can be
// cancelled. A slice that runs out after part of a chunk must say how much went,
// so that the next slice sends the rest and not the whole chunk again.
TEST(SendAll, SendPartTellsHowMuchWentBeforeATimeout)
{
    FakeSocket s;
    s.max_per_call = 50;
    s.fail_at_call = 1;
    fake = &s;
    std::vector<char> data = chunk(128);

    int sent = -1;
    EXPECT_TRUE(send_part(3, data.data(), (int)data.size(), &sent) < 0);
    EXPECT_EQ(sent, 50);
    int rest = -1;
    EXPECT_EQ(send_part(3, data.data() + sent, (int)data.size() - sent, &rest), 0);
    EXPECT_EQ(rest, 78);
    EXPECT_TRUE(s.received == data);
}
#endif
