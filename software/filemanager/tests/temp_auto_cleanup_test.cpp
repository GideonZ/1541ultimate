#include "../../io/usb/tests/host_test/host_test.h"
#include "fake_filesystem.h"

namespace {

class TempTestEnvironment
{
public:
    FileManager *fm;
    FakeTempFileSystem *filesystem;
    Node_DirectFS *temp_node;
    bool root_attached;

    TempTestEnvironment() : fm(FileManager::getFileManager())
    {
        filesystem = new FakeTempFileSystem();
        temp_node = new Node_DirectFS(filesystem, "Temp", AM_DIR);
        fm->add_root_entry(temp_node);
        root_attached = true;
    }

    ~TempTestEnvironment()
    {
        if (!root_attached) {
            fm->add_root_entry(temp_node);
            root_attached = true;
        }
        remove_all_files();
        detach_root();
        delete temp_node;
        delete filesystem;
    }

    void reset(void)
    {
        remove_all_files();
        filesystem->reset();
    }

    void remove_all_files()
    {
        std::vector<std::string> files = filesystem->list_files();
        for (size_t i = 0; i < files.size(); i++) {
            std::string absolute_path = std::string("/Temp") + files[i];
            fm->delete_file(absolute_path.c_str());
        }
    }

    void detach_root()
    {
        if (!root_attached) {
            return;
        }
        fm->remove_root_entry(temp_node);
        root_attached = false;
    }
};

std::string create_managed_temp_file(TempTestEnvironment &env, const char *category, const char *suggested_name, uint32_t size)
{
    File *file = NULL;
    mstring path;
    ASSERT_EQ(FR_OK, env.fm->create_temp_file(category, suggested_name, FA_WRITE | FA_CREATE_ALWAYS, &file, &path));
    ASSERT_TRUE(file != NULL);
    std::vector<uint8_t> data(size, 0xAA);
    uint32_t written = 0;
    ASSERT_EQ(FR_OK, file->write(data.data(), size, &written));
    ASSERT_EQ(size, written);
    env.fm->fclose(file);
    return path.c_str();
}

std::string create_managed_temp_file(TempTestEnvironment &env, const char *suggested_name, uint32_t size)
{
    return create_managed_temp_file(env, "upload", suggested_name, size);
}

File *create_open_managed_temp_file(TempTestEnvironment &env, const char *suggested_name, mstring &path)
{
    File *file = NULL;
    ASSERT_EQ(FR_OK, env.fm->create_temp_file("upload", suggested_name, FA_WRITE | FA_CREATE_ALWAYS, &file, &path));
    ASSERT_TRUE(file != NULL);
    return file;
}

std::string temp_relative_path(const std::string &absolute_path)
{
    if (absolute_path.find("/Temp") == 0) {
        return absolute_path.substr(5);
    }
    return absolute_path;
}

} // namespace

TEST(TempAutoCleanupTest, AutoCleanupDisabledKeepsManagedFiles)
{
    set_auto_cleanup(false);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    std::vector<std::string> paths;
    for (int i = 0; i < 11; i++) {
        paths.push_back(create_managed_temp_file(env, "upload.bin", 16));
    }

    EXPECT_EQ(0, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(11, (int)env.filesystem->list_files().size());

    FileInfo info(64);
    EXPECT_EQ(FR_OK, env.fm->fstat(paths.front().c_str(), info));
}

TEST(TempAutoCleanupTest, AutoCleanupDisabledFilesStayUntrackedWhenCleanupTurnsOn)
{
    set_auto_cleanup(false);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    std::vector<std::string> disabled_paths;
    for (int i = 0; i < 11; i++) {
        char name[32];
        sprintf(name, "disabled-%d.bin", i);
        disabled_paths.push_back(create_managed_temp_file(env, name, 16));
    }

    set_auto_cleanup(true);
    std::vector<std::string> tracked_paths;
    for (int i = 0; i < 11; i++) {
        char name[32];
        sprintf(name, "tracked-%d.bin", i);
        tracked_paths.push_back(create_managed_temp_file(env, name, 16));
    }

    FileInfo info(64);
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(21, (int)env.filesystem->list_files().size());
    EXPECT_EQ(FR_OK, env.fm->fstat(disabled_paths.front().c_str(), info));
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(tracked_paths.front().c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(tracked_paths.back().c_str(), info));
}

TEST(TempAutoCleanupTest, AutoCleanupEnabledDeletesOldestManagedFile)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    std::vector<std::string> paths;
    for (int i = 0; i < 11; i++) {
        paths.push_back(create_managed_temp_file(env, "upload.bin", 16));
    }

    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(10, (int)env.filesystem->list_files().size());

    FileInfo info(64);
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(paths.front().c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(paths.back().c_str(), info));
}

TEST(TempAutoCleanupTest, MissingManagedEntryDoesNotBlockFutureCleanup)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    std::vector<std::string> paths;
    for (int i = 0; i < 10; i++) {
        char name[32];
        sprintf(name, "stale-%d.bin", i);
        paths.push_back(create_managed_temp_file(env, name, 16));
    }

    env.filesystem->remove_file_without_delete(temp_relative_path(paths.front()));
    create_managed_temp_file(env, "trigger-0.bin", 16);

    FileInfo info(64);
    EXPECT_EQ(0, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(paths.front().c_str(), info));

    create_managed_temp_file(env, "trigger-1.bin", 16);

    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(paths[1].c_str(), info));
}

TEST(TempAutoCleanupTest, CacheSubfolderToggleChangesManagedTempRoot)
{
    set_auto_cleanup(false);
    set_use_cache_subfolder(true);
    {
        TempTestEnvironment env;
        env.reset();
        std::string path = create_managed_temp_file(env, "upload.bin", 8);
        EXPECT_EQ(0, path.find("/Temp/cache/upload/"));
        EXPECT_TRUE(path.find("/Temp/cache/a64/") != 0);
        EXPECT_TRUE(path.find("/Temp/cache/socket/") != 0);
    }

    set_use_cache_subfolder(false);
    {
        TempTestEnvironment env;
        env.reset();
        std::string upload_path = create_managed_temp_file(env, "upload", "upload.bin", 8);
        std::string a64_path = create_managed_temp_file(env, "a64", "demo.prg", 8);
        std::string socket_path = create_managed_temp_file(env, "socket", "socket.d64", 8);
        EXPECT_EQ("/Temp/upload.bin", upload_path);
        EXPECT_EQ("/Temp/demo.prg", a64_path);
        EXPECT_EQ("/Temp/socket.d64", socket_path);
        EXPECT_TRUE(upload_path.find("/Temp/cache/") != 0);
        EXPECT_TRUE(a64_path.find("/Temp/cache/") != 0);
        EXPECT_TRUE(socket_path.find("/Temp/cache/") != 0);
    }
}

TEST(TempAutoCleanupTest, ManagedTempNamesUseClientNameAndHexFallback)
{
    set_auto_cleanup(false);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::string first_upload = create_managed_temp_file(env, "upload", "upload.bin", 8);
    std::string second_upload = create_managed_temp_file(env, "upload", "upload.bin", 8);
    std::string socket_file = create_managed_temp_file(env, "socket", "tcpimage.d64", 8);
    std::string a64_file = create_managed_temp_file(env, "a64", "demo.prg", 8);

    EXPECT_EQ("/Temp/cache/upload/upload.bin", first_upload);
    EXPECT_EQ("/Temp/cache/upload/upload_1.bin", second_upload);
    EXPECT_EQ("/Temp/cache/socket/tcpimage.d64", socket_file);
    EXPECT_EQ("/Temp/cache/a64/demo.prg", a64_file);

    bool saw_hex_letter = false;
    for (int i = 0; i < 20; i++) {
        std::string path = create_managed_temp_file(env, "upload", NULL, 8);
        const std::string prefix = "/Temp/cache/upload/temp";
        ASSERT_EQ(0, path.find(prefix));
        std::string seq = path.substr(prefix.size());
        ASSERT_TRUE(seq.size() >= 4);
        for (size_t j = 0; j < seq.size(); j++) {
            EXPECT_TRUE(((seq[j] >= '0') && (seq[j] <= '9')) || ((seq[j] >= 'a') && (seq[j] <= 'f')));
            if ((seq[j] >= 'a') && (seq[j] <= 'f')) {
                saw_hex_letter = true;
            }
        }
    }
    EXPECT_TRUE(saw_hex_letter);
}

TEST(TempAutoCleanupTest, NonManagedTempFilesAreUnaffected)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    const uint8_t manual_data[] = { 0x01, 0x02, 0x03, 0x04 };
    uint32_t written = 0;
    ASSERT_EQ(FR_OK, env.fm->save_file(true, "/Temp", "keep.bin", (uint8_t *)manual_data, sizeof(manual_data), &written));
    ASSERT_EQ(sizeof(manual_data), written);

    for (int i = 0; i < 11; i++) {
        create_managed_temp_file(env, "upload.bin", 16);
    }

    FileInfo info(64);
    EXPECT_EQ(FR_OK, env.fm->fstat("/Temp/keep.bin", info));
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    for (size_t i = 0; i < env.filesystem->get_deleted_paths().size(); i++) {
        EXPECT_NE(std::string("/keep.bin"), env.filesystem->get_deleted_paths()[i]);
    }
}

TEST(TempAutoCleanupTest, RenameWithinManagedRootKeepsTracking)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::string original_path = create_managed_temp_file(env, "rename.d64", 16);
    const char *renamed_path = "/Temp/cache/upload/renamed.d64";
    ASSERT_EQ(FR_OK, env.fm->rename(original_path.c_str(), renamed_path));

    for (int i = 0; i < 10; i++) {
        create_managed_temp_file(env, "upload.bin", 16);
    }

    FileInfo info(64);
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(renamed_path, info));
}

TEST(TempAutoCleanupTest, RenameOutsideManagedRootPromotesFile)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::string original_path = create_managed_temp_file(env, "promote.d64", 16);
    const char *promoted_path = "/Temp/promoted.d64";
    ASSERT_EQ(FR_OK, env.fm->rename(original_path.c_str(), promoted_path));

    std::vector<std::string> managed_paths;
    for (int i = 0; i < 11; i++) {
        managed_paths.push_back(create_managed_temp_file(env, "upload.bin", 16));
    }

    FileInfo info(64);
    EXPECT_EQ(FR_OK, env.fm->fstat(promoted_path, info));
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(managed_paths.front().c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(managed_paths.back().c_str(), info));
}

TEST(TempAutoCleanupTest, RenameIntoManagedRootDoesNotCreateTracking)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();
    const uint8_t manual_data[] = { 0x01, 0x02, 0x03, 0x04 };
    uint32_t written = 0;
    ASSERT_EQ(FR_OK, env.fm->save_file(true, "/Temp", "manual.bin", (uint8_t *)manual_data, sizeof(manual_data), &written));
    ASSERT_EQ(FR_OK, env.fm->create_dir("/Temp/cache"));
    ASSERT_EQ(FR_OK, env.fm->create_dir("/Temp/cache/upload"));
    ASSERT_EQ(FR_OK, env.fm->rename("/Temp/manual.bin", "/Temp/cache/upload/manual.bin"));

    for (int i = 0; i < 11; i++) {
        create_managed_temp_file(env, "upload.bin", 16);
    }

    FileInfo info(64);
    EXPECT_EQ(FR_OK, env.fm->fstat("/Temp/cache/upload/manual.bin", info));
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
}

TEST(TempAutoCleanupTest, CountCleanupDefersOpenMountedTempUntilBackingHandleCloses)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::string mount_path = create_managed_temp_file(env, "mounted.mnt", 80);
    File *mounted_file = NULL;
    ASSERT_EQ(FR_OK, env.fm->fopen((mount_path + "/mounted.bin").c_str(), FA_READ, &mounted_file));
    ASSERT_TRUE(mounted_file != NULL);
    env.fm->fclose(mounted_file);

    std::vector<std::string> pressure_paths;
    for (int i = 0; i < 11; i++) {
        pressure_paths.push_back(create_managed_temp_file(env, "pressure.bin", 20));
    }

    EXPECT_FALSE(env.filesystem->exists(temp_relative_path(pressure_paths.front())));
    EXPECT_TRUE(env.filesystem->exists(temp_relative_path(pressure_paths.back())));
    EXPECT_TRUE(env.filesystem->exists(temp_relative_path(pressure_paths[9])));
    EXPECT_TRUE(env.filesystem->exists(temp_relative_path(mount_path)));
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());

    env.detach_root();

    EXPECT_FALSE(env.filesystem->exists(temp_relative_path(mount_path)));
    EXPECT_EQ(2, (int)env.filesystem->get_deleted_paths().size());
}

TEST(TempAutoCleanupTest, LowercaseTempRootResolvesCanonicalManagedPath)
{
    set_auto_cleanup(false);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::string path = create_managed_temp_file(env, "case.bin", 8);
    ASSERT_EQ(0, path.find("/Temp/cache/upload/"));

    std::string lower_path = path;
    lower_path.replace(0, 5, "/temp");

    FileInfo info(64);
    EXPECT_EQ(FR_OK, env.fm->fstat(path.c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(lower_path.c_str(), info));
}

TEST(TempAutoCleanupTest, TempClassesShareNewestTenPool)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::vector<std::string> paths;
    paths.push_back(create_managed_temp_file(env, "upload", "first-upload.bin", 8));
    for (int i = 0; i < 4; i++) {
        char name[32];
        sprintf(name, "a64-%d.prg", i);
        paths.push_back(create_managed_temp_file(env, "a64", name, 8));
    }
    for (int i = 0; i < 3; i++) {
        char name[32];
        sprintf(name, "socket-%d.d64", i);
        paths.push_back(create_managed_temp_file(env, "socket", name, 8));
    }
    for (int i = 0; i < 3; i++) {
        char name[32];
        sprintf(name, "upload-%d.bin", i);
        paths.push_back(create_managed_temp_file(env, "upload", name, 8));
    }

    FileInfo info(64);
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(paths.front().c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(paths.back().c_str(), info));
}

TEST(TempAutoCleanupTest, A64RenamedUploadStaysInSharedNewestTenPool)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::vector<std::string> old_paths;
    for (int i = 0; i < 9; i++) {
        char name[32];
        sprintf(name, "old-%d.bin", i);
        old_paths.push_back(create_managed_temp_file(env, "upload", name, 8));
    }

    std::string staged_path = create_managed_temp_file(env, "upload", "download.tmp", 8);
    mstring a64_path;
    ASSERT_EQ(FR_OK, env.fm->get_temp_path("a64", "demo.prg", &a64_path));
    mstring a64_dir;
    ASSERT_EQ(FR_OK, env.fm->ensure_temp_directory("a64", a64_dir));
    ASSERT_EQ(FR_OK, env.fm->rename(staged_path.c_str(), a64_path.c_str()));

    create_managed_temp_file(env, "socket", "socket-0.d64", 8);
    create_managed_temp_file(env, "socket", "socket-1.d64", 8);

    FileInfo info(64);
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(old_paths[0].c_str(), info));
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(old_paths[1].c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(a64_path.c_str(), info));
    EXPECT_EQ(10, (int)env.filesystem->list_files().size());
}

TEST(TempAutoCleanupTest, GroupedFourFileUploadSurvivesNewestTenFloor)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    for (int i = 0; i < 7; i++) {
        char name[32];
        sprintf(name, "old-%d.bin", i);
        create_managed_temp_file(env, "upload", name, 8);
    }

    std::vector<std::string> group_paths;
    for (int i = 0; i < 4; i++) {
        char name[32];
        sprintf(name, "group-%d.bin", i);
        group_paths.push_back(create_managed_temp_file(env, "upload", name, 8));
    }

    FileInfo info(64);
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    for (size_t i = 0; i < group_paths.size(); i++) {
        EXPECT_EQ(FR_OK, env.fm->fstat(group_paths[i].c_str(), info));
    }
}

TEST(TempAutoCleanupTest, ElevenOpenFilesDeleteOldestOnlyOnFinalClose)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    std::vector<File *> files;
    std::vector<std::string> paths;
    for (int i = 0; i < 11; i++) {
        char name[32];
        sprintf(name, "open-%d.bin", i);
        mstring path;
        files.push_back(create_open_managed_temp_file(env, name, path));
        paths.push_back(path.c_str());
    }

    EXPECT_EQ(0, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_TRUE(env.filesystem->exists(temp_relative_path(paths.front())));

    env.fm->fclose(files[1]);
    files[1] = NULL;
    EXPECT_EQ(0, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_TRUE(env.filesystem->exists(temp_relative_path(paths.front())));

    env.fm->fclose(files[0]);
    files[0] = NULL;
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_FALSE(env.filesystem->exists(temp_relative_path(paths.front())));

    for (size_t i = 0; i < files.size(); i++) {
        if (files[i]) {
            env.fm->fclose(files[i]);
        }
    }
}

TEST(TempAutoCleanupTest, GenerateFatNameKeepsLongDirectNames)
{
    const char *boundary_name =
        "zzzz_long_filename_browser_regression_0123456789_0123456789_0123";
    ASSERT_EQ(64, (int)strlen(boundary_name));

    FileInfo boundary_info((int)strlen(boundary_name) + 1);
    strcpy(boundary_info.lfname, boundary_name);
    boundary_info.name_format = NAME_FORMAT_DIRECT;

    char boundary_buffer[65];
    memset(boundary_buffer, 'X', sizeof(boundary_buffer));
    EXPECT_EQ(std::string(boundary_name),
        std::string(boundary_info.generate_fat_name(boundary_buffer, sizeof(boundary_buffer))));
    EXPECT_EQ(0, boundary_buffer[64]);

    const char *long_name =
        "Rambo First Blood Part II NTSC - Thunder Mountain 1985 (EasyLoad64).d64";
    FileInfo info((int)strlen(long_name) + 1);
    strcpy(info.lfname, long_name);
    info.name_format = NAME_FORMAT_DIRECT;

    std::vector<char> full_buffer(strlen(long_name) + 1);
    EXPECT_EQ(std::string(long_name),
        std::string(info.generate_fat_name(full_buffer.data(), full_buffer.size())));

    char truncated[16];
    memset(truncated, 'X', sizeof(truncated));
    info.generate_fat_name(truncated, sizeof(truncated));
    EXPECT_EQ(0, truncated[sizeof(truncated) - 1]);
}

TEST(TempAutoCleanupTest, FileManagerHandlesLongDirectNames)
{
    set_auto_cleanup(true);
    set_use_cache_subfolder(true);

    TempTestEnvironment env;
    env.reset();

    const char *long_name =
        "Rambo First Blood Part II NTSC - Thunder Mountain 1985 (EasyLoad64).d64";
    const char *renamed_name = "lfnok.d64";
    EXPECT_TRUE((int)strlen(long_name) > 64);

    std::string original_path = create_managed_temp_file(env, "upload", long_name, 16);

    FileInfo info(128);
    EXPECT_EQ(FR_OK, env.fm->fstat(original_path.c_str(), info));
    EXPECT_EQ(std::string(long_name), std::string(info.lfname));

    Path *path = env.fm->get_new_path("long-direct-rename");
    path->cd(parent_path(original_path).c_str());
    ASSERT_EQ(FR_OK, env.fm->rename(path, long_name, renamed_name));
    env.fm->release_path(path);

    std::string renamed_path = parent_path(original_path) + "/" + renamed_name;
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(original_path.c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(renamed_path.c_str(), info));
    EXPECT_EQ(std::string(renamed_name), std::string(info.lfname));
}
