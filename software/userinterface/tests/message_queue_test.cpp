#include "host_test/host_test.h"
#include "message_queue_env.h"
#include <new>
#include <stdlib.h>

extern "C" void outbyte(int) { }

QueueHandle_t userMessageQueue = 0;   // as in userinterface.cc

// live count of scalar new/delete: the posted messages are mstring objects
static int live_objects = 0;
void *operator new(size_t n) { live_objects++; void *p = malloc(n); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void *p) noexcept { if (p) live_objects--; free(p); }
void operator delete(void *p, size_t) noexcept { if (p) live_objects--; free(p); }

#include "output/post_message.cc"

// The queue holds 4 messages and only a menu drains it. Posts beyond that cannot be
// delivered; whoever posted them has given up ownership, so postMessage must free them.
static void prime_queue(void)
{
    // the queue is created by the first post and lives on: not part of the count
    UserInterface::postMessage("prime");
    delete UserInterface::getMessage();
}

TEST(PostMessageTest, MessagesRefusedByAFullQueueAreFreed)
{
    prime_queue();
    int base = live_objects;
    for (int i = 0; i < 4; i++) {
        UserInterface::postMessage("queued");
    }
    int queued = live_objects - base;
    for (int i = 0; i < 100; i++) {
        UserInterface::postMessage("one too many");   // e.g. repeated REST config writes
    }
    EXPECT_EQ(queued, live_objects - base);           // nothing more is held than the queue's 4
    int received = 0;
    mstring *m;
    while ((m = UserInterface::getMessage()) != NULL) {
        received++;
        delete m;
    }
    EXPECT_EQ(4, received);
    EXPECT_EQ(base, live_objects);
}

TEST(PostMessageTest, DrainedQueueAcceptsMessagesAgain)
{
    prime_queue();
    int base = live_objects;
    for (int round = 0; round < 3; round++) {
        UserInterface::postMessage("a");
        UserInterface::postMessage("b");
        mstring *m = UserInterface::getMessage();
        ASSERT_TRUE(m != NULL);
        EXPECT_EQ(std::string("a"), std::string(m->c_str()));
        delete m;
        m = UserInterface::getMessage();
        ASSERT_TRUE(m != NULL);
        delete m;
    }
    EXPECT_EQ(base, live_objects);
}
