// The TAP file type: a normal .tap with and without an index file, through the same calls
// the browser and the context menu make.
#include <string.h>
#include "host_test/host_test.h"
#include "tape_env.h"
#include "tape_test_util.h"
#include "tape_file_util.h"

using namespace tapetest;

static const int TAPFILE_START = 0x3110;

TEST(TapFile, OpenWithIndexListsTheEntriesAndClosesTheIndexFile)
{
    reset_env();
    fs.put("game.idx", "0x0 Intro\n0x1234 Level 1 ; comment\n");
    BrowsableDirEntry entry("game.tap", "/Usb0", "TAP");
    FileType *ft = FileTypeTap::test_type(&entry);
    ASSERT_TRUE(ft != NULL);
    IndexedList<Browsable *> list(4, NULL);
    EXPECT_EQ(ft->getCustomBrowsables(&entry, list), 1);
    ASSERT_EQ(list.get_elements(), 2);
    EXPECT_TRUE(strcmp(list[0]->getName(), "BrowsableTapEntry") == 0);
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 1);
    EXPECT_TRUE(strcasecmp(fs.opened[0].c_str(), "game.idx") == 0);
    for (int i = 0; i < list.get_elements(); i++) delete list[i];
    delete ft;
    fs.release_graveyard();
}

TEST(TapFile, OpenWithoutIndexReportsNone)
{
    reset_env();
    BrowsableDirEntry entry("game.tap", "/Usb0", "TAP");
    FileType *ft = FileTypeTap::test_type(&entry);
    ASSERT_TRUE(ft != NULL);
    IndexedList<Browsable *> list(4, NULL);
    EXPECT_EQ(ft->getCustomBrowsables(&entry, list), -1);
    EXPECT_EQ(list.get_elements(), 0);
    EXPECT_EQ(fs.opens, 0);                          // the index file does not exist
    delete ft;
}

TEST(TapFile, NonTapEntryIsNotClaimed)
{
    reset_env();
    BrowsableDirEntry entry("game.d64", "/Usb0", "D64");
    EXPECT_TRUE(FileTypeTap::test_type(&entry) == NULL);
}

TEST(TapFile, StartHandsAValidTapToTheTapeController)
{
    TapeEnvScope scope;
    scope.put_tap("good.tap", 100);
    SubsysResultCode_e r = scope.execute(0x3110 /* TAPFILE_START */, "good.tap");
    EXPECT_EQ((int)r, (int)SSRET_OK);
    EXPECT_EQ(tape_controller->set_file_calls, 1);
    EXPECT_EQ(tape_controller->length, 100u);
    EXPECT_EQ(tape_controller->started, 1);
    ASSERT_EQ((int)executed_functions.size(), 1);
    EXPECT_EQ(executed_functions[0], (int)C64_UNFREEZE);
    EXPECT_EQ(fs.opens, 1);
    EXPECT_EQ(fs.closes, 0);                         // the controller owns the open file now
    EXPECT_TRUE(tape_controller->file != NULL);
}

TEST(TapFile, MissingTapIsReportedAndNothingIsOpen)
{
    TapeEnvScope scope;
    SubsysResultCode_e r = scope.execute(0x3110, "nothere.tap");
    EXPECT_EQ((int)r, (int)SSRET_CANNOT_OPEN_FILE);
    EXPECT_TRUE(has_popup(scope.ui, "Can't open TAP file."));
    EXPECT_EQ(fs.opens, fs.closes);
}

// T5: a file that is not a good TAP is opened, rejected, and must be closed again.
TEST(TapFile, T5_BadSignatureClosesTheFile)
{
    TapeEnvScope scope;
    fs.put("bad.tap", "NOT-A-TAPE-FILE-AT-ALL-0123456789");
    SubsysResultCode_e r = scope.execute(TAPFILE_START, "bad.tap");
    EXPECT_EQ((int)r, (int)SSRET_ERROR_IN_FILE_FORMAT);
    EXPECT_EQ(tape_controller->set_file_calls, 0);
    EXPECT_EQ(fs.opens, fs.closes);
}

TEST(TapFile, T5_ShortFileClosesTheFile)
{
    TapeEnvScope scope;
    fs.put("short.tap", "C64-TAPE-RAW");
    SubsysResultCode_e r = scope.execute(TAPFILE_START, "short.tap");
    EXPECT_EQ((int)r, (int)SSRET_ERROR_IN_FILE_FORMAT);
    EXPECT_EQ(fs.opens, fs.closes);
}

TEST(TapFile, T5_ReadErrorOnTheHeaderClosesTheFile)
{
    TapeEnvScope scope;
    scope.put_tap("err.tap", 10);
    fs.file("err.tap").read_error = 1;
    SubsysResultCode_e r = scope.execute(TAPFILE_START, "err.tap");
    EXPECT_EQ((int)r, (int)SSRET_FILE_READ_FAILED);
    EXPECT_EQ(fs.opens, fs.closes);
}

// Found while building the harness: parseIndexFile allocates two char arrays with new[] and
// releases them with delete.
TEST(TapFile, X_IndexBuffersAreReleasedWithDeleteArray)
{
    reset_env();
    fs.put("game.idx", "0x0 Intro\n0x1234 Level 1\n");
    long before = mismatched_deletes();
    browse_with_index();
    EXPECT_EQ((int)(mismatched_deletes() - before), 0);
}
