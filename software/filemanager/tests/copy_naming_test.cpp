// System headers come first: system/integer.h defines a lowercase max() macro,
// which breaks libstdc++'s <algorithm> if that is included afterwards.
#include <algorithm>

#include "../../io/usb/tests/host_test/host_test.h"
#include "fake_filesystem.h"

namespace {

// Two independent fake filesystems mounted side by side, so that fcopy() has a
// real source and a real destination to work with.
class CopyTestEnvironment
{
public:
    FileManager *fm;
    FakeTempFileSystem *source_fs;
    FakeTempFileSystem *dest_fs;
    Node_DirectFS *source_node;
    Node_DirectFS *dest_node;

    CopyTestEnvironment() : fm(FileManager::getFileManager())
    {
        source_fs = new FakeTempFileSystem();
        dest_fs = new FakeTempFileSystem();
        source_node = new Node_DirectFS(source_fs, "Src", AM_DIR);
        dest_node = new Node_DirectFS(dest_fs, "Dst", AM_DIR);
        fm->add_root_entry(source_node);
        fm->add_root_entry(dest_node);
    }

    ~CopyTestEnvironment()
    {
        fm->remove_root_entry(source_node);
        fm->remove_root_entry(dest_node);
        delete source_node;
        delete dest_node;
        delete source_fs;
        delete dest_fs;
    }

    // Creates a file on the source filesystem. When cbm_type is given the
    // source reports it the way a D64 does: as metadata outside the filename.
    void given_source_file(const char *name, const char *cbm_type = NULL)
    {
        File *file = NULL;
        ASSERT_EQ(FR_OK, fm->fopen("/Src", name, FA_WRITE | FA_CREATE_NEW, &file));
        ASSERT_TRUE(file != NULL);
        fm->fclose(file);
        if (cbm_type) {
            source_fs->set_cbm_type(std::string("/") + name, cbm_type);
        }
    }

    FRESULT copy_to_dest(const char *name)
    {
        return fm->fcopy("/Src", name, "/Dst", name, false);
    }

    std::vector<std::string> dest_files()
    {
        std::vector<std::string> files = dest_fs->list_files();
        std::sort(files.begin(), files.end());
        return files;
    }

    bool dest_has(const char *path)
    {
        std::vector<std::string> files = dest_files();
        return std::find(files.begin(), files.end(), std::string(path)) != files.end();
    }
};

} // namespace

// --- FAT to FAT: the filename must survive the copy verbatim ---------------

TEST(CopyNamingTest, PreservesExtensionLongerThanThreeCharacters)
{
    CopyTestEnvironment env;
    env.given_source_file("foo.bar1234");

    ASSERT_EQ(FR_OK, env.copy_to_dest("foo.bar1234"));
    EXPECT_TRUE(env.dest_has("/foo.bar1234"));
}

TEST(CopyNamingTest, DistinctLongExtensionsDoNotCollideInDestination)
{
    CopyTestEnvironment env;
    env.given_source_file("foo.bar1234");
    env.given_source_file("foo.bar5678");

    ASSERT_EQ(FR_OK, env.copy_to_dest("foo.bar1234"));
    ASSERT_EQ(FR_OK, env.copy_to_dest("foo.bar5678"));

    std::vector<std::string> files = env.dest_files();
    ASSERT_EQ(2u, (unsigned)files.size());
    EXPECT_EQ(files[0], std::string("/foo.bar1234"));
    EXPECT_EQ(files[1], std::string("/foo.bar5678"));
}

TEST(CopyNamingTest, PreservesMultiPartExtension)
{
    CopyTestEnvironment env;
    env.given_source_file("archive.tar.gz");

    ASSERT_EQ(FR_OK, env.copy_to_dest("archive.tar.gz"));
    EXPECT_TRUE(env.dest_has("/archive.tar.gz"));
}

TEST(CopyNamingTest, PreservesNameWithoutExtension)
{
    CopyTestEnvironment env;
    env.given_source_file("readme");

    ASSERT_EQ(FR_OK, env.copy_to_dest("readme"));
    EXPECT_TRUE(env.dest_has("/readme"));
}

TEST(CopyNamingTest, DoesNotDuplicateShortExtension)
{
    CopyTestEnvironment env;
    env.given_source_file("game.prg");

    ASSERT_EQ(FR_OK, env.copy_to_dest("game.prg"));
    EXPECT_TRUE(env.dest_has("/game.prg"));
}

// --- CBM to FAT: the file type is metadata and must be appended ------------

TEST(CopyNamingTest, AppendsCbmTypeWhenSourceNameHasNoExtension)
{
    CopyTestEnvironment env;
    env.given_source_file("FOO", "PRG");

    ASSERT_EQ(FR_OK, env.copy_to_dest("FOO"));
    EXPECT_TRUE(env.dest_has("/FOO.prg"));
}

TEST(CopyNamingTest, AppendsCbmTypeWithoutTruncatingDottedCbmName)
{
    CopyTestEnvironment env;
    env.given_source_file("MY.DOCUMENT", "SEQ");

    ASSERT_EQ(FR_OK, env.copy_to_dest("MY.DOCUMENT"));
    EXPECT_TRUE(env.dest_has("/MY.DOCUMENT.seq"));
}

// --- regression guards for the ROM / cartridge flashing flow ---------------
//
// filetype_bin.cc, filetype_crt.cc and filetype_vpl.cc are the only callers
// that copy under a different name. They all build that name with
// truncate_filename(), so the destination must keep the source extension
// exactly as it did before.

TEST(CopyNamingTest, RomFlashFlowKeepsShortName)
{
    CopyTestEnvironment env;
    env.given_source_file("kernal.rom");

    char fnbuf[32];
    truncate_filename("kernal.rom", fnbuf, 30);
    ASSERT_EQ(FR_OK, env.fm->fcopy("/Src", "kernal.rom", "/Dst", fnbuf, false));
    EXPECT_TRUE(env.dest_has("/kernal.rom"));
}

TEST(CopyNamingTest, RomFlashFlowTruncatesLongNameButKeepsExtension)
{
    CopyTestEnvironment env;
    const char *source = "a_very_long_kernal_filename_here.rom";
    env.given_source_file(source);

    char fnbuf[32];
    truncate_filename(source, fnbuf, 30);
    ASSERT_EQ(FR_OK, env.fm->fcopy("/Src", source, "/Dst", fnbuf, false));

    std::vector<std::string> files = env.dest_files();
    ASSERT_EQ(1u, (unsigned)files.size());
    EXPECT_TRUE(files[0].size() <= 30u);
    EXPECT_TRUE(files[0].compare(files[0].size() - 4, 4, ".rom") == 0);
}

TEST(CopyNamingTest, UppercaseExtensionStillDetectedAsItsFileType)
{
    // The FAT layer uppercases FileInfo::extension when reading a directory, so
    // preserving the on-disk case of the name must not change file type
    // detection, which compares against the uppercased extension.
    CopyTestEnvironment env;
    env.given_source_file("GAME.CRT");

    ASSERT_EQ(FR_OK, env.copy_to_dest("GAME.CRT"));

    FileInfo info(64);
    ASSERT_EQ(FR_OK, env.fm->fstat("/Dst", "GAME.CRT", info));
    EXPECT_EQ(strcmp(info.extension, "CRT"), 0);
}
