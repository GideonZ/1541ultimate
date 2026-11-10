// The pool of reference pbufs (MEMP_PBUF, MEMP_NUM_PBUF entries). Every UDP socket send
// takes one while it runs: lwip_sendto() wraps the caller's data with netbuf_ref(), which
// is pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_REF) (api/netbuf.c), and fails with ERR_MEM,
// errno ENOMEM, when the pool is empty. Several tasks send UDP at once on the device: the
// network target for the C64, syslog, the ident responder, the streams. The firmware's
// MEMP_NUM_PBUF (software/network/config/lwipopts.h, taken by the Makefile) has to leave
// a send room while others are in flight.
//
// REF_POOL_LEGACY builds the same cases with the single entry the firmware had.

#include "../../usb/tests/host_test/host_test.h"

extern "C" {
#include "lwip/init.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
}

extern "C" u32_t sys_now(void) { return 0; }

namespace {

uint8_t payload[64];

// What netbuf_ref() does for each send
struct pbuf *send_ref(void)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_REF);
    if (p) {
        p->payload = payload;
        p->len = p->tot_len = sizeof(payload);
    }
    return p;
}

struct Pool {
    Pool() { static bool done = false; if (!done) { lwip_init(); done = true; } }
};

} // namespace

TEST(RefPool, ASendWhileAnotherIsInFlightGetsItsPbuf)
{
    Pool pool;
    struct pbuf *other = send_ref();   // syslog's send, still in lwip_sendto
    ASSERT_TRUE(other != NULL);
    struct pbuf *mine = send_ref();    // the network target's send
    EXPECT_TRUE(mine != NULL);
    if (mine) pbuf_free(mine);
    pbuf_free(other);
}

TEST(RefPool, FourSendersAtOnce)
{
    Pool pool;
    struct pbuf *p[4];
    for (int i = 0; i < 4; i++) {
        p[i] = send_ref();
        EXPECT_TRUE(p[i] != NULL);
    }
    for (int i = 0; i < 4; i++) {
        if (p[i]) pbuf_free(p[i]);
    }
}

TEST(RefPool, TheEntriesComeBack)
{
    Pool pool;
    for (int round = 0; round < 100; round++) {
        struct pbuf *a = send_ref();
        struct pbuf *b = send_ref();
        EXPECT_TRUE((a != NULL) && (b != NULL));
        if (a) pbuf_free(a);
        if (b) pbuf_free(b);
    }
}
