// Healthy captures through the real TapeRecorder. They show that the register model, the fake
// file system and the scripted task follow what the firmware expects, so that the fault
// tests (tape_recorder_faults.cpp) fail for the defect and not for the harness.
#include "tape_recorder_util.h"

using namespace tapetest;

TEST(TapeRecorder, CaptureOfSeveralBlocksIsWrittenAndClosedOnce)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    ASSERT_EQ(fs.opens, 1);
    EXPECT_TRUE(fs.flags_seen[0] & FA_WRITE);
    EXPECT_TRUE(recorder.enabled);                  // start() enabled the block
    EXPECT_EQ(recorder.mode, 0);                    // rising edge, as the write menu item asks
    EXPECT_EQ(recorder.sel, 1);
    EXPECT_TRUE(recorder.irq_en);

    c.block_arrives();
    c.block_arrives();
    c.block_arrives();
    c.tail_arrives(100);
    c.run_task_steps(3);                            // the task drains the cache to the file

    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_TRUE(has_popup(c.ui, "Tape capture stopped."));

    const std::string &data = fs.file("cap.tap").content;
    ASSERT_EQ((int)data.size(), 20 + 3 * 512 + 100);
    EXPECT_TRUE(data.compare(0, 12, "C64-TAPE-RAW") == 0);
    EXPECT_EQ((int)(uint8_t)data[16] | ((int)(uint8_t)data[17] << 8), 3 * 512 + 100);   // length field
    for (int b = 0; b < 3; b++) EXPECT_TRUE(data.compare(20 + b * 512, 512, expected_block(b)) == 0);
    for (int i = 0; i < 100; i++) EXPECT_EQ((int)(uint8_t)data[20 + 3 * 512 + i], (int)(uint8_t)(0xA0 + i));

    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);
    EXPECT_EQ(fs.null_closes, 0);
    EXPECT_FALSE(recorder.enabled);
    EXPECT_EQ((int)recorder.fifo.size(), 0);
}

TEST(TapeRecorder, SampleTapeUnfreezesTheC64AndStartsReading)
{
    Capture c;
    ASSERT_EQ((int)c.menu(MENU_REC_SAMPLE_TAPE, "tape1"), (int)SSRET_OK);
    EXPECT_TRUE(has_popup(c.ui, "Start tape when back in C64 screen"));
    ASSERT_EQ((int)executed_functions.size(), 1);
    EXPECT_EQ(executed_functions[0], (int)C64_UNFREEZE);
    EXPECT_TRUE(recorder.enabled);
    EXPECT_EQ(recorder.sel, 0);
    EXPECT_EQ(recorder.mode, 1);                    // falling edge
    c.block_arrives();
    c.run_task_steps(2);
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_EQ((int)fs.file("tape1.tap").content.size(), 20 + 512);
    EXPECT_EQ(fs.opens, fs.closes);
}

TEST(TapeRecorder, EmptyCaptureIsAValidHeaderOnly)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    const std::string &data = fs.file("cap.tap").content;
    EXPECT_EQ((int)data.size(), 20);
    EXPECT_EQ((int)(uint8_t)data[16], 0);
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);
}

TEST(TapeRecorder, ModelSetsErrorWhenTheFifoOverflows)
{
    // c2n_record.vhd:74-75: a byte for a full FIFO while enabled latches the error flag.
    reset_env();
    recorder.write_control(0x01);
    std::vector<uint8_t> v(2048 + 10, 0x55);
    recorder.capture(&v[0], (int)v.size());
    EXPECT_EQ((int)recorder.fifo.size(), 2048);
    EXPECT_TRUE(recorder.read_status() & 0x02);
    EXPECT_TRUE(recorder.read_status() & 0x04);
    recorder.write_control(0x01 | 0x02);            // clear error
    EXPECT_FALSE(recorder.read_status() & 0x02);
}

TEST(TapeRecorder, FinishWithoutACaptureReportsNoFile)
{
    Capture c;
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);      // flush() with no file: the error code is set, nothing is closed
    EXPECT_EQ(fs.opens, 0);
    EXPECT_EQ(fs.closes, 0);
}

// T1: the capture overflows the cache; the task calls stop(REC_ERR_OVERFLOW). stop() flushes,
// flush() closes the file and clears the member, and stop() then closes it again.
TEST(TapeRecorder, T1_OverflowStopDoesNotCloseANullHandle)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    for (int i = 0; i < REC_NUM_CACHE_BLOCKS; i++) c.block_arrives();   // the task has not run for a long time
    bool ran = guarded("task step after a cache overflow", [&]() { c.run_task_steps(2); });
    EXPECT_EQ(fs.null_closes, 0);
    if (ran) {
        EXPECT_EQ(fs.opens, fs.closes);
    }
}

// T1, second way in: stopping through the destructor while a capture is running.
TEST(TapeRecorder, T1_DestroyingTheRecorderMidCaptureClosesTheFileOnce)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    TapeRecorder *r = c.rec;
    c.rec = NULL;
    guarded("destroying the recorder during a capture", [&]() { delete r; });
    EXPECT_EQ(fs.null_closes, 0);
    EXPECT_EQ(fs.opens, fs.closes);
}

// T2: the card reports a write error while the capture runs. The task must not spin.
TEST(TapeRecorder, T2_WriteErrorInTheTaskDoesNotLoopForever)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    fs.file("cap.tap").fail_from_write = 2;                 // the signature (write 1) went through
    fs.file("cap.tap").fail_kind = WF_ERROR;
    c.block_arrives();
    c.block_arrives();
    bool ran = guarded("task step after a write error", [&]() { c.run_task_steps(3); });
    EXPECT_TRUE(ran);
    // the writes that were attempted are bounded: it gives up or retries a few times, but does not spin
    EXPECT_TRUE(fs.writes_total < 100);
    fs.max_writes = 100000;
    fs.file("cap.tap").fail_from_write = 0;                  // so that the cleanup below can finish
}

// T2, at the end of a capture: "Finish Rec. to TAP" while the card refuses writes.
TEST(TapeRecorder, T2_FinishWithFailingWritesReturns)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    fs.file("cap.tap").fail_from_write = 2;
    bool ran = guarded("Finish with a failing card", [&]() { c.finish(); });
    EXPECT_TRUE(ran);
    fs.file("cap.tap").fail_from_write = 0;
    fs.max_writes = 100000;
}

// T2, full volume: FatFs answers FR_OK with fewer bytes than asked. The recorder must notice.
TEST(TapeRecorder, T2_ShortWriteOnAFullDiskIsReported)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    fs.file("cap.tap").fail_from_write = 2;
    fs.file("cap.tap").fail_kind = WF_SHORT;
    c.block_arrives();
    c.run_task_steps(3);
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture.") || !recorder.enabled);
    fs.file("cap.tap").fail_from_write = 0;
}

// ---- a write that fails ends the capture, once, and the user is told

TEST(TapeRecorder, WriteErrorEndsTheCaptureAndNothingIsTriedAgain)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    fs.file("cap.tap").fail_from_write = 2;                 // the signature (write 1) went through; every write after fails
    c.block_arrives();
    c.block_arrives();
    c.run_task_steps(3);
    EXPECT_FALSE(recorder.enabled);                          // the hardware is stopped
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);                                 // the file is closed, once
    EXPECT_EQ(fs.null_closes, 0);
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture."));
    // signature, the one block that failed, and the attempt to put the length in the header
    EXPECT_EQ(fs.writes_total, 3);
    c.block_arrives();                                       // a late interrupt does nothing
    c.run_task_steps(3);
    EXPECT_EQ(fs.writes_total, 3);
    EXPECT_EQ(fs.closes, 1);
}

TEST(TapeRecorder, AShortWriteOfTheBytesLeftInTheFifoIsAnErrorToo)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.tail_arrives(50);                                      // not a whole block: Finish reads it from the FIFO
    fs.file("cap.tap").fail_from_write = 2;
    fs.file("cap.tap").fail_kind = WF_SHORT;
    fs.file("cap.tap").fail_count = 1;
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    const std::string &data = fs.file("cap.tap").content;
    ASSERT_EQ((int)data.size(), 20 + 25);
    EXPECT_EQ((int)length_field(data), 25);
    EXPECT_EQ(fs.opens, fs.closes);
    c.run_task_steps(1);
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture."));
}

TEST(TapeRecorder, AfterAWriteErrorTheHeaderCountsWhatReachedTheFile)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    fs.file("cap.tap").fail_from_write = 3;                  // block 0 is written, block 1 fails
    fs.file("cap.tap").fail_count = 1;                       // the header write after it works
    c.block_arrives();
    c.block_arrives();
    c.run_task_steps(3);
    const std::string &data = fs.file("cap.tap").content;
    ASSERT_EQ((int)data.size(), 20 + 512);
    EXPECT_EQ((int)length_field(data), 512);
    EXPECT_TRUE(data.compare(20, 512, expected_block(0)) == 0);
    EXPECT_EQ(fs.opens, fs.closes);
}

TEST(TapeRecorder, AShortWriteEndsTheCaptureAndTheHeaderCountsTheBytesWritten)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    fs.file("cap.tap").fail_from_write = 2;
    fs.file("cap.tap").fail_kind = WF_SHORT;                 // FR_OK, half the bytes
    fs.file("cap.tap").fail_count = 1;
    c.block_arrives();
    c.block_arrives();
    c.run_task_steps(3);
    const std::string &data = fs.file("cap.tap").content;
    ASSERT_EQ((int)data.size(), 20 + 256);
    EXPECT_EQ((int)length_field(data), 256);
    EXPECT_FALSE(recorder.enabled);
    EXPECT_EQ(fs.opens, fs.closes);
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture."));
}

TEST(TapeRecorder, FinishWithFailingWritesEndsTheCaptureAndTheUserIsTold)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    c.tail_arrives(50);
    fs.file("cap.tap").fail_from_write = 2;
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);
    EXPECT_FALSE(recorder.enabled);
    EXPECT_EQ(fs.writes_total, 3);                           // signature, the block that failed, the header
    c.run_task_steps(1);                                     // the task shows the error
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture."));
}

// T3: the medium is removed during the capture. The file becomes invalid; the owner still has
// to close it, or the open file list keeps a handle that nothing releases.
TEST(TapeRecorder, T3_FileInvalidatedDuringCaptureIsStillClosed)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    ASSERT_EQ((int)fs.live.size(), 1);
    fs.live[0]->invalidate();       // what FileManager::invalidate does to every file of a removed medium (filemanager.cc:440)
    c.block_arrives();
    guarded("task step after the medium vanished", [&]() { c.run_task_steps(3); });
    EXPECT_EQ(fs.opens, fs.closes);
}

TEST(TapeRecorder, AFileThatVanishedEndsTheCaptureWithoutWritingAndTheUserIsTold)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    int writes = fs.writes_total;
    fs.live[0]->invalidate();
    c.run_task_steps(2);
    EXPECT_EQ(fs.writes_total, writes);                      // the file is not used any more, only closed
    EXPECT_EQ(fs.closes, 1);
    EXPECT_FALSE(recorder.enabled);
    EXPECT_TRUE(has_popup(c.ui, "Error during tape capture."));
}

// T4: the signature cannot be written when a capture starts. The file was opened; it has to be
// closed, and the next attempt has to be able to start.
TEST(TapeRecorder, T4_SignatureWriteFailureClosesTheFile)
{
    Capture c;
    fs.file("cap.tap").fail_from_write = 1;
    fs.file("cap.tap").fail_count = 1;
    guarded("start capture", [&]() { c.start_capture("cap"); });
    EXPECT_TRUE(has_popup(c.ui, "Error writing signature"));
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);
    EXPECT_FALSE(recorder.enabled);
}

TEST(TapeRecorder, T4_AfterASignatureFailureTheNextCaptureCanStart)
{
    Capture c;
    fs.file("cap.tap").fail_from_write = 1;
    fs.file("cap.tap").fail_count = 1;
    guarded("first attempt", [&]() { c.start_capture("cap"); });
    guarded("second attempt", [&]() { c.start_capture("again"); });
    EXPECT_EQ(fs.opens, 2);
    EXPECT_TRUE(recorder.enabled);
    guarded("finish", [&]() { c.finish(); });
}

TEST(TapeRecorder, AShortSignatureWriteIsAnErrorToo)
{
    Capture c;
    fs.file("cap.tap").fail_from_write = 1;
    fs.file("cap.tap").fail_count = 1;
    fs.file("cap.tap").fail_kind = WF_SHORT;
    c.start_capture("cap");
    EXPECT_TRUE(has_popup(c.ui, "Error writing signature"));
    EXPECT_EQ(fs.opens, fs.closes);
    EXPECT_FALSE(recorder.enabled);
}

// ---- the recorder task and the commands: the subsystem's mutex
// The commands that start and finish a capture hold the recorder's mutex while they run
// (SubsysCommand::execute). The task does its part of the capture only while it can take the
// mutex, and does not wait for it.

TEST(TapeRecorder, TheTaskDoesNothingWhileACommandHoldsTheMutex)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    c.block_arrives();
    int writes = fs.writes_total;
    ASSERT_TRUE(c.rec->lock("a command in progress"));
    c.run_task_steps(3);                                    // it neither waits nor writes
    EXPECT_EQ(fs.writes_total, writes);
    EXPECT_TRUE(sched.failed_tries >= 3);
    EXPECT_TRUE(recorder.enabled);                           // and it has not stopped the capture
    c.rec->unlock();
    c.run_task_steps(1);                                    // when the command is done, the cache is written
    EXPECT_EQ(fs.writes_total, writes + 2);
}

TEST(TapeRecorder, TheTaskGivesTheMutexBackWhateverItDid)
{
    // a write error, a vanished file and an overflow all end the capture from inside the task
    for (int what = 0; what < 3; what++) {
        Capture c;
        ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
        if (what == 0) { fs.file("cap.tap").fail_from_write = 2; c.block_arrives(); }
        if (what == 1) { fs.live[0]->invalidate(); }
        if (what == 2) { for (int i = 0; i < REC_NUM_CACHE_BLOCKS; i++) c.block_arrives(); }
        guarded("task step", [&]() { c.run_task_steps(2); });
        EXPECT_EQ(sched.takes, sched.gives);
        EXPECT_FALSE(recorder.enabled);
        fs.max_writes = 100000;
        EXPECT_TRUE(c.rec->lock("check"));                  // free: the command that comes next can run
        c.rec->unlock();
    }
}

TEST(TapeRecorder, FinishGivesTheMutexBackAndTheTaskCanRunAgain)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_EQ(sched.takes, sched.gives);
    int writes = fs.writes_total;
    c.run_task_steps(3);                                    // nothing left to do, and no error
    EXPECT_EQ(fs.writes_total, writes);
    EXPECT_FALSE(has_popup(c.ui, "Error during tape capture."));
}

// ---- Finish and the interrupt
// "Finish Rec. to TAP" reads the rest of the FIFO into the same cache that the interrupt fills.
// The interrupt can run at any point of Finish, and does when the almost-full line is up and
// the interrupt is enabled in the block. The test lets the encoder fill a block (and so the
// interrupt run) at every operation Finish performs on the registers, the file system and the
// close, one run per point, and checks that
//  - no file call is made on a closed file (the fake raises a Fault),
//  - the file is closed exactly once,
//  - the file holds every byte that arrived while the interrupt was enabled, in order, once,
//  - its length field says so,
//  - and the file has nothing else: the pulse the encoder writes when the block is disabled
//    never reaches it.
// The recorder task runs at every point too; it must find the capture in the hands of Finish.
namespace {
void encoder_interrupt_and_task(Capture &c, std::string &injected)
{
    if (recorder.enabled && recorder.irq_en) {
        injected += expected_block(c.blocks_sent);
        c.block_arrives();                 // the encoder fills a block; the interrupt caches it
    }
    c.run_task_steps(1);                   // one pass of the task loop
}
}

TEST(TapeRecorder, FinishWhileTheInterruptAndTheTaskCanRunAtAnyPoint)
{
    int fired_total = 0;
    for (int k = 1; k < 3000; k++) {
        Capture c;
        ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
        std::string stream;
        for (int i = 0; i < 3; i++) { stream += expected_block(i); c.block_arrives(); }   // cached by the interrupt, task has not run
        stream += expected_tail(100);
        c.tail_arrives(100);                                                               // sits in the FIFO
        int n = 0;
        bool fired = false;
        std::string at;
        preempt.hook = [&](const char *what) {
            if (++n == k) { fired = true; at = what; encoder_interrupt_and_task(c, stream); }
        };
        bool ran = guarded(("finish, task and interrupt at point " + std::to_string(k)).c_str(), [&]() { c.finish(); });
        preempt.hook = 0;
        if (!fired) break;                 // Finish has fewer points than k
        fired_total++;
        EXPECT_TRUE(ran);
        EXPECT_EQ(fs.null_closes, 0);
        EXPECT_EQ(fs.opens, 1);
        EXPECT_EQ(fs.closes, 1);
        const std::string &data = fs.file("cap.tap").content;
        bool same = data.size() == 20 + stream.size() && data.compare(20, std::string::npos, stream) == 0;
        if (!same) host_test::record_failure("content differs when the interrupt and the task run at point " + std::to_string(k) + " (" + at + ")", false);
        if (data.size() >= 20) EXPECT_EQ((int)length_field(data), (int)stream.size());
        EXPECT_FALSE(has_popup(c.ui, "Error during tape capture."));
    }
    EXPECT_TRUE(fired_total > 8);          // the sweep really went through Finish
}

// An interrupt that was already pending when Finish ran must find the capture over: it reads
// nothing from the FIFO, and the task step has nothing to write.
TEST(TapeRecorder, AfterFinishAnInterruptCachesNothingAndTheTaskWritesNothing)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    c.block_arrives();
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    int writes = fs.writes_total;
    int reads = recorder.data_reads;
    uint8_t b[512] = { 0 };
    recorder.capture(b, 512);                // the FIFO holds a block (the model does not look at the enable bit)
    c.rec->irq();
    EXPECT_EQ(recorder.data_reads, reads);
    c.run_task_steps(2);
    EXPECT_EQ(fs.writes_total, writes);
    EXPECT_EQ(fs.closes, 1);
}

// While Finish reads the FIFO the interrupt is off, but the block is still enabled: disabling
// it makes the encoder write one more entry for the pulse in progress (c2n_record.vhd:104-106),
// which Finish has always dropped by emptying the FIFO in the same register write. A
// capture must come out byte for byte as it always has.
TEST(TapeRecorder, TheEntryThatDisablingTheBlockWritesNeverReachesTheFile)
{
    for (int longpause = 0; longpause < 2; longpause++) {
        Capture c;
        if (longpause) { recorder.final_entry.clear(); recorder.final_entry.push_back(0); recorder.final_entry.push_back(0x11); recorder.final_entry.push_back(0x22); recorder.final_entry.push_back(0x33); }
        ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
        c.block_arrives();
        c.tail_arrives(30);
        ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
        const std::string &data = fs.file("cap.tap").content;
        EXPECT_TRUE(data.substr(20) == expected_block(0) + expected_tail(30));
        EXPECT_EQ((int)length_field(data), 512 + 30);
        EXPECT_EQ(recorder.final_entries, 0);       // the block is disabled and emptied in one write
        EXPECT_FALSE(recorder.enabled);
        EXPECT_FALSE(recorder.flush_level);
        EXPECT_EQ((int)recorder.fifo.size(), 0);
    }
}

// A capture with no tail: Finish with an empty FIFO must not turn the entry into a tail.
TEST(TapeRecorder, AnEmptyFifoStaysEmptyAtFinish)
{
    Capture c;
    ASSERT_EQ((int)c.start_capture(), (int)SSRET_OK);
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_EQ((int)fs.file("cap.tap").content.size(), 20);
}

// While Finish reads the FIFO the block is enabled in the mode the capture was started with, and
// only its interrupt is off. Seen from the first write Finish makes to the file.
TEST(TapeRecorder, WhileFinishWritesTheBlockKeepsItsModeAndOnlyTheInterruptIsOff)
{
    for (int sample = 0; sample < 2; sample++) {
        Capture c;
        ASSERT_EQ((int)c.menu(sample ? MENU_REC_SAMPLE_TAPE : MENU_REC_RECORD_TO_TAP), (int)SSRET_OK);
        int mode = recorder.mode, sel = recorder.sel;
        EXPECT_TRUE(recorder.irq_en);
        c.block_arrives();
        bool seen = false;
        preempt.hook = [&](const char *what) {
            if (!seen && !strcmp(what, "file write")) {
                seen = true;
                EXPECT_TRUE(recorder.enabled);
                EXPECT_FALSE(recorder.irq_en);
                EXPECT_EQ(recorder.mode, mode);
                EXPECT_EQ(recorder.sel, sel);
            }
        };
        ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
        preempt.hook = 0;
        EXPECT_TRUE(seen);
    }
}

TEST(TapeRecorder, FinishWithoutACaptureDoesNotEnableTheBlock)
{
    Capture c;
    ASSERT_EQ((int)c.finish(), (int)SSRET_OK);
    EXPECT_EQ(recorder.enables, 0);
    EXPECT_FALSE(recorder.enabled);
}
