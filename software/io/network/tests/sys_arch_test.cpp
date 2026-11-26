// The ticks network/sys_arch.c asks FreeRTOS to wait for an lwIP wait in milliseconds.
// The tick is 5 ms (portTICK_PERIOD_MS). A wait of 1-4 ms used to be truncated to 0 ticks,
// a poll that returns at once: tcpip_mbox_fetch() (lwip/src/api/tcpip.c) then finds its
// timer not yet due (sys_now() moves in steps of 5 ms) and polls again, so the tcpip
// thread, at priority 2, spun until the tick moved. A wait has to be rounded up, and so
// never be 0 ticks.
//
// sys_arch.c is built as it is, against the stand-ins of sys_arch_host/; the waits record
// what they were asked for and time out at once. SYS_ARCH_TICKS_LEGACY (Makefile, target
// sys-arch-red) builds the same cases against sys_arch.c with the truncating division.

#include "../../usb/tests/host_test/host_test.h"
extern "C" {
#include "lwip/sys.h"
}

namespace {

TickType_t asked_receive; // the ticks of the last xQueueReceive
TickType_t asked_take;    // and of the last xSemaphoreTake
int dummy_queue, dummy_sem;

TickType_t mbox_ticks(u32_t ms)
{
    sys_mbox_t mbox = &dummy_queue;
    void *msg;
    asked_receive = 12345;
    EXPECT_EQ(sys_arch_mbox_fetch(&mbox, &msg, ms), (u32_t)SYS_ARCH_TIMEOUT);
    return asked_receive;
}

TickType_t sem_ticks(u32_t ms)
{
    sys_sem_t sem = &dummy_sem;
    asked_take = 12345;
    EXPECT_EQ(sys_arch_sem_wait(&sem, ms), (u32_t)SYS_ARCH_TIMEOUT);
    return asked_take;
}

} // namespace

// The FreeRTOS calls of sys_arch.c
#pragma GCC diagnostic ignored "-Wunused-parameter"
extern "C" {
TickType_t xTaskGetTickCount(void) { return 1000; }
BaseType_t xTaskCreate(TaskFunction_t f, const char *n, uint16_t s, void *p, UBaseType_t pr, TaskHandle_t *h) { return pdFALSE; }
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size) { return &dummy_queue; }
void vQueueDelete(QueueHandle_t q) { }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks) { return pdFALSE; }
BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t ticks) { return pdFALSE; }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken) { return pdFALSE; }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks) { asked_receive = ticks; return pdFALSE; }
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken) { return pdFALSE; }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return 0; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &dummy_sem; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &dummy_sem; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) { asked_take = ticks; return pdFALSE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken) { return pdTRUE; }
}

// Shorter than a tick: one tick, not a poll
TEST(SysArchTicks, mbox_wait_below_a_tick_is_one_tick)
{
    EXPECT_EQ(mbox_ticks(1), (TickType_t)1);
    EXPECT_EQ(mbox_ticks(3), (TickType_t)1);
    EXPECT_EQ(mbox_ticks(4), (TickType_t)1);
}

TEST(SysArchTicks, sem_wait_below_a_tick_is_one_tick)
{
    EXPECT_EQ(sem_ticks(1), (TickType_t)1);
    EXPECT_EQ(sem_ticks(4), (TickType_t)1);
}

// Rounded up, never shorter than asked; whole ticks are unchanged
TEST(SysArchTicks, waits_round_up_to_whole_ticks)
{
    EXPECT_EQ(mbox_ticks(5), (TickType_t)1);
    EXPECT_EQ(mbox_ticks(6), (TickType_t)2);
    EXPECT_EQ(mbox_ticks(119), (TickType_t)24); // the longest mDNS response delay
    EXPECT_EQ(mbox_ticks(1000), (TickType_t)200);
    EXPECT_EQ(sem_ticks(6), (TickType_t)2);
    EXPECT_EQ(sem_ticks(1000), (TickType_t)200);
}

// The longest wait does not overflow into a short one
TEST(SysArchTicks, longest_wait_does_not_wrap)
{
    EXPECT_EQ(mbox_ticks(0xFFFFFFFEu), (TickType_t)858993459);
    EXPECT_EQ(sem_ticks(0xFFFFFFFCu), (TickType_t)858993459);
}
