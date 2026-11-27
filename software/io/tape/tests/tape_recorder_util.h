// Shared by the recorder tests: a fixture that starts the real TapeRecorder against the
// scripted environment, and the helpers that play the parts of the interrupt and of the task.
#ifndef TAPE_RECORDER_UTIL_H
#define TAPE_RECORDER_UTIL_H
#include "tape_test_util.h"
#include "tape_recorder.h"      // the generated copy, see the Makefile

namespace tapetest {

// What the recorder object is driven through: the same calls the menu and the interrupt make.
struct Capture {
    UserInterface ui;
    TapeRecorder *rec;
    int blocks_sent;
    Capture() : rec(NULL), blocks_sent(0) { reset_env(); rec = new TapeRecorder; }
    ~Capture() { fs.quiet_null_close = true; try { delete rec; } catch (...) { } fs.release_graveyard(); }

    // A command to the recorder, as SubsysCommand::execute (infra/subsys.cc:19-22) runs it: the
    // subsystem's mutex is held while executeCommand runs. This is how the menu items "Sample
    // tape to TAP", "Capture save to TAP" and "Finish Rec. to TAP" and the UCI command reach
    // the recorder.
    SubsysResultCode_e menu(int function, const char *name = "cap")
    {
        ui.name_to_give = name;
        SubsysCommand cmd(&ui, SUBSYSID_TAPE_RECORDER, function, 0, "/Usb0", "");
        if (!rec->lock("test")) return SSRET_NO_LOCK;
        SubsysResultCode_e r;
        try {
            r = rec->executeCommand(&cmd);
        } catch (...) {
            rec->unlock();
            throw;
        }
        rec->unlock();
        return r;
    }
    SubsysResultCode_e start_capture(const char *name = "cap") { return menu(MENU_REC_RECORD_TO_TAP, name); }
    SubsysResultCode_e finish() { return menu(MENU_REC_FINISH); }

    // The pulse encoder delivers a block's worth of bytes and the almost-full interrupt fires.
    void block_arrives()
    {
        uint8_t b[512];
        for (int i = 0; i < 512; i++) b[i] = (uint8_t)(blocks_sent * 7 + i * 3 + 1);
        blocks_sent++;
        recorder.capture(b, 512);
        rec->irq();
    }
    // Bytes that arrive but do not fill a block, so no interrupt.
    void tail_arrives(int n)
    {
        uint8_t b[512];
        for (int i = 0; i < n; i++) b[i] = (uint8_t)(0xA0 + i);
        recorder.capture(b, n);
    }
    // The recorder task: runs the real loop body until the Nth delay.
    void run_task_steps(int steps)
    {
        rtos.on_delay = [steps](int step) { if (step >= steps) throw TaskEnd(); };
        try { rtos.run_task(); } catch (const TaskEnd &) { }
        rtos.on_delay = 0;
    }
};

inline std::string expected_block(int n)
{
    std::string s;
    for (int i = 0; i < 512; i++) s.push_back((char)(uint8_t)(n * 7 + i * 3 + 1));
    return s;
}

inline std::string expected_tail(int n)
{
    std::string s;
    for (int i = 0; i < n; i++) s.push_back((char)(uint8_t)(0xA0 + i));
    return s;
}

inline uint32_t length_field(const std::string &d)
{
    return (uint8_t)d[16] | ((uint8_t)d[17] << 8) | ((uint8_t)d[18] << 16) | ((uint32_t)(uint8_t)d[19] << 24);
}

}
#endif
