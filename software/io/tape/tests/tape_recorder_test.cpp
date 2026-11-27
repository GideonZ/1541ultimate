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
