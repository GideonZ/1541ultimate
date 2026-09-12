/*
 * Host test of the firmware's own reporting of stack and heap exhaustion, in
 * software/system/assert.c.
 *
 * A task that runs past the end of its stack corrupts whatever lies next to it, and
 * the firmware then stops somewhere else, later, with nothing to say why. The kernel
 * detects it at the next context switch only when FreeRTOSConfig.h asks for the
 * check, and it can only report it through vApplicationStackOverflowHook(). The same
 * applies to a heap that runs low: nothing shows it unless something prints it.
 */
#define _DEFAULT_SOURCE
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
int rtos_resource_report(char *buf, int size);

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* ---- the kernel calls assert.c makes, answered from a fixed picture ---- */

static const TaskStatus_t fake_tasks[] = {
    { (TaskHandle_t)1, "IEC Server", 7, eBlocked, 1, 1, 0, NULL, 700 },
    { (TaskHandle_t)2, "GUI Task",   3, eReady,   0, 0, 0, NULL, 1200 },
    { (TaskHandle_t)3, "Tmr Svc",    2, eBlocked, 3, 3, 0, NULL, 35 },
};
#define FAKE_TASKS (sizeof(fake_tasks) / sizeof(fake_tasks[0]))

static int allocations = 0;

void *pvPortMalloc(size_t size) { allocations++; return malloc(size); }
void vPortFree(void *pv) { if (pv) { allocations--; } free(pv); }
size_t xPortGetFreeHeapSize(void) { return 1583280; }
size_t xPortGetMinimumEverFreeHeapSize(void) { return 1502864; }
UBaseType_t uxTaskGetNumberOfTasks(void) { return FAKE_TASKS; }
void vTaskList(char *pcWriteBuffer) { pcWriteBuffer[0] = 0; }

UBaseType_t uxTaskGetSystemState(TaskStatus_t *pxTaskStatusArray, UBaseType_t uxArraySize,
                                 uint32_t *pulTotalRunTime)
{
    UBaseType_t n = (uxArraySize < FAKE_TASKS) ? uxArraySize : FAKE_TASKS;
    memcpy(pxTaskStatusArray, fake_tasks, n * sizeof(TaskStatus_t));
    if (pulTotalRunTime) {
        *pulTotalRunTime = 0;
    }
    return n;
}

/* ---- the configuration ---- */

static void test_configuration(void)
{
    // Method 2 also compares the last bytes of the stack with the fill pattern, so it
    // catches an overrun that has already moved the stack pointer back.
    check(configCHECK_FOR_STACK_OVERFLOW == 2, "configCHECK_FOR_STACK_OVERFLOW is 2");
    check(INCLUDE_uxTaskGetStackHighWaterMark == 1, "INCLUDE_uxTaskGetStackHighWaterMark is 1");
}

/* ---- the hook ---- */

// The hook must name the task and must not return, because the stack it would
// return onto is the one that was overrun. It runs in a child process so that a
// hook that halts can be stopped from outside.
static void test_stack_overflow_hook(void)
{
    int fds[2];
    if (pipe(fds) != 0) {
        check(0, "pipe for the hook's output");
        return;
    }
    fflush(stdout); // or the child inherits, and prints again, what the parent buffered
    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        setvbuf(stdout, NULL, _IONBF, 0);
        char name[] = "IEC Server";
        vApplicationStackOverflowHook((TaskHandle_t)1, name);
        printf("HOOK RETURNED\n");
        _exit(3);
    }
    close(fds[1]);

    char out[512];
    int got = 0;
    struct pollfd p = { fds[0], POLLIN, 0 };
    while ((got < (int)sizeof(out) - 1) && (poll(&p, 1, 2000) > 0)) {
        int n = read(fds[0], out + got, sizeof(out) - 1 - got);
        if (n <= 0) {
            break;
        }
        got += n;
        out[got] = 0;
        if (strstr(out, "IEC Server") && strchr(strstr(out, "IEC Server"), '\n')) {
            break;
        }
    }
    out[got] = 0;
    usleep(100 * 1000);
    int status = 0;
    pid_t done = waitpid(child, &status, WNOHANG);
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    close(fds[0]);

    printf("      hook printed: %s", got ? out : "(nothing)\n");
    check(strstr(out, "STACK OVERFLOW") != NULL, "the hook says it is a stack overflow");
    check(strstr(out, "IEC Server") != NULL, "the hook names the task");
    check((done == 0) && (strstr(out, "HOOK RETURNED") == NULL), "the hook does not return");
}

/* ---- the report on the System Information screen ---- */

static void test_resource_report(void)
{
    char buf[1024];
    int len = rtos_resource_report(buf, sizeof(buf));
    printf("      report:\n%s", buf);
    check(len == (int)strlen(buf), "the report returns its length");
    check(strstr(buf, "1583280") != NULL, "the report carries the free heap");
    check(strstr(buf, "1502864") != NULL, "the report carries the lowest free heap");
    check(strstr(buf, "8388608") != NULL, "the report carries the heap size");
    // The high water mark is in words; the report gives bytes.
    check(strstr(buf, "IEC Server") && strstr(strstr(buf, "IEC Server"), "2800"),
          "the report gives the IEC task's stack head room in bytes");
    check(strstr(buf, "GUI Task") && strstr(strstr(buf, "GUI Task"), "4800"),
          "the report lists every task");
    check(strstr(buf, "Tmr Svc") && strstr(strstr(buf, "Tmr Svc"), "140"),
          "the report lists the last task");
    check(allocations == 0, "the report frees what it allocated");

    // A buffer too small for the report is filled and terminated, never overrun.
    char small[48];
    memset(small, 'X', sizeof(small));
    len = rtos_resource_report(small, 40);
    check((small[39] == 0) && (len == (int)strlen(small)) && (len < 40) &&
          (memcmp(small + 40, "XXXXXXXX", 8) == 0),
          "a short buffer is cut and terminated inside its size");
    check(allocations == 0, "a cut report frees what it allocated");
}

int main(void)
{
    test_configuration();
    test_stack_overflow_hook();
    test_resource_report();
    if (failures) {
        printf("\n%d check(s) failed.\n", failures);
        return 1;
    }
    printf("\nAll RTOS diagnostics checks passed.\n");
    return 0;
}
