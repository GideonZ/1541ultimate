/*
 * Host stand-ins for the firmware headers that network/ftpd.cc includes.
 * Sockets are the host's POSIX sockets; FreeRTOS, configuration and the
 * real-time clock are reduced to what ftpd.cc touches. The file system (vfs.h,
 * the real header) is implemented by the test.
 */
#ifndef FTPD_STUBS_H
#define FTPD_STUBS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// lwIP names
#define closesocket(s) close(s)
#define ioctlsocket(s, cmd, argp) ioctl(s, cmd, argp)
typedef struct { uint32_t addr; } ip_addr_t;
#define IP4_ADDR(ip, a, b, c, d) ((ip)->addr = htonl(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d)))
#define sin_len sin_zero[0]   // lwIP's sockaddr_in has a length byte; Linux's has not

// FreeRTOS
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define portTICK_PERIOD_MS 5
#define configMINIMAL_STACK_SIZE 1024
#define PRIO_NETSERVICE 1
#define pdPASS 1
typedef long BaseType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms) / portTICK_PERIOD_MS)
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
int xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t t);
TickType_t xTaskGetTickCount(void);

// Configuration
class ConfigItem;
class ConfigStore {
public:
    void addObject(void *) { }
    int get_value(uint8_t id) { return 0; }
    const char *get_string(uint8_t id) { return ""; }
};
class ConfigurableObject {
public:
    ConfigStore *cfg;
    virtual ~ConfigurableObject() { }
    virtual void effectuate_settings(void) { }
};

class Rtc {
public:
    void get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s) { y = 2026; M = 1; D = 1; wd = 0; h = m = s = 0; }
    uint32_t get_fat_time(void) { return 0; }
};
extern Rtc rtc;

class InitFunction {
public:
    InitFunction(const char *, void (*)(void *, void *), void *, void *, int) { }
};

#include <stdarg.h>
int _my_vnprintf(void (*putc)(char c, void **param), void **param, size_t maxlen, const char *fmt, va_list ap);

void getProductTitleString(char *buf, int size, bool ascii);
const char *getProductString(void);

#endif
