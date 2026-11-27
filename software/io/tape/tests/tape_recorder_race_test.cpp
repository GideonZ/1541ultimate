// "Finish Rec. to TAP" while the recorder task is inside a write to the file.
//
// On the target the task has a higher priority than the task that runs the menu or the UCI
// command, so Finish can only run while the task is blocked, which it is for as long as the
// medium takes to write a block. The test has two host threads that stand for the two tasks and
// lets only one run at a time, in the order the priorities give:
//  - the task starts writing its first block and stops inside the write, as if the medium were
//    slow;
//  - Finish starts and runs until it can go no further: until it waits for the subsystem's mutex
//    (which every command takes, SubsysCommand::execute), or, without the mutex, until its own
//    first write;
//  - the write of the task completes and the task goes on until it delays (the highest priority
//    task runs as long as it can);
//  - then Finish goes on.
// Nothing here depends on how long anything takes: the threads wait for each other's state and
// for nothing else. The waits have a bound only so that a defect in the code under test shows
// as a failed test and not as a hang.
#include <thread>
#include <chrono>
#include "tape_recorder_util.h"

using namespace tapetest;

namespace {
std::thread::id task_id, ui_id;
bool task_in_write, ui_waiting, task_idle, gate_broken;

// Waits for a flag that the other thread sets under sched_m. False when it never came.
template <class P>
bool wait_flag(P pred)
{
    std::unique_lock<std::mutex> l(sched_m);
    if (!sched_cv.wait_for(l, std::chrono::seconds(10), pred)) { gate_broken = true; return false; }
    return true;
}
void set_flag(bool &flag)
{
    { std::lock_guard<std::mutex> l(sched_m); flag = true; }
    sched_cv.notify_all();
}

// blocks cached by the interrupt and not yet written, then a tail in the FIFO; Finish arrives
// while the task is in the write of the first block
void finish_while_the_task_writes(int blocks, bool &closed_once, std::string &body, std::vector<std::string> &popups)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    std::string expect;
    for (int i = 0; i < blocks; i++) { expect += expected_block(i); c.block_arrives(); }
    expect += expected_tail(100);
    c.tail_arrives(100);

    task_in_write = ui_waiting = task_idle = gate_broken = false;
    int task_writes = 0, ui_writes = 0;
    sched.threaded = true;
    sched.on_block = []() { if (std::this_thread::get_id() == ui_id) set_flag(ui_waiting); };
    sched.may_run = []() { return task_idle; };            // evaluated with sched_m held
    fs.on_write = [&]() {
        if (std::this_thread::get_id() == task_id) {
            if (task_writes++ == 0) { set_flag(task_in_write); wait_flag([]() { return ui_waiting; }); }
        } else if (std::this_thread::get_id() == ui_id) {
            if (ui_writes++ == 0) { set_flag(ui_waiting); wait_flag([]() { return task_idle; }); }
        }
    };
    rtos.on_delay = [](int) { set_flag(task_idle); throw TaskEnd(); };

    std::thread task([&]() {
        task_id = std::this_thread::get_id();
        try { rtos.run_task(); } catch (const TaskEnd &) { } catch (const Fault &e) {
            host_test::record_failure(std::string("task: ") + e.what(), false);
            set_flag(task_idle);
        }
    });
    wait_flag([]() { return task_in_write; });
    std::thread ui([&]() {
        ui_id = std::this_thread::get_id();
        try { c.finish(); } catch (const Fault &e) { host_test::record_failure(std::string("finish: ") + e.what(), false); }
    });
    task.join();
    ui.join();
    fs.on_write = 0;
    rtos.on_delay = 0;
    sched.on_block = 0;
    sched.may_run = 0;
    sched.threaded = false;
    EXPECT_FALSE(gate_broken);

    closed_once = (fs.opens == 1) && (fs.closes == 1) && (fs.null_closes == 0);
    const std::string &data = fs.file("cap.tap").content;
    body = data.size() > 20 ? data.substr(20) : std::string();
    EXPECT_TRUE(body == expect);
    if (data.size() >= 20) EXPECT_EQ((int)length_field(data), (int)expect.size());
    c.run_task_steps(1);               // the task shows an error, if there is one
    popups = c.ui.popups;
}

void check(int blocks)
{
    bool closed_once = false;
    std::string body;
    std::vector<std::string> popups;
    finish_while_the_task_writes(blocks, closed_once, body, popups);
    EXPECT_TRUE(closed_once);
    int errors = 0;
    for (size_t i = 0; i < popups.size(); i++) if (popups[i] == "Error during tape capture.") errors++;
    EXPECT_EQ(errors, 0);
    EXPECT_EQ((int)popups.size(), 1);   // "Tape capture stopped."
    // the blocks are in the file in order, each once: the layout reads b0 b1 ... then the tail
    EXPECT_EQ((int)body.size(), blocks * 512 + 100);
}
}

TEST(TapeRecorderRace, FinishWhileTheTaskWritesOneBlock) { check(1); }
TEST(TapeRecorderRace, FinishWhileTheTaskWritesTwoBlocks) { check(2); }
TEST(TapeRecorderRace, FinishWhileTheTaskWritesFourBlocks) { check(4); }
