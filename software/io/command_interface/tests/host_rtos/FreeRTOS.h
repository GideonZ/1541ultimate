/*
 * The few FreeRTOS calls that the UCI Server and the HTTP target make, on POSIX threads,
 * for the host tests. A tick is a millisecond. A critical section is one process-wide
 * lock, so it excludes the other threads as it excludes the other tasks on the device.
 */
#ifndef HOST_RTOS_FREERTOS_H
#define HOST_RTOS_FREERTOS_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS pdTRUE
#define errQUEUE_FULL 0
#define portMAX_DELAY 0xFFFFFFFFUL
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configMINIMAL_STACK_SIZE 512
#define PRIO_HW_SERVICE 1
#define PRIO_NETSERVICE 1
#define PRIO_C64_SERVICE 2

// inline, not static: one lock for the whole program, not one per file.
inline pthread_mutex_t *host_rtos_critical(void)
{
    static pthread_mutex_t m;
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    struct init {
        static void run(void)
        {
            pthread_mutexattr_t a;
            pthread_mutexattr_init(&a);
            pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&m, &a);
        }
    };
    pthread_once(&once, init::run);
    return &m;
}
#define portENTER_CRITICAL() pthread_mutex_lock(host_rtos_critical())
#define portEXIT_CRITICAL() pthread_mutex_unlock(host_rtos_critical())

typedef struct HostQueue {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int length, item_size, count, head;
    uint8_t *items;
} *QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;
typedef pthread_t *TaskHandle_t;

static inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    QueueHandle_t q = new HostQueue;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->changed, NULL);
    q->length = (int)length;
    q->item_size = (int)item_size;
    q->count = q->head = 0;
    q->items = new uint8_t[length * (item_size ? item_size : 1)];
    return q;
}

static inline void host_rtos_deadline(struct timespec *t, TickType_t ticks)
{
    clock_gettime(CLOCK_REALTIME, t);
    t->tv_sec += ticks / 1000;
    t->tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_sec++;
        t->tv_nsec -= 1000000000L;
    }
}

// Waits while the queue is full (send) or empty (receive), at most `ticks`.
static inline bool host_rtos_wait(QueueHandle_t q, bool sending, TickType_t ticks)
{
    struct timespec deadline;
    host_rtos_deadline(&deadline, ticks);
    while (sending ? (q->count == q->length) : (q->count == 0)) {
        if (ticks == 0) {
            return false;
        }
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&q->changed, &q->lock);
        } else if (pthread_cond_timedwait(&q->changed, &q->lock, &deadline) == ETIMEDOUT) {
            return !(sending ? (q->count == q->length) : (q->count == 0));
        }
    }
    return true;
}

// Called after a task posts to a queue: a test can run, there and then, the higher task
// that the post would wake on the device.
inline void (*&host_rtos_send_hook(void))(QueueHandle_t)
{
    static void (*hook)(QueueHandle_t) = NULL;
    return hook;
}

static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    pthread_mutex_lock(&q->lock);
    bool room = host_rtos_wait(q, true, ticks);
    if (room) {
        memcpy(q->items + ((q->head + q->count) % q->length) * q->item_size, item, q->item_size);
        q->count++;
        pthread_cond_broadcast(&q->changed);
    }
    pthread_mutex_unlock(&q->lock);
    if (room && host_rtos_send_hook()) {
        host_rtos_send_hook()(q);
    }
    return room ? pdTRUE : errQUEUE_FULL;
}

static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    pthread_mutex_lock(&q->lock);
    bool got = host_rtos_wait(q, false, ticks);
    if (got) {
        memcpy(item, q->items + q->head * q->item_size, q->item_size);
        q->head = (q->head + 1) % q->length;
        q->count--;
        pthread_cond_broadcast(&q->changed);
    }
    pthread_mutex_unlock(&q->lock);
    return got ? pdTRUE : pdFALSE;
}

static inline BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return xQueueSend(q, item, 0);
}

static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    pthread_mutex_lock(&q->lock);
    int n = q->count;
    pthread_mutex_unlock(&q->lock);
    return (UBaseType_t)n;
}

static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q)
{
    pthread_mutex_lock(&q->lock);
    int n = q->length - q->count;
    pthread_mutex_unlock(&q->lock);
    return (UBaseType_t)n;
}

static inline void vQueueDelete(QueueHandle_t q)
{
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->changed);
    delete[] q->items;
    delete q;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xQueueCreate(1, 0); }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { return xQueueSend(s, "", 0); }
static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken) { return xQueueSendFromISR(s, "", woken); }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) { uint8_t b; return xQueueReceive(s, &b, ticks); }

struct HostTask {
    TaskFunction_t code;
    void *parameter;
    static void *run(void *t)
    {
        HostTask task = *(HostTask *)t;
        delete (HostTask *)t;
        task.code(task.parameter);
        return NULL;
    }
};

static inline BaseType_t xTaskCreate(TaskFunction_t code, const char *, uint32_t, void *parameter,
                                     UBaseType_t, TaskHandle_t *handle)
{
#ifdef HOST_RTOS_NO_TASKS
    // The test runs the task's work itself, one step at a time.
    if (handle) {
        *handle = NULL;
    }
    return pdPASS;
#endif
    pthread_t *thread = new pthread_t;
    HostTask *task = new HostTask;
    task->code = code;
    task->parameter = parameter;
    if (pthread_create(thread, NULL, HostTask::run, task) != 0) {
        delete task;
        delete thread;
        return pdFALSE;
    }
    pthread_detach(*thread);
    if (handle) {
        *handle = thread;
    } else {
        delete thread;
    }
    return pdPASS;
}

// The priority of the calling task, as the tests last set it; enough for a test that
// runs a task's work on its own thread.
inline UBaseType_t *host_rtos_priority(void)
{
    static __thread UBaseType_t priority = 0;
    return &priority;
}
static inline void vTaskPrioritySet(TaskHandle_t, UBaseType_t priority) { *host_rtos_priority() = priority; }
static inline UBaseType_t uxTaskPriorityGet(TaskHandle_t) { return *host_rtos_priority(); }

static inline void vTaskDelay(TickType_t ticks)
{
    struct timespec t = { (time_t)(ticks / 1000), (long)(ticks % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

#endif
