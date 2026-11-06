// Implementation of the fakes declared in uci_fakes.h. See the header for the
// test-control API.
#ifndef __linux__
#error "the UCI target harness maps the REU window with MAP_FIXED_NOREPLACE: Linux only"
#endif
#include "uci_fakes.h"   // pulls in the std headers first
#include <stdlib.h>
#include <sys/mman.h>
#include "subsys.h"      // real: SubsysCommand, SubsysResultCode_t, error_string()
#include "command_target.h"
#include "pattern.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// ---- command interface registers and message constants ---------------------------------------
volatile uint8_t uci_fake_cmdif_regs[0x1000];

CommandTarget *command_targets[CMD_IF_MAX_TARGET + 1];
Message c_message_no_target      = {  9, true, (uint8_t *)"NO TARGET" };
Message c_status_ok              = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty          = {  0, true, (uint8_t *)"" };

extern "C" uint32_t getFpgaCapabilities() { return uci_fake::fpga_capabilities; }
const char *getProductString() { return "Host Harness"; }
Rtc rtc;

// production: io/command_interface/command_intf.cc
namespace uci_fake { extern bool c64_reset_pending; }
bool c64_reset_is_pending(void) { return uci_fake::c64_reset_pending; }

void uci_fake_run_init_functions(void);

// ---- state ------------------------------------------------------------------------------------
namespace uci_fake {

SubsysResultCode_e next_subsys_result = SSRET_OK;
std::vector<SubsysCall> subsys_calls;
std::vector<IoEvent> io_log;
std::function<void(const IoEvent &)> on_io;
bool c64_reset_pending = false;
int mp3_ramdrive_type[4];
int mp3_native_size[4];
uint32_t fpga_capabilities = CAPAB_MM_DRIVE | CAPAB_COMMAND_INTF;
ConfigStore cfg;

typedef std::shared_ptr<std::vector<uint8_t> > Data;
static std::map<std::string, Data> files;
static std::set<std::string> dirs;
static std::set<File *> open_files;
static C1541 drives[2];
static C1541 *last_mounted = NULL;

static std::string normalize(const std::string &p)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string e = p.substr(i, j - i);
        if (e == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!e.empty() && e != ".") parts.push_back(e);
        i = j + 1;
    }
    std::string r;
    for (size_t k = 0; k < parts.size(); k++) r += "/" + parts[k];
    return r.empty() ? "/" : r;
}

static std::string join(const std::string &dir, const std::string &name)
{
    if (!name.empty() && name[0] == '/') return normalize(name);
    return normalize(dir + "/" + name);
}

static std::string parent_of(const std::string &p)
{
    size_t i = p.rfind('/');
    return (i == 0 || i == std::string::npos) ? "/" : p.substr(0, i);
}

static std::string base_of(const std::string &p)
{
    return p.substr(p.rfind('/') + 1);
}

void fs_add_dir(const std::string &path)
{
    std::string p = normalize(path);
    while (p != "/") {
        dirs.insert(p);
        p = parent_of(p);
    }
}

void fs_add_file(const std::string &path, const std::vector<uint8_t> &bytes)
{
    std::string p = normalize(path);
    fs_add_dir(parent_of(p));
    files[p] = Data(new std::vector<uint8_t>(bytes));
}

const std::vector<uint8_t> *fs_file(const std::string &path)
{
    std::map<std::string, Data>::iterator it = files.find(normalize(path));
    return (it == files.end()) ? NULL : it->second.get();
}

int open_file_count(void) { return (int)open_files.size(); }

uint32_t max_io_len(bool is_write)
{
    uint32_t m = 0;
    for (size_t i = 0; i < io_log.size(); i++)
        if (io_log[i].is_write == is_write && io_log[i].requested > m) m = io_log[i].requested;
    return m;
}

uint8_t *reu(void) { return (uint8_t *)(uintptr_t)0x01000000; }

void install_drive(int which, int iec, bool powered)
{
    C1541 *d = &drives[which ? 1 : 0];
    d->id = which ? SUBSYSID_DRIVE_B : SUBSYSID_DRIVE_A;
    d->iec_address = iec;
    d->power = powered;
    d->type = e_dt_1541;
    if (which) c1541_B = d; else c1541_A = d;
    last_mounted = d;
}

void remove_drive(int which)
{
    if (which) c1541_B = NULL; else c1541_A = NULL;
    last_mounted = NULL;
}

void init(void)
{
    void *p = mmap((void *)(uintptr_t)0x01000000, 0x01000000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)(uintptr_t)0x01000000) {
        perror("uci_fake::init: cannot map the REU window at 0x01000000");
        abort();
    }
    ::uci_fake_run_init_functions();
    reset();
}

void reset(void)
{
    next_subsys_result = SSRET_OK;
    subsys_calls.clear();
    io_log.clear();
    on_io = NULL;
    c64_reset_pending = false;
    memset(mp3_ramdrive_type, 0, sizeof(mp3_ramdrive_type));
    memset(mp3_native_size, 0, sizeof(mp3_native_size));
    fpga_capabilities = CAPAB_MM_DRIVE | CAPAB_COMMAND_INTF;
    cfg.clear();
    memset((void *)uci_fake_cmdif_regs, 0, sizeof(uci_fake_cmdif_regs));
    memset(reu(), 0, 0x01000000);
    // Files already open stay open (and stay counted) so leaks survive reset();
    // their data is shared and therefore remains valid.
    files.clear();
    dirs.clear();
    c1541_A = c1541_B = NULL;
    last_mounted = NULL;
}

} // namespace uci_fake

using namespace uci_fake;

// ---- init functions ---------------------------------------------------------------------------
struct InitEntry { initFunction_t fn; void *obj; void *prm; int ord; };
static std::vector<InitEntry> &init_entries() { static std::vector<InitEntry> v; return v; }

InitFunction::InitFunction(const char *, initFunction_t func, void *obj, void *prm, int ord)
{
    InitEntry e = { func, obj, prm, ord };
    init_entries().push_back(e);
}

void uci_fake_run_init_functions(void)
{
    std::vector<InitEntry> &v = init_entries();
    for (size_t i = 0; i < v.size(); i++) v[i].fn(v[i].obj, v[i].prm);
}

// ---- SubsysCommand: the class is real (infra/subsys.h); execute() is the fake ------------------
SubsysResultCode_t SubsysCommand::execute(void)
{
    SubsysCall c;
    c.subsysID = subsysID;
    c.functionID = functionID;
    c.mode = mode;
    c.path = path.c_str();
    c.filename = filename.c_str();
    c.actionName = actionName.c_str();
    subsys_calls.push_back(c);
    SubsysResultCode_t r = { next_subsys_result };
    delete this;
    return r;
}

// ---- machine ----------------------------------------------------------------------------------
C1541 *c1541_A = NULL;
C1541 *c1541_B = NULL;
C1541 *C1541::get_last_mounted_drive(void) { return uci_fake::last_mounted; }

C64 *C64::getMachine(void)
{
    static C64 machine;
    machine.cfg = &uci_fake::cfg;
    return &machine;
}
int C64::isMP3RamDrive(int dev) { return mp3_ramdrive_type[dev & 3]; }
int C64::getSizeOfMP3NativeRamdrive(int dev) { return mp3_native_size[dev & 3]; }
uint8_t *C64::get_cartridge_rom_addr(void) { static uint8_t rom[0x100000]; return rom; }

// ---- file system ------------------------------------------------------------------------------
const char *FileSystem::get_error_string(FRESULT res)
{
    switch (res) {
    case FR_OK: return "OK!";
    case FR_DISK_ERR: return "DISK ERROR";
    case FR_INT_ERR: return "INTERNAL ERROR";
    case FR_NO_FILE: return "FILE DOESN'T EXIST";
    case FR_NO_PATH: return "PATH DOESN'T EXIST";
    case FR_INVALID_NAME: return "INVALID NAME";
    case FR_DENIED: return "ACCESS DENIED";
    case FR_EXIST: return "FILE EXISTS";
    case FR_DISK_FULL: return "DISK IS FULL";
    default: return "UNKNOWN ERROR";
    }
}

int Path::cd(const char *p)
{
    full = join(full, p ? p : "");
    return 0;
}

const char *Path::getElement(int index)
{
    std::vector<std::string> parts;
    size_t i = 1;
    while (i < full.size()) {
        size_t j = full.find('/', i);
        if (j == std::string::npos) j = full.size();
        parts.push_back(full.substr(i, j - i));
        i = j + 1;
    }
    element_buf = (index >= 0 && index < (int)parts.size()) ? parts[index] : "";
    return element_buf.c_str();
}

static void log_io(File *f, bool is_write, uint32_t requested, uint32_t transferred)
{
    IoEvent e;
    e.is_write = is_write;
    e.requested = requested;
    e.transferred = transferred;
    e.path = f->get_path();
    io_log.push_back(e);
    if (on_io) on_io(e);
}

FRESULT File::read(void *buffer, uint32_t len, uint32_t *transferred)
{
    uint32_t n = 0;
    if (!(flags & FA_READ)) {
        if (transferred) *transferred = 0;
        return FR_DENIED;
    }
    if (pos < data->size()) {
        n = (uint32_t)data->size() - pos;
        if (n > len) n = len;
        memcpy(buffer, data->data() + pos, n);
        pos += n;
    }
    if (transferred) *transferred = n;
    log_io(this, false, len, n);
    return FR_OK;
}

FRESULT File::write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    if (!(flags & FA_WRITE)) {
        if (transferred) *transferred = 0;
        return FR_DENIED;
    }
    if (pos + len > data->size()) data->resize(pos + len);
    memcpy(data->data() + pos, buffer, len);
    pos += len;
    if (transferred) *transferred = len;
    log_io(this, true, len, len);
    return FR_OK;
}

FRESULT File::seek(uint32_t position)
{
    if (!(flags & FA_WRITE) && position > data->size()) position = (uint32_t)data->size();
    pos = position;
    return FR_OK;
}

FileManager *FileManager::getFileManager(void)
{
    static FileManager fm;
    return &fm;
}

bool FileManager::is_path_valid(const char *p, FileInfo *)
{
    std::string n = normalize(p);
    return n == "/" || dirs.count(n) || files.count(n);
}

static FRESULT stat_resolved(const std::string &p, FileInfo &info)
{
    std::map<std::string, Data>::iterator it = files.find(p);
    bool is_dir = (it == files.end());
    if (is_dir && p != "/" && !dirs.count(p)) return FR_NO_FILE;
    std::string name = base_of(p);
    info.size = is_dir ? 0 : (uint32_t)it->second->size();
    info.attrib = is_dir ? AM_DIR : 0;
    strncpy(info.lfname, name.c_str(), info.lfsize);
    info.lfname[info.lfsize - 1] = 0;
    memset(info.extension, 0, sizeof(info.extension));
    if (!is_dir) get_extension(name.c_str(), info.extension, true);
    return FR_OK;
}

FRESULT FileManager::fstat(Path *path, const char *filename, FileInfo &info)
{
    return stat_resolved(join(path->get_path(), filename), info);
}
FRESULT FileManager::fstat(const char *path, const char *name, FileInfo &info)
{
    return stat_resolved(join(path, name), info);
}
FRESULT FileManager::fstat(const char *pathname, FileInfo &info)
{
    return stat_resolved(normalize(pathname), info);
}

static FRESULT open_resolved(const std::string &p, uint8_t flags, File **out)
{
    *out = NULL;
    std::map<std::string, Data>::iterator it = files.find(p);
    bool writing_new = (flags & FA_WRITE) && (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS));
    if (writing_new) {
        std::string par = parent_of(p);
        if (par != "/" && !dirs.count(par)) return FR_NO_PATH;
        if (dirs.count(p)) return FR_DENIED;
        Data d(new std::vector<uint8_t>());
        files[p] = d;
        *out = new File(p.c_str(), d, flags);
    } else {
        if (it == files.end()) return FR_NO_FILE;
        *out = new File(p.c_str(), it->second, flags);
    }
    open_files.insert(*out);
    return FR_OK;
}

FRESULT FileManager::fopen(Path *path, const char *filename, uint8_t flags, File **f)
{
    return open_resolved(join(path->get_path(), filename), flags, f);
}
FRESULT FileManager::fopen(const char *path, const char *filename, uint8_t flags, File **f)
{
    return open_resolved(join(path, filename), flags, f);
}
FRESULT FileManager::fopen(const char *pathname, uint8_t flags, File **f)
{
    return open_resolved(normalize(pathname), flags, f);
}

void FileManager::fclose(File *f)
{
    if (open_files.erase(f)) delete f;
}

static FRESULT copy_resolved(const std::string &src, const std::string &dst, bool overwrite)
{
    std::map<std::string, Data>::iterator it = files.find(src);
    if (it == files.end()) return FR_NO_FILE;
    if (files.count(dst) && !overwrite) return FR_EXIST;
    files[dst] = Data(new std::vector<uint8_t>(*it->second));
    return FR_OK;
}

FRESULT FileManager::fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool overwrite)
{
    return copy_resolved(join(path, filename), join(dest, dest_filename), overwrite);
}

static FRESULT rename_resolved(const std::string &from, const std::string &to)
{
    std::map<std::string, Data>::iterator it = files.find(from);
    if (it == files.end()) return FR_NO_FILE;
    if (files.count(to)) return FR_EXIST;
    files[to] = it->second;
    files.erase(from);
    return FR_OK;
}

FRESULT FileManager::rename(Path *path, const char *old_name, const char *new_name)
{
    return rename_resolved(join(path->get_path(), old_name), join(path->get_path(), new_name));
}
FRESULT FileManager::rename(const char *old_name, const char *new_name)
{
    return rename_resolved(normalize(old_name), normalize(new_name));
}

static FRESULT delete_resolved(const std::string &p)
{
    if (files.erase(p)) return FR_OK;
    if (dirs.erase(p)) return FR_OK;
    return FR_NO_FILE;
}

FRESULT FileManager::delete_file(Path *path, const char *name) { return delete_resolved(join(path->get_path(), name)); }
FRESULT FileManager::delete_file(const char *pathname) { return delete_resolved(normalize(pathname)); }

FRESULT FileManager::create_dir(Path *path, const char *name) { fs_add_dir(join(path->get_path(), name)); return FR_OK; }
FRESULT FileManager::create_dir(const char *pathname) { fs_add_dir(pathname); return FR_OK; }

FRESULT FileManager::get_directory(Path *p, IndexedList<FileInfo *> &target, const char *)
{
    std::string dir = normalize(p->get_path());
    if (dir != "/" && !dirs.count(dir)) return FR_NO_PATH;
    for (std::set<std::string>::iterator it = dirs.begin(); it != dirs.end(); ++it) {
        if (parent_of(*it) == dir) {
            FileInfo *fi = new FileInfo(INFO_SIZE);
            stat_resolved(*it, *fi);
            target.append(fi);
        }
    }
    for (std::map<std::string, Data>::iterator it = files.begin(); it != files.end(); ++it) {
        if (parent_of(it->first) == dir) {
            FileInfo *fi = new FileInfo(INFO_SIZE);
            stat_resolved(it->first, *fi);
            target.append(fi);
        }
    }
    return FR_OK;
}

FRESULT FileManager::load_file(const char *path, const char *filename, uint8_t *mem, uint32_t maxlen, uint32_t *transferred)
{
    std::map<std::string, Data>::iterator it = files.find(join(path, filename));
    if (it == files.end()) return FR_NO_FILE;
    uint32_t n = (uint32_t)it->second->size();
    if (n > maxlen) n = maxlen;
    memcpy(mem, it->second->data(), n);
    if (transferred) *transferred = n;
    return FR_OK;
}

FRESULT FileManager::save_file(bool overwrite, const char *path, const char *filename, const uint8_t *mem, uint32_t len, uint32_t *transferred)
{
    std::string p = join(path, filename);
    if (files.count(p) && !overwrite) return FR_EXIST;
    fs_add_file(p, std::vector<uint8_t>(mem, mem + len));
    if (transferred) *transferred = len;
    return FR_OK;
}

FRESULT FileManager::save_temp_file(const char *category, const char *suggested_name, const uint8_t *data, uint32_t len, mstring *canonical_path_out)
{
    std::string p = join(std::string("/Temp/") + category, suggested_name);
    fs_add_file(p, std::vector<uint8_t>(data, data + len));
    if (canonical_path_out) *canonical_path_out = p.c_str();
    return FR_OK;
}
