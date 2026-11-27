#include "host_test/host_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <chrono>
#include "tape_env.h"
#include "tape_regs.h"
#include "tape_controller.h"
#include "filemanager.h"
#include "subsys.h"
#include "init_function.h"
#include "iomap.h"

volatile uint8_t tape_test_profiler_sub = 0;
TapeController *tape_controller = NULL;
void TapeController::close() { closes++; }

namespace tapeenv {

// ------------------------------------------------------------------ recorder model
void RecorderModel::reset()
{
    fifo.clear();
    enabled = error = irq_en = flush_level = stream_en = false;
    mode = sel = 0;
    control_writes = data_reads = 0;
    sense = true;
    final_entry.assign(1, 0x01);
    final_entries = 0;
    enables = 0;
}

uint8_t RecorderModel::read_status()
{
    uint8_t s = 0;
    int n = (int)fifo.size();
    if (enabled)                    s |= 0x01;   // status(0)
    if (error)                      s |= 0x02;   // status(1)
    if (n >= FIFO_DEPTH)            s |= 0x04;   // status(2) fifo_full
    if (n >= FIFO_THRESHOLD)        s |= 0x08;   // status(3) fifo_almostfull
    if (stream_en && enabled)       s |= 0x40 | 0x20;  // status(6), and state_enc "listen" = "10" in bits 5..4
    if (n > 0)                      s |= 0x80;   // status(7) not fifo_empty
    return s;
}

void RecorderModel::write_control(uint8_t v)
{
    control_writes++;
    bool was_enabled = enabled;
    enabled = v & 0x01;
    if (enabled && !was_enabled) enables++;
    if (v & 0x02) error = false;
    flush_level = (v & 0x04) != 0;
    if (flush_level) fifo.clear();               // the FIFO is held empty while the flush bit is set
    if (was_enabled && !enabled && sense && !flush_level) {   // c2n_record.vhd:104-106
        for (size_t i = 0; i < final_entry.size() && (int)fifo.size() < FIFO_DEPTH; i++) fifo.push_back(final_entry[i]);
        final_entries++;
    }
    mode = (v >> 4) & 3;
    sel = (v >> 6) & 1;
    irq_en = (v & 0x80) != 0;
}

uint8_t RecorderModel::read_data8()
{
    data_reads++;
    if (fifo.empty()) return 0;
    uint8_t b = fifo.front();
    fifo.pop_front();
    return b;
}

uint32_t RecorderModel::read_data32()
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)read_data8() << (8 * i);
    return v;
}

void RecorderModel::capture(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        if (flush_level) continue;
        if ((int)fifo.size() >= FIFO_DEPTH) {
            if (enabled) error = true;           // c2n_record.vhd:74-75
            continue;
        }
        fifo.push_back(p[i]);
    }
}

RecorderModel recorder;
ControlReg control_reg;
uint8_t reg_status() { preempt_point("status read"); return recorder.read_status(); }
uint8_t reg_data8() { preempt_point("data read"); return recorder.read_data8(); }
uint32_t reg_data32() { preempt_point("data read"); return recorder.read_data32(); }
void ControlReg::operator=(uint8_t v) { preempt_point("control write"); recorder.write_control(v); }

// ------------------------------------------------------------------ preemption
Preempt preempt;
void preempt_point(const char *what)
{
    if (++preempt.points > preempt.max_points)
        throw StepLimit("more than " + std::to_string(preempt.max_points) + " operations on the outside world: a loop that does not end");
    if (preempt.hook && preempt.depth == 0) {
        preempt.depth++;
        try { preempt.hook(what); } catch (...) { preempt.depth--; throw; }
        preempt.depth--;
    }
}
void safe_enter() { preempt.enters++; preempt.depth++; }
void safe_leave() { preempt.leaves++; preempt.depth--; }

// ------------------------------------------------------------------ fake files
FsLog fs;

FakeFile::FakeFile(const std::string &n, FileScript *s)
    : File((FileSystem *)&fs),     // any non-NULL pointer makes the File valid; it is never dereferenced
      name(n), script(s), pos(0), writes(0), closed(false) { }

FRESULT FakeFile::read(void *buffer, uint32_t len, uint32_t *transferred)
{
    if (closed) throw Fault("read after close of " + name);
    *transferred = 0;
    if (!isValid()) return FR_NOT_READY;
    if (script->read_error) { script->read_error = 0; return FR_DISK_ERR; }
    uint32_t avail = pos < script->content.size() ? (uint32_t)script->content.size() - pos : 0;
    uint32_t n = len < avail ? len : avail;
    memcpy(buffer, script->content.data() + pos, n);
    pos += n;
    *transferred = n;
    return FR_OK;
}

FRESULT FakeFile::write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    preempt_point("file write");
    if (fs.on_write) fs.on_write();
    if (closed) throw Fault("write after close of " + name);
    if (++fs.writes_total > fs.max_writes)
        throw StepLimit("more than " + std::to_string(fs.max_writes) + " writes: runaway loop");
    writes++;
    *transferred = 0;
    if (!isValid()) return FR_NOT_READY;         // as FileFAT::write does, filesystem_fat.cc:203
    uint32_t n = len;
    bool failing = script->fail_from_write && writes >= script->fail_from_write &&
                   (script->fail_count < 0 || writes < script->fail_from_write + script->fail_count);
    FRESULT res = FR_OK;
    if (failing) {
        if (script->fail_kind == WF_ERROR) return FR_DISK_ERR;
        if (script->fail_kind == WF_SHORT) n = len / 2;   // FatFs on a full volume: FR_OK, fewer bytes
    }
    if (script->content.size() < pos + n) script->content.resize(pos + n);
    memcpy(&script->content[pos], buffer, n);
    pos += n;
    *transferred = n;
    if (script->invalidate_after_write && writes == script->invalidate_after_write) invalidate();
    return res;
}

FRESULT FakeFile::seek(uint32_t p)
{
    preempt_point("file seek");
    if (closed) throw Fault("seek after close of " + name);
    if (!isValid()) return FR_NOT_READY;
    pos = p;
    return FR_OK;
}

uint32_t FakeFile::get_size(void) { return (uint32_t)script->content.size(); }

static std::string lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = s[i] - 'A' + 'a';
    return s;
}

FileScript &FsLog::file(const std::string &name) { return files[lower(name)]; }
void FsLog::put(const std::string &name, const std::string &content)
{
    FileScript &s = files[lower(name)];
    s.content = content;
    s.existing = true;
}

void FsLog::reset()
{
    release_graveyard();
    for (size_t i = 0; i < live.size(); i++) delete live[i];
    live.clear();
    opens = closes = null_closes = writes_total = 0;
    opened.clear();
    flags_seen.clear();
    files.clear();
    quiet_null_close = false;
    next_open_result = FR_OK;
    max_writes = 2000;
    on_write = 0;
}

void FsLog::release_graveyard()
{
    for (size_t i = 0; i < graveyard.size(); i++) delete graveyard[i];
    graveyard.clear();
}

// ------------------------------------------------------------------ RTOS
Rtos rtos;
std::vector<int> executed_functions;

// ------------------------------------------------------------------ allocation counters
long g_news = 0, g_deletes = 0, g_mismatch = 0;
long live_allocations() { return g_news - g_deletes; }
long mismatched_deletes() { return g_mismatch; }

}

using namespace tapeenv;

// ---- FileManager: the fake
FileManager *FileManager::getFileManager() { static FileManager fm; return &fm; }

FRESULT FileManager::fopen(const char *, const char *filename, uint8_t flags, File **f)
{
    *f = NULL;
    if (fs.next_open_result != FR_OK) return fs.next_open_result;
    std::string name = lower(filename);
    bool create = (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS)) != 0;
    std::map<std::string, FileScript>::iterator it = fs.files.find(name);
    bool present = it != fs.files.end() && it->second.existing;
    if (!present) {
        if (!create) return FR_NO_FILE;
        if (it == fs.files.end()) it = fs.files.insert(std::make_pair(name, FileScript())).first;
        it->second.existing = true;
        it->second.content.clear();
    } else if (create && (flags & FA_CREATE_NEW)) {
        return FR_EXIST;
    } else if (create) {
        it->second.content.clear();
    }
    FakeFile *ff = new FakeFile(name, &it->second);
    fs.live.push_back(ff);
    fs.opens++;
    fs.opened.push_back(filename);
    fs.flags_seen.push_back(flags);
    *f = ff;
    return FR_OK;
}

FRESULT FileManager::fopen(Path *, const char *filename, uint8_t flags, File **f)
{
    return fopen("", filename, flags, f);
}

static void close_it(File *f)
{
    if (!f) {
        fs.null_closes++;
        // The real FileManager::fclose starts with f->get_file_system() (filemanager.cc:975), a
        // load through a null pointer. Say so loudly, fail the running test, and carry on so that
        // the rest of the scenario is still seen.
        if (!fs.quiet_null_close) host_test::record_failure("FileManager::fclose(NULL): the real one dereferences it (filemanager.cc:975)", false);
        return;
    }
    FakeFile *ff = (FakeFile *)f;
    for (size_t i = 0; i < fs.live.size(); i++) {
        if (fs.live[i] == ff) {
            fs.live.erase(fs.live.begin() + i);
            ff->closed = true;
            fs.closes++;
            fs.graveyard.push_back(ff);
            return;
        }
    }
    host_test::record_failure("FileManager::fclose of a handle that is not open (closed twice)", false);
}

void FileManager::fclose(File *f)
{
    preempt_point("before close");
    close_it(f);
    preempt_point("after close");     // the file is gone, the caller has not yet forgotten the pointer
}

const char *FileSystem::get_error_string(FRESULT res) { return "file system error"; }

// ---- RTOS
BaseType_t xTaskCreate(TaskFunction_t fn, const char *, uint16_t, void *arg, int, TaskHandle_t *h)
{
    rtos.task_fn = fn;
    rtos.task_arg = arg;
    if (h) *h = (TaskHandle_t)fn;
    return pdPASS;
}

void vTaskDelay(TickType_t)
{
    int step = ++rtos.delays;
    if (step > rtos.max_delays) throw StepLimit("task delay bound exceeded");
    if (rtos.on_delay) rtos.on_delay(step);
}

namespace tapeenv {
Sched sched;
std::mutex sched_m;
std::condition_variable sched_cv;
struct FakeMutex { bool held = false; };
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)new FakeMutex; }
void vSemaphoreDelete(SemaphoreHandle_t s) { delete (FakeMutex *)s; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    FakeMutex *m = (FakeMutex *)s;
    std::unique_lock<std::mutex> l(sched_m);
    if (!m->held) {
        m->held = true;
        sched.takes++;
        return pdTRUE;
    }
    if (ticks == 0) {
        sched.failed_tries++;
        return pdFALSE;
    }
    if (!sched.threaded) throw Fault("a take that would wait for ever: the mutex is held and there is no other thread to give it");
    sched.blocked_takes++;
    std::function<void()> on_block = sched.on_block;
    if (on_block) { l.unlock(); on_block(); l.lock(); }
    // the bound is for a test that has gone wrong; a take that is waited for takes no time
    bool ok = sched_cv.wait_for(l, std::chrono::seconds(10), [&]() { return !m->held && (!sched.may_run || sched.may_run()); });
    if (!ok) throw Fault("a take that did not end");
    m->held = true;
    sched.takes++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    FakeMutex *m = (FakeMutex *)s;
    {
        std::lock_guard<std::mutex> l(sched_m);
        if (!m->held) throw Fault("give of a mutex that is not held");
        m->held = false;
        sched.gives++;
    }
    sched_cv.notify_all();
    return pdTRUE;
}

// ---- system services of the target
uint32_t getFpgaCapabilities() { return 0x00000180; }   // streamer and recorder present
void ioWrite8(uint32_t, uint8_t) { }
uint8_t ioRead8(uint32_t) { return 0; }
InitFunction::InitFunction(const char *n, initFunction_t f, void *o, void *p, int ord)
    : name(n), function(f), object(o), param(p), ordering(ord) { }
InitFunction::~InitFunction() { }

SubsysResultCode_t SubsysCommand::execute(void)
{
    executed_functions.push_back(functionID);
    delete this;                      // as the real execute() does, subsys.cc:29
    SubsysResultCode_t ok = { SSRET_OK };
    return ok;
}

// ---- global new/delete, with a header so that a mismatch is seen
namespace tapeenv { extern long g_news, g_deletes, g_mismatch; }
namespace {
const uint32_t KIND_SCALAR = 0x5ca1a400, KIND_ARRAY = 0xa22a7000;
struct Hdr { uint32_t kind; uint32_t pad[3]; };
void *take(size_t n, uint32_t kind)
{
    Hdr *h = (Hdr *)malloc(n + sizeof(Hdr));
    if (!h) throw std::bad_alloc();
    h->kind = kind;
    tapeenv::g_news++;
    return h + 1;
}
void give(void *p, uint32_t kind)
{
    if (!p) return;
    Hdr *h = (Hdr *)p - 1;
    if (h->kind != kind) tapeenv::g_mismatch++;
    tapeenv::g_deletes++;
    free(h);
}
}
#ifndef TAPE_NO_ALLOC_COUNT   // a sanitizer build leaves operator new alone, so that it sees the allocations itself
void *operator new(size_t n) { return take(n, KIND_SCALAR); }
void *operator new[](size_t n) { return take(n, KIND_ARRAY); }
void operator delete(void *p) noexcept { give(p, KIND_SCALAR); }
void operator delete[](void *p) noexcept { give(p, KIND_ARRAY); }
void operator delete(void *p, size_t) noexcept { give(p, KIND_SCALAR); }
void operator delete[](void *p, size_t) noexcept { give(p, KIND_ARRAY); }
#endif
