/*
 * Stand-ins for what iec_printer.cc uses of the rest of the firmware, so that the printer's own
 * source runs on the host: the menu, the configuration, the IEC interface, the files and the
 * page emulation record what they are asked to do, and the test reads it back.
 */
#ifndef HOST_PRINTER_STUBS_H
#define HOST_PRINTER_STUBS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <atomic>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* The spool's big ring goes through these, so a test can tell what is still allocated (and, under ASan, who owns it). */
extern std::atomic<int> host_ring_live;
static inline void *host_ring_malloc(size_t n) { host_ring_live++; return malloc(n); }
static inline void host_ring_free(void *p) { host_ring_live--; free(p); }
#define PRINTER_SPOOL_MALLOC host_ring_malloc
#define PRINTER_SPOOL_FREE   host_ring_free

#define FS_ROOT "/"
#define SORT_ORDER_PRINTER 1
#define SORT_ORDER_CFG_PRINTER 2
#define SUBSYSID_PRINTER 7
#define CFG_TYPE_ENUM 0
#define CFG_TYPE_VALUE 1
#define CFG_TYPE_STRING 2
#define CFG_TYPE_END 3
#define SLAVE_CMD_ATN 0x100
#define SLAVE_CMD_EOI 0x101

enum SubsysResultCode_e { SSRET_OK = 0 };
typedef int FRESULT;
#define FA_WRITE 1
#define FA_OPEN_ALWAYS 2
#define FA_CREATE_NEW 4

struct JSON_Object;
struct StreamTextLog { };
struct Path;

struct t_cfg_definition {
    uint8_t id;
    int type;
    const char *item_text;
    const char *item_format;
    const char **item_strings;
    int min, max, def;
};
static const char *en_dis[] = { "Disabled", "Enabled" };

/* what the test sees */
struct Record {
    std::atomic<int> configure, readdress, form_feed, mps_reset, file_opens, file_closes, progress_shown, progress_hidden;
    std::atomic<int> file_is_open;
    std::atomic<int> interpreted_bytes;
    Record() { reset(); }
    void reset()
    {
        configure = readdress = form_feed = mps_reset = file_opens = file_closes = 0;
        progress_shown = progress_hidden = file_is_open = interpreted_bytes = 0;
    }
};
extern Record rec;

struct ConfigStore {
    std::map<int, int> values;
    std::map<int, std::string> strings;
    int get_value(uint8_t id) { return values[id]; }
    const char *get_string(uint8_t id) { return strings[id].c_str(); }
    void set_value(uint8_t id, int v) { values[id] = v; }
    void set_sort_order(int) { }
};
extern ConfigStore *host_cfg;

class ConfigurableObject {
public:
    ConfigStore *cfg;
    ConfigurableObject() : cfg(host_cfg) { }
    virtual ~ConfigurableObject() { }
    virtual void effectuate_settings(void) { }
    void register_store(uint32_t, const char *, t_cfg_definition *) { }
    void effectuate_registered_settings(void) { effectuate_settings(); }
};

class SubSystem {
public:
    SubSystem(int) { }
    virtual ~SubSystem() { }
};

class ObjectWithMenu {
public:
    virtual ~ObjectWithMenu() { }
};

struct Action {
    Action(const char *, int, int) { }
    void show() { } void hide() { } void enable() { } void disable() { }
};
struct TaskCategory {
    void append(Action *) { }
};
struct TasksCollection {
    static TaskCategory *getCategory(const char *, int) { static TaskCategory c; return &c; }
};

struct UserInterface {
    void show_progress(const char *, int) { rec.progress_shown++; }
    void hide_progress() { rec.progress_hidden++; }
    void update_progress(const char *, int) { }
};
struct SubsysCommand {
    UserInterface *user_interface;
    int functionID;
};

class IecSlave {
public:
    virtual ~IecSlave() { }
    virtual void reset(void) { }
};
enum t_channel_retval { IEC_OK = 0, IEC_BYTE_LOST = -6 };

class IecInterface {
public:
    void configure(void) { rec.configure++; }
    void readdress(int) { rec.readdress++; }
    int register_slave(IecSlave *) { return 3; }
    static IecInterface *get_iec_interface(void) { static IecInterface i; return &i; }
};
extern IecInterface *iec_if;

struct File {
    uint32_t size;
    File() : size(0) { rec.file_is_open = 1; }
    uint32_t get_size() { return size; }
    int seek(uint32_t) { return 0; }
    int write(const void *, uint32_t n, uint32_t *w) { *w = n; return 0; }
};
struct FileSystem {
    static const char *get_error_string(FRESULT) { return "error"; }
};
struct FileManager {
    static FileManager *getFileManager(void) { static FileManager f; return &f; }
    FRESULT fopen(const char *, const char *, int, File **f)
    {
        rec.file_opens++;
        *f = new File;
        return 0;
    }
    FRESULT fopen(const char *, int, File **f)
    {
        rec.file_opens++;
        *f = new File;
        return 0;
    }
    FRESULT fclose(File *f)
    {
        rec.file_closes++;
        rec.file_is_open = 0;
        delete f;
        return 0;
    }
};

#define MPS_PRINTER_INTERPRETER_CBM 0
#define MPS_PRINTER_INTERPRETER_EPSONFX80 1
#define MPS_PRINTER_INTERPRETER_IBMGP 2
#define MPS_PRINTER_INTERPRETER_IBMPP 3
struct MpsPrinter {
    static MpsPrinter *getMpsPrinter(void) { static MpsPrinter m; return &m; }
    void Reset() { rec.mps_reset++; }
    void FormFeed() { rec.form_feed++; }
    void Interpreter(uint8_t *, int n) { rec.interpreted_bytes += n; }
    void setCharsetVariant(int) { }
    void setColorMode(bool) { }
    void setFilename(char *) { }
    void setTopMargin(int) { }
    void setPrintableHeight(int) { }
    void setInterpreter(int) { }
    void setDotSize(int) { }
    void setCBMCharset(int) { }
    void setEpsonCharset(int) { }
    void setIBMCharset(int) { }
};

struct InitFunction {
    InitFunction(const char *, void (*)(void *, void *), void *, void *, int) { }
};

/* The task the firmware creates: here a thread, unless the test keeps the printer task away. */
extern bool host_start_tasks;
extern TaskHandle_t host_dummy_handle;
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, int, void *arg, int, TaskHandle_t *h);
static inline void vTaskDelete(TaskHandle_t) { }

#endif
