/*
 * Host stand-ins for the firmware headers that network/socket_dma.cc includes.
 * Sockets are the host's POSIX sockets; FreeRTOS, the C64 and the file manager
 * are reduced to what socket_dma.cc touches, and every C64 command is recorded
 * instead of executed (see socket_dma_test.cc).
 */
#ifndef SOCKET_DMA_STUBS_H
#define SOCKET_DMA_STUBS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// FreeRTOS
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
#define portTICK_PERIOD_MS 5    // configTICK_RATE_HZ 200, as on the firmware
#define configMINIMAL_STACK_SIZE 1024
#define PRIO_NETSERVICE 1
int xTaskCreate(TaskFunction_t fn, const char *name, int stack, void *arg, int prio, void *handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

// lwIP names
#define closesocket(s) close(s)
int sdma_test_bind(int s, const struct sockaddr *a, socklen_t l);
#define bind(s, a, l) sdma_test_bind(s, a, l)
// socket_dma.cc passes an unsigned long length, as lwIP's socklen_t is on the Nios.
static inline int accept(int s, struct sockaddr *a, unsigned long *l)
{
    socklen_t t = (socklen_t)*l;
    int r = accept(s, a, &t);
    *l = t;
    return r;
}

// Configuration
class ConfigItem;
template <class T> class IndexedList { };
class ConfigStore {
public:
    void addObject(void *) { }
    int get_value(uint8_t id);
    const char *get_string(uint8_t id);
};
class ConfigurableObject {
public:
    ConfigStore *cfg;
    virtual ~ConfigurableObject() { }
    virtual void effectuate_settings(void) { }
};

// Subsystem commands: recorded, not executed
#define SUBSYSID_C64      1
#define SUBSYSID_DRIVE_A  2
#define C64_DMA_BUFFER    10
#define C64_DMA_RAW_WRITE 11
#define C64_READ_FLASH    12
#define C64_DRIVE_LOAD    13
#define MENU_C64_RESET    14
#define MENU_C64_POWEROFF 15
#define MENU_1541_MOUNT_D64 16
#define RUNCODE_DMALOAD       0x20
#define RUNCODE_DMALOAD_RUN   0x21
#define RUNCODE_DMALOAD_JUMP  0x22
#define RUNCODE_MOUNT_LOAD_RUN 0x23
#define FLASH_CMD_PAGESIZE 0
#define FLASH_CMD_NOPAGES  1
#define FLASH_CMD_GETPAGE  2
#define REU_MEMORY_BASE 0

class SubsysCommand {
public:
    int function_id;
    int mode;
    void *buffer;
    int bufferSize;
    void *direct_call;
    SubsysCommand(void *ui, int id, int func, int mode, const void *buf, int len);
    SubsysCommand(void *ui, int id, int func, int mode, const char *p, const char *f);
    int execute(void);
};

class C64 {
public:
    static C64 *getMachine(void);
    void enable_kernal(uint8_t *) { }
};
struct cart_def { uint8_t *custom_addr; uint32_t length; };

class mstring {
public:
    const char *c_str(void) { return ""; }
};
class Path {
public:
    Path(const char *) { }
    const char *getHead(mstring &) { return ""; }
    const char *getLastElement(void) { return ""; }
};
typedef enum { FR_OK = 0, FR_DENIED = 7 } FRESULT;
class FileManager {
public:
    static FileManager *getFileManager(void);
    FRESULT save_temp_file(const char *dir, const char *name, uint8_t *buf, uint32_t len, mstring *path);
};
class FileTypeCRT {
public:
    static int execute_st(SubsysCommand *) { return 0; }
};
class C1541 {
public:
    int get_current_iec_address(void) { return 8; }
};
extern C1541 *c1541_A;

void getProductTitleString(char *buf, int size, bool ascii);

// UDP ident (compiled, not exercised)
#define lwip_close(s) close(s)
#define APPL_VERSION_ASCII "test"
void getProductVersionString(char *buf, int size, bool ascii);
int getFpgaVersion(void);
const char *getProductUniqueId(void);
class JSON {
public:
    static JSON *Obj(void) { return new JSON(); }
    JSON *add(const char *, const char *) { return this; }
    JSON *add(const char *, bool) { return this; }
    const char *render(void) { return "{}"; }
};
typedef JSON JSON_Object;

// Startup registration: the test constructs SocketDMA itself.
class InitFunction {
public:
    InitFunction(const char *, void (*)(void *, void *), void *, void *, int) { }
};

#endif
