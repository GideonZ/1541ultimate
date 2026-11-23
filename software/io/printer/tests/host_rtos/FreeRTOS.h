/*
 * The few FreeRTOS calls the printer spool makes (task notification, a one tick delay,
 * a critical section), on POSIX threads, for the host test. A tick is a millisecond.
 */
#ifndef HOST_RTOS_FREERTOS_H
#define HOST_RTOS_FREERTOS_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

typedef long BaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS pdTRUE
#define portMAX_DELAY 0xFFFFFFFFUL
#define portTICK_PERIOD_MS 1
#define configMINIMAL_STACK_SIZE 512
#define PRIO_BACKGROUND 1
typedef void (*TaskFunction_t)(void *);

// One process-wide lock, as the critical section excludes the other tasks on the device.
inline pthread_mutex_t *host_rtos_lock(void)
{
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    return &m;
}
#define portENTER_CRITICAL() pthread_mutex_lock(host_rtos_lock())

// A test can run something, once, at the next end of a critical section: the task that
// preempts there on the device, between that section and what follows it.
inline void (*&host_rtos_exit_hook(void))(void)
{
    static void (*hook)(void) = NULL;
    return hook;
}
static inline void host_rtos_exit_critical(void)
{
    pthread_mutex_unlock(host_rtos_lock());
    void (*h)(void) = host_rtos_exit_hook();
    if (h) {
        host_rtos_exit_hook() = NULL;
        h();
    }
}
#define portEXIT_CRITICAL() host_rtos_exit_critical()

// Counts the times a task slept: a test sees that a producer is waiting.
#include <atomic>
inline std::atomic<unsigned> &host_rtos_sleeps(void)
{
    static std::atomic<unsigned> n(0);
    return n;
}

struct HostTask {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    uint32_t count;
    HostTask() : count(0)
    {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&changed, NULL);
    }
};
typedef HostTask *TaskHandle_t;

inline TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    static thread_local HostTask me;
    return &me;
}

static inline BaseType_t xTaskNotifyGive(TaskHandle_t t)
{
    pthread_mutex_lock(&t->lock);
    t->count++;
    pthread_cond_signal(&t->changed);
    pthread_mutex_unlock(&t->lock);
    return pdPASS;
}

// Returns the notification count (0 on timeout), clearing it when asked.
static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    TaskHandle_t t = xTaskGetCurrentTaskHandle();
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += ticks / 1000;
    dl.tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&t->lock);
    while (t->count == 0) {
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&t->changed, &t->lock);
        } else if (pthread_cond_timedwait(&t->changed, &t->lock, &dl) == ETIMEDOUT) {
            break;
        }
    }
    uint32_t r = t->count;
    t->count = clear ? 0 : (r ? r - 1 : 0);
    pthread_mutex_unlock(&t->lock);
    return r;
}

static inline void vTaskDelay(TickType_t ticks)
{
    host_rtos_sleeps()++;
    usleep(ticks * 1000);
}


static inline TickType_t xTaskGetTickCount(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (TickType_t)(t.tv_sec * 1000UL + t.tv_nsec / 1000000UL);
}

// Binary semaphore: a count of 0 or 1, take waits at most 'ticks'.
struct HostSem {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int count;
    HostSem() : count(0)
    {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&changed, NULL);
    }
};
typedef HostSem *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return new HostSem; }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    pthread_mutex_lock(&s->lock);
    s->count = 1;
    pthread_cond_signal(&s->changed);
    pthread_mutex_unlock(&s->lock);
    return pdTRUE;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += ticks / 1000;
    dl.tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&s->lock);
    while (s->count == 0) {
        if (ticks == 0)
            break;
        if (ticks == portMAX_DELAY)
            pthread_cond_wait(&s->changed, &s->lock);
        else if (pthread_cond_timedwait(&s->changed, &s->lock, &dl) == ETIMEDOUT)
            break;
    }
    BaseType_t got = s->count ? pdTRUE : pdFALSE;
    s->count = 0;
    pthread_mutex_unlock(&s->lock);
    return got;
}

#endif
