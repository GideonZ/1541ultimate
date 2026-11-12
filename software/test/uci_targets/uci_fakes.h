// Fakes for the host harness of the UCI DOS target (filemanager/dos.cc), the
// UCI control target (io/command_interface/control_target.cc) and the REU
// preloader (io/c64/reu_preloader.cc), which are compiled UNMODIFIED.
//
// Everything a test controls or observes lives in namespace uci_fake. Call
// uci_fake::reset() at the start of each test case.
//
//   Setup (once, at program start)
//     uci_fake::init()                  map the REU window at 0x01000000..0x01FFFFFF
//                                       (aborts loudly if that fails), run the
//                                       InitFunction hooks (creates reu_preloader)
//   Subsystem commands (SubsysCommand::execute is faked; the class is the real one)
//     uci_fake::next_subsys_result      result returned by every execute() (default SSRET_OK)
//     uci_fake::subsys_calls            one SubsysCall per execute(): subsysID, functionID,
//                                       mode, path, filename (and the Action name)
//   File system (in memory; "/Usb0/a.prg" style absolute paths)
//     uci_fake::fs_add_file(path, bytes)   create/replace a file (parents are created)
//     uci_fake::fs_add_dir(path)           create a directory
//     uci_fake::fs_file(path)              the bytes of a file (NULL if absent)
//     uci_fake::io_log                     one IoEvent per File::read / File::write call:
//                                          is_write, requested length, transferred, path
//     uci_fake::on_io                      std::function invoked after EVERY read/write
//                                          call (set a flag, poke a register, ...)
//     uci_fake::open_file_count()          File objects currently open (leak detection)
//     uci_fake::max_io_len(is_write)       largest requested length seen so far
//   REU memory
//     uci_fake::reu()                      uint8_t* at 0x01000000 (the same bytes dos.cc and
//                                          reu_preloader.cc address as REU_MEMORY_BASE)
//   UCI registers
//     CMD_IF_STATUSBYTE, CMD_IF_*          the real macros from io/command_interface/command_intf.h
//                                          resolve to uci_fake_cmdif_regs[], so
//                                          CMD_IF_STATUSBYTE |= CMD_ABORT_DATA sets the abort bit
//     uci_fake::c64_reset_pending          variable for a reset-pending getter. The getter does not
//                                          exist at base; the fix adds it to command_intf.h and a
//                                          one-line definition returning this variable to uci_fakes.cc
//   Drives
//     uci_fake::install_drive(0/1, iec, powered)   c1541_A / c1541_B present (or removed with
//                                                  uci_fake::remove_drive); last_mounted_drive too
//   C64 / config
//     uci_fake::mp3_ramdrive_type[4], uci_fake::mp3_native_size[4]
//     uci_fake::fpga_capabilities          returned by getFpgaCapabilities()
//     uci_fake::cfg                        ConfigStore behind C64::getMachine()->cfg:
//                                          cfg.set(CFG_C64_REU_EN, 1), cfg.set_string(CFG_C64_REU_IMG, "/Usb0/x.reu")
//
// Include <string>/<vector>/... BEFORE this header: integer.h defines a max() macro.
#ifndef UCI_FAKES_H
#define UCI_FAKES_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>

#include "rtos.h"
#include "integer.h"
#include "mystring.h"
#include "indexed_list.h"
#include "itu.h"          // real: CAPAB_* and getFpgaCapabilities()
#include "return_codes.h" // real
#include "file_info.h"    // real FileInfo

// ---- constants the production files take from c64.h / c1541.h / tape_recorder.h -------------
#define REU_MEMORY_BASE     0x1000000
#define MENU_C64_REBOOT     0x6402
#define C64_PUSH_BUTTON     0x6476
#define DRVTYPE_MP3_DNP     31
#define CFG_C64_REU_EN      0xC3
#define CFG_C64_REU_SIZE    0xC4
#define CFG_C64_REU_PRE     0x80
#define CFG_C64_REU_IMG     0x81
#define CFG_C64_REU_OFFS    0x82
#define MENU_REC_FINISH     0x3213
#define MENU_1541_REMOVE    0x1502
#define MENU_1541_SWAP      0x1514
#define MENU_1541_MOUNT_D64 0x1520
#define MENU_1541_MOUNT_G64 0x1530
typedef enum { e_dt_1541 = 0, e_dt_1571 = 1, e_dt_1581 = 2, e_dt_unset = 3 } t_drive_type;

// ---- file system ------------------------------------------------------------------------------
typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH, FR_INVALID_NAME,
    FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED,
    FR_NO_FILESYSTEM, FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NO_MEMORY, FR_DISK_FULL,
    FR_DIR_NOT_EMPTY, FR_LOOP_DETECTED
} FRESULT;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define INFO_SIZE        128

class FileSystem {
public:
    static const char *get_error_string(FRESULT res);
};

class UserInterface { };

class Path {
    std::string full;
public:
    const char *owner;
    Path() : full("/"), owner(NULL) { }
    int cd(const char *p);
    const char *get_path(void) { return full.c_str(); }
    const char *getElement(int index);
    std::string element_buf;
};

class File {
    std::string path;
    std::shared_ptr<std::vector<uint8_t> > data;
    uint32_t pos;
    uint8_t flags;
public:
    File(const char *p, std::shared_ptr<std::vector<uint8_t> > d, uint8_t f) : path(p), data(d), pos(0), flags(f) { }
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t position);
    uint32_t get_size(void) { return (uint32_t)data->size(); }
    uint32_t get_position(void) { return pos; }
    const char *get_path(void) { return path.c_str(); }
    FRESULT sync(void) { return FR_OK; }
};

typedef enum { eRefreshDirectory, eNodeAdded, eNodeRemoved, eNodeMediaRemoved, eNodeUpdated, eChangeDirectory } eFileManagerEventType;

class FileManagerEvent {
public:
    eFileManagerEventType eventType;
    mstring pathName;
    mstring newName;
};

#include "observer.h"  // real: ObserverQueue (no-op without OS)

class FileManager {
public:
    static FileManager *getFileManager(void);
    Path *get_new_path(const char *owner) { Path *p = new Path(); p->owner = owner; return p; }
    void release_path(Path *p) { delete p; }
    bool is_path_valid(const char *p, FileInfo *inf = NULL);
    void registerObserver(ObserverQueue *) { }
    void deregisterObserver(ObserverQueue *) { }

    FRESULT fstat(Path *path, const char *filename, FileInfo &info);
    FRESULT fstat(const char *path, const char *name, FileInfo &info);
    FRESULT fstat(const char *pathname, FileInfo &info);
    FRESULT fopen(Path *path, const char *filename, uint8_t flags, File **);
    FRESULT fopen(const char *path, const char *filename, uint8_t flags, File **);
    FRESULT fopen(const char *pathname, uint8_t flags, File **);
    void    fclose(File *f);
    FRESULT fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool overwrite);
    FRESULT rename(Path *path, const char *old_name, const char *new_name);
    FRESULT rename(const char *old_name, const char *new_name);
    FRESULT delete_file(Path *path, const char *name);
    FRESULT delete_file(const char *pathname);
    FRESULT create_dir(Path *path, const char *name);
    FRESULT create_dir(const char *pathname);
    FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target, const char *matchPattern);
    FRESULT load_file(const char *path, const char *filename, uint8_t *mem, uint32_t maxlen, uint32_t *transferred);
    FRESULT save_file(bool overwrite, const char *path, const char *filename, const uint8_t *mem, uint32_t len, uint32_t *transferred);
    FRESULT save_temp_file(const char *category, const char *suggested_name, const uint8_t *data, uint32_t len, mstring *canonical_path_out);
};

// ---- configuration ----------------------------------------------------------------------------
class ConfigStore {
    std::map<int, int> values;
    std::map<int, std::string> strings;
public:
    int get_value(int id) { return values.count(id) ? values[id] : 0; }
    const char *get_string(int id) { if (!strings.count(id)) strings[id] = ""; return strings[id].c_str(); }
    void set(int id, int v) { values[id] = v; }
    void set_string(int id, const char *s) { strings[id] = s; }
    void clear(void) { values.clear(); strings.clear(); }
    bool need_effectuate(void) { return false; }
    void effectuate(void) { }
    void set_effectuated(void) { }
    const char *get_store_name(void) { return "fake"; }
};

class ConfigManager {
    IndexedList<ConfigStore *> stores;
public:
    ConfigManager() : stores(4, NULL) { }
    static ConfigManager *getConfigManager(void) { static ConfigManager cm; return &cm; }
    IndexedList<ConfigStore *> *getStores(void) { return &stores; }
};

class StreamTextLog {
public:
    StreamTextLog(int size) { }
    int getLength(void) { return 0; }
    char *getText(void) { return (char *)""; }
};

class ConfigIO {
public:
    static bool S_read_from_file(File *, StreamTextLog *) { return true; }
};

// ---- machine and drives -----------------------------------------------------------------------
class C64 {
public:
    ConfigStore *cfg;
    static C64 *getMachine(void);
    static int isMP3RamDrive(int dev);
    static int getSizeOfMP3NativeRamdrive(int dev);
    static uint8_t *get_cartridge_rom_addr(void);
private:
    // Private, as in io/c64/c64.h; only the friends below may call it.
    void get_all_memory(uint8_t *pb);   // runs uci_fake_all_memory_hook, if set
    friend class ControlTarget;         // SAVEMEM saves through get_all_memory
};

extern void (*uci_fake_all_memory_hook)(uint8_t *);

class C1541 {
public:
    int id, iec_address;
    bool power;
    t_drive_type type;
    static C1541 *get_last_mounted_drive(void);
    int  getID(void) { return id; }
    int  get_current_iec_address(void) { return iec_address; }
    int  get_effective_iec_address(void) { return iec_address; }
    void drive_power(bool on) { power = on; }
    bool get_drive_power(void) { return power; }
    t_drive_type get_drive_type(void) { return type; }
};
extern C1541 *c1541_A;
extern C1541 *c1541_B;

struct _message;          // Message is the real struct from command_target.h
class IecInterface {
public:
    static void info(struct _message &msg, int &offs) { }
};

class GcrImage {
public:
    static int convert_gcr_track_to_bin(uint8_t *, int, int, int, uint8_t *, uint8_t *, int) { return 0; }
};

class HomeDirectory {
public:
    static const char *getHomeDirectory(void) { return "/Usb0"; }
};

class Rtc {
public:
    int  get_correction(void) { return 0; }
    void get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s) { y = 40; M = 1; D = 1; wd = 0; h = m = s = 0; }
    void set_time(int, int, int, int, int, int, int) { }
    void set_time_in_chip(int, int, int, int, int, int, int, int) { }
};
extern Rtc rtc;

const char *getProductString();

// InitFunction hooks are recorded and run by uci_fake::init().
typedef void (*initFunction_t)(void *object, void *param);
class InitFunction {
public:
    InitFunction(const char *name, initFunction_t func, void *obj, void *prm, int ord = 0);
};

// ---- test-side control and observation --------------------------------------------------------
namespace uci_fake {

struct SubsysCall {
    int subsysID, functionID, mode;
    std::string path, filename, actionName;
};
struct IoEvent {
    bool is_write;
    uint32_t requested, transferred;
    std::string path;
};

extern SubsysResultCode_e next_subsys_result;
extern std::vector<SubsysCall> subsys_calls;
extern std::vector<IoEvent> io_log;
extern int yields;   // SLICED_TRANSFER_YIELD() calls (taskYIELD on the device)
extern std::function<void(const IoEvent &)> on_io;
extern bool c64_reset_pending;
extern int mp3_ramdrive_type[4];
extern int mp3_native_size[4];
extern uint32_t fpga_capabilities;
extern ConfigStore cfg;

void init(void);    // once, at program start
void reset(void);   // between test cases

void fs_add_file(const std::string &path, const std::vector<uint8_t> &bytes);
void fs_add_dir(const std::string &path);
const std::vector<uint8_t> *fs_file(const std::string &path);
int  open_file_count(void);
uint32_t max_io_len(bool is_write);
uint8_t *reu(void);

void install_drive(int which, int iec, bool powered);
void remove_drive(int which);

} // namespace uci_fake

#endif
