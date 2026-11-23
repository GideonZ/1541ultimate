/*
 * Host stand-ins for the firmware headers that network/syslog.cc includes.
 * FreeRTOS tasks are host threads, the safe section is a recursive mutex, sockets
 * are the host's POSIX sockets. The config store keeps the one setting syslog.cc
 * reads and calls the registered objects when the test "closes" the config.
 */
#ifndef SYSLOG_STUBS_H
#define SYSLOG_STUBS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// FreeRTOS
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
#define portTICK_PERIOD_MS 5    // configTICK_RATE_HZ 200, as on the firmware
#define configMINIMAL_STACK_SIZE 1024
#define PRIO_NETSERVICE 1
#define pdPASS 1
int xTaskCreate(TaskFunction_t fn, const char *name, int stack, void *arg, int prio, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t t);
void vTaskSuspend(TaskHandle_t t);
void syslog_test_enter(void);
void syslog_test_leave(void);
#define ENTER_SAFE_SECTION syslog_test_enter()
#define LEAVE_SAFE_SECTION syslog_test_leave()

// lwIP names. Sockets are counted so the test can see leaks.
extern int syslog_test_sockets_opened;
extern int syslog_test_sockets_open;
int syslog_test_socket(int d, int t, int p);
int syslog_test_close(int s);
#define socket(d, t, p) syslog_test_socket(d, t, p)
#define closesocket(s) syslog_test_close(s)

typedef struct { uint32_t addr; } ip_addr_t;
static inline int ipaddr_aton(const char *s, ip_addr_t *ip)
{
    struct in_addr a;
    if (!inet_aton(s, &a)) {
        return 0;
    }
    ip->addr = a.s_addr;
    return 1;
}

// Configuration
class ConfigurableObject;
class ConfigStore {
public:
    void addObject(ConfigurableObject *o);
    void effectuate(void);
    const char *get_string(uint8_t id);
    void set_string(uint8_t id, const char *s);
};
class ConfigurableObject {
public:
    ConfigStore *cfg;
    ConfigurableObject() { cfg = 0; }
    virtual ~ConfigurableObject() { }
    virtual void effectuate_settings(void) { }
};
class ConfigItem;
template <class T> class IndexedList { };

class NetworkInterface {
public:
    static NetworkInterface *DoWeHaveLink(void);
};

#endif
