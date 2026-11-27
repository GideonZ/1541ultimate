// The scripted environment of the tape tests: a model of the recorder registers, a fake
// FileManager and File, the RTOS task and delay, and global allocation counters.
//
// Nothing here knows what the tape code should do. It only does what it is told and
// keeps count; the assertions live in the tests.
#ifndef TAPE_TESTS_ENV_H
#define TAPE_TESTS_ENV_H
#include <stdint.h>
#include <stdexcept>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "file.h"
#include "FreeRTOS.h"

namespace tapeenv {

// Thrown by the fake so that a defect shows as a failed test instead of a crash or a hang.
struct Fault : std::runtime_error { explicit Fault(const std::string &m) : std::runtime_error(m) { } };
struct StepLimit : Fault { explicit StepLimit(const std::string &m) : Fault(m) { } };
// Thrown by the scripted delay to end the task loop the way a test decides.
struct TaskEnd { };

// ---- the recorder block, as modelled from fpga/io/c2n_record/vhdl_source/c2n_record.vhd ----
struct RecorderModel {
    static const int FIFO_DEPTH = 2048;      // sync_fifo g_depth (c2n_record.vhd:211)
    static const int FIFO_THRESHOLD = 512;   // g_threshold (c2n_record.vhd:213); almost_full when count >= 512
    std::deque<uint8_t> fifo;
    bool enabled, error, irq_en, flush_level, stream_en;
    int mode, sel;
    int control_writes, data_reads;
    // The tape is running (the C2N sense line), so the encoder is in its "listen" state. When
    // the block is disabled in that state it still writes one entry for the pulse in progress
    // (c2n_record.vhd:104-106: disabling sets read_event, and the encoder turns it into an
    // entry), unless the FIFO is being flushed at that moment. The entry is one byte, or four
    // when the pause was long (c2n_record.vhd:157-160, :171-185).
    bool sense;
    std::vector<uint8_t> final_entry;
    int final_entries;       // how many of them the model has written
    int enables;             // how many times the block went from disabled to enabled
    RecorderModel() { reset(); }
    void reset();
    uint8_t read_status();                    // c2n_record.vhd:233-240 and :118-119
    void    write_control(uint8_t v);         // c2n_record.vhd:100-114
    uint8_t read_data8();                     // c2n_record.vhd:120-121 and :207 (a read pops one byte)
    uint32_t read_data32();                   // four byte reads, little endian (assumed bus bridge)
    // The interrupt request line: irq <= irq_en and fifo_almostfull (c2n_record.vhd:132).
    bool irq_line() const { return irq_en && (int)fifo.size() >= FIFO_THRESHOLD; }
    // The capture side: bytes the encoder would write (c2n_record.vhd:74-75, :136-189).
    // A byte arriving at a full FIFO sets the error flag and is lost.
    void capture(const uint8_t *p, int n);
};
extern RecorderModel recorder;

// ---- the fake file system ----
enum WriteFault { WF_NONE, WF_ERROR, WF_SHORT };
struct FileScript {
    std::string content;        // what a read sees / what writes build
    int fail_from_write = 0;    // 1-based number of the first write that fails, 0 never
    int fail_count = -1;        // how many writes fail from there, -1 forever
    WriteFault fail_kind = WF_ERROR;
    int invalidate_after_write = 0;  // after this many writes the medium "disappears" (File::invalidate)
    int read_error = 0;         // the first read fails
    bool existing = false;      // the file is on the volume (made by put() or by a create)
};

class FakeFile : public File {
public:
    FakeFile(const std::string &name, FileScript *s);
    FRESULT close(void) { return FR_OK; }   // the FileManager fake owns the lifetime
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
    uint32_t get_size(void);
    std::string name;
    FileScript *script;
    uint32_t pos;
    int writes;
    bool closed;
};

struct FsLog {
    int opens, closes, null_closes, writes_total;
    std::vector<std::string> opened;
    std::vector<uint8_t> flags_seen;
    std::map<std::string, FileScript> files;   // by file name
    std::vector<FakeFile *> live;
    std::vector<FakeFile *> graveyard;
    FRESULT next_open_result;                  // FR_OK normally
    int max_writes;                            // a hard bound; more writes means a runaway loop
    // A hook at the start of every write, for a test that runs two threads (see Sched).
    std::function<void()> on_write;
    FsLog() { reset(); }
    // The script of a file, by name. A FAT volume does not tell upper case from lower case.
    FileScript &file(const std::string &name);
    // Puts a file on the volume.
    void put(const std::string &name, const std::string &content);
    bool quiet_null_close;                     // set while a test fixture cleans up; the call is still counted
    void reset();
    void release_graveyard();
};
extern FsLog fs;

// ---- the RTOS ----
struct Rtos {
    void (*task_fn)(void *);
    void *task_arg;
    int delays;
    int max_delays;
    std::function<void(int)> on_delay;       // called at every vTaskDelay with the step number
    Rtos() { reset(); }
    void reset() { task_fn = 0; task_arg = 0; delays = 0; max_delays = 100000; on_delay = 0; }
    // Runs the registered task body. It ends when on_delay throws TaskEnd.
    void run_task() { task_fn(task_arg); }
};
extern Rtos rtos;

// ---- preemption
// On the target the interrupt and the recorder task (the highest priority) can run between any
// two statements of code on a lower priority task, unless a critical section is open. The model
// has no threads; instead the code under test calls preempt_point at each of its operations on
// the outside world (register access, file system call, close), and a test installs a hook that
// plays what the interrupt and the task would have done at that moment. The hook does not run
// inside a critical section, nor inside itself.
struct Preempt {
    std::function<void(const char *)> hook;
    int depth;               // critical sections open, and 1 while the hook runs
    int enters, leaves;
    int points;              // preemption points passed so far
    int max_points;          // a hard bound: more points means a loop that does not end
    Preempt() { reset(); }
    void reset() { hook = 0; depth = 0; enters = leaves = points = 0; max_points = 200000; }
};
extern Preempt preempt;
void preempt_point(const char *what);

// ---- the subsystem mutex, and a test that runs two threads
// SemaphoreHandle_t of the fake is a mutex that is taken and given as on the target (not
// recursive: a second take by the owner would wait for ever). A take with a zero timeout
// answers at once. A take that would have to wait is only possible when the test runs real
// threads (threaded); with one thread nothing could ever release the mutex, and the fake says so
// instead of hanging.
//
// A threaded test takes the place of the scheduler: it lets one thread run at a time, in the
// order the priorities of the target give, using sched_m and sched_cv for its own flags too.
// on_block runs when a take starts to wait, may_run decides when a waiting take may go on.
struct Sched {
    bool threaded;
    std::function<void()> on_block;
    std::function<bool()> may_run;
    int takes, gives, failed_tries, blocked_takes;
    Sched() { reset(); }
    void reset() { threaded = false; on_block = 0; may_run = 0; takes = gives = failed_tries = blocked_takes = 0; }
};
extern Sched sched;
extern std::mutex sched_m;
extern std::condition_variable sched_cv;

// ---- the commands the firmware issues through SubsysCommand::execute ----
extern std::vector<int> executed_functions;

// ---- allocations ----
long live_allocations();      // operator new minus operator delete, whole process
long mismatched_deletes();    // new[] released with delete, or new with delete[]

}
#endif
