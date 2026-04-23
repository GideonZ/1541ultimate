#include "../../io/usb/tests/host_test/host_test.h"
#include "../filemanager.h"
#include "../node_directfs.h"
#include "../../filesystem/embedded_fs.h"

#include <map>
#include <set>
#include <strings.h>
#include <string>
#include <vector>

namespace {

bool g_auto_cleanup = true;
bool g_use_cache_subfolder = true;

std::string normalize_path(const char *path)
{
    if (!path || !path[0]) {
        return "/";
    }

    std::string normalized(path);
    for (size_t i = 0; i < normalized.size(); i++) {
        if (normalized[i] == '\\') {
            normalized[i] = '/';
        }
    }
    if (normalized.empty() || (normalized[0] != '/')) {
        normalized.insert(normalized.begin(), '/');
    }
    while ((normalized.size() > 1) && (normalized.back() == '/')) {
        normalized.pop_back();
    }
    return normalized.empty() ? "/" : normalized;
}

std::string parent_path(const std::string &path)
{
    if (path == "/") {
        return "/";
    }
    size_t slash = path.find_last_of('/');
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string basename_path(const std::string &path)
{
    if (path == "/") {
        return "/";
    }
    size_t slash = path.find_last_of('/');
    return path.substr(slash + 1);
}

bool ends_with_ci(const char *text, const char *suffix)
{
    if (!text || !suffix) {
        return false;
    }
    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }
    return strncasecmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

struct FakeFileRecord {
    uint32_t inode;
    uint32_t size;
};

class FakeTempFileSystem;

class FakeTempFile : public File
{
    FakeTempFileSystem *filesystem;
    std::string path;
    uint32_t offset;
public:
    FakeTempFile(FakeTempFileSystem *fs, const std::string &path);
    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
    uint32_t get_size(void);
    uint32_t get_inode(void);
};

class FakeMountedFile : public File
{
public:
    explicit FakeMountedFile(FileSystem *fs) : File(fs) { }
    FRESULT close(void)
    {
        delete this;
        return FR_OK;
    }
};

class FakeDirectory : public Directory
{
public:
    struct Entry {
        std::string name;
        bool is_dir;
        uint32_t size;
    };

private:
    FileSystem *filesystem;
    std::vector<Entry> entries;
    size_t index;
public:
    FakeDirectory(FileSystem *fs, const std::vector<Entry> &entries) : filesystem(fs), entries(entries), index(0) { }

    FRESULT get_entry(FileInfo &info)
    {
        if (index >= entries.size()) {
            return FR_NO_FILE;
        }
        const Entry &entry = entries[index++];
        info.fs = filesystem;
        info.cluster = 0;
        info.size = entry.size;
        info.attrib = entry.is_dir ? AM_DIR : AM_ARC;
        info.name_format = NAME_FORMAT_DIRECT;
        strncpy(info.lfname, entry.name.c_str(), info.lfsize - 1);
        info.lfname[info.lfsize - 1] = 0;
        info.extension[0] = 0;
        return FR_OK;
    }
};

class FakeTempFileSystem : public FileSystem
{
    uint32_t next_inode;
    std::set<std::string> directories;
    std::map<std::string, FakeFileRecord> files;
    std::vector<std::string> deleted_paths;
public:
    FakeTempFileSystem() : FileSystem(NULL), next_inode(1)
    {
        directories.insert("/");
    }

    void reset(void)
    {
        directories.clear();
        directories.insert("/");
        files.clear();
        deleted_paths.clear();
    }

    bool is_writable() { return true; }

    FRESULT dir_open(const char *path, Directory **out)
    {
        std::string dir = normalize_path(path);
        if (!directories.count(dir)) {
            return FR_NO_PATH;
        }

        std::vector<FakeDirectory::Entry> entries;
        std::set<std::string> seen;
        const std::string prefix = (dir == "/") ? "/" : dir + "/";

        for (std::set<std::string>::const_iterator it = directories.begin(); it != directories.end(); ++it) {
            if (*it == dir) {
                continue;
            }
            if (it->find(prefix) != 0) {
                continue;
            }
            std::string remainder = it->substr(prefix.size());
            if (remainder.empty() || (remainder.find('/') != std::string::npos)) {
                continue;
            }
            if (seen.insert(remainder).second) {
                entries.push_back({ remainder, true, 0 });
            }
        }
        for (std::map<std::string, FakeFileRecord>::const_iterator it = files.begin(); it != files.end(); ++it) {
            if (it->first.find(prefix) != 0) {
                continue;
            }
            std::string remainder = it->first.substr(prefix.size());
            if (remainder.empty() || (remainder.find('/') != std::string::npos)) {
                continue;
            }
            if (seen.insert(remainder).second) {
                entries.push_back({ remainder, false, it->second.size });
            }
        }
        *out = new FakeDirectory(this, entries);
        return FR_OK;
    }

    FRESULT dir_create(const char *path)
    {
        std::string dir = normalize_path(path);
        if (directories.count(dir)) {
            return FR_EXIST;
        }
        if (!directories.count(parent_path(dir))) {
            return FR_NO_PATH;
        }
        directories.insert(dir);
        return FR_OK;
    }

    FRESULT file_open(const char *filename, uint8_t flags, File **out)
    {
        std::string path = normalize_path(filename);
        const bool exists = files.find(path) != files.end();
        const bool create_new = (flags & FA_CREATE_NEW) != 0;
        const bool create_always = (flags & FA_CREATE_ALWAYS) != 0;
        const bool open_always = (flags & FA_OPEN_ALWAYS) != 0;
        const bool create = (create_new && !exists) || create_always || (open_always && !exists);

        if (!directories.count(parent_path(path))) {
            return FR_NO_PATH;
        }
        if (create_new && exists) {
            return FR_EXIST;
        }
        if (!exists && !create) {
            return FR_NO_FILE;
        }

        if (!exists) {
            files[path] = { next_inode++, 0 };
        } else if (create_always) {
            files[path].size = 0;
        }

        *out = new FakeTempFile(this, path);
        return FR_OK;
    }

    FRESULT file_rename(const char *old_name, const char *new_name)
    {
        std::string from = normalize_path(old_name);
        std::string to = normalize_path(new_name);
        std::map<std::string, FakeFileRecord>::iterator it = files.find(from);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        if (!directories.count(parent_path(to))) {
            return FR_NO_PATH;
        }
        if (files.find(to) != files.end()) {
            return FR_EXIST;
        }
        files[to] = it->second;
        files.erase(it);
        return FR_OK;
    }

    FRESULT file_delete(const char *path)
    {
        std::string normalized = normalize_path(path);
        std::map<std::string, FakeFileRecord>::iterator it = files.find(normalized);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        deleted_paths.push_back(normalized);
        files.erase(it);
        return FR_OK;
    }

    FRESULT write_file(const std::string &path, uint32_t offset, uint32_t len, uint32_t *transferred)
    {
        std::map<std::string, FakeFileRecord>::iterator it = files.find(path);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        const uint32_t end = offset + len;
        if (end > it->second.size) {
            it->second.size = end;
        }
        if (transferred) {
            *transferred = len;
        }
        return FR_OK;
    }

    bool seek_file(const std::string &path)
    {
        return files.find(path) != files.end();
    }

    uint32_t get_file_size(const std::string &path) const
    {
        std::map<std::string, FakeFileRecord>::const_iterator it = files.find(path);
        return (it == files.end()) ? 0 : it->second.size;
    }

    uint32_t get_file_inode(const std::string &path) const
    {
        std::map<std::string, FakeFileRecord>::const_iterator it = files.find(path);
        return (it == files.end()) ? 0 : it->second.inode;
    }

    bool exists(const std::string &path) const
    {
        return files.find(normalize_path(path.c_str())) != files.end();
    }

    const std::vector<std::string> &get_deleted_paths() const
    {
        return deleted_paths;
    }

    std::vector<std::string> list_files() const
    {
        std::vector<std::string> paths;
        for (std::map<std::string, FakeFileRecord>::const_iterator it = files.begin(); it != files.end(); ++it) {
            paths.push_back(it->first);
        }
        return paths;
    }
};

class FakeMountedFileSystem : public FileSystem
{
public:
    FakeMountedFileSystem() : FileSystem(NULL) { }

    FRESULT dir_open(const char *path, Directory **out)
    {
        std::string normalized = normalize_path(path);
        if (normalized != "/") {
            return FR_NO_PATH;
        }
        std::vector<FakeDirectory::Entry> entries;
        entries.push_back({ "mounted.bin", false, 0 });
        *out = new FakeDirectory(this, entries);
        return FR_OK;
    }

    FRESULT file_open(const char *filename, uint8_t flags, File **out)
    {
        (void)flags;
        if (normalize_path(filename) != "/mounted.bin") {
            return FR_NO_FILE;
        }
        *out = new FakeMountedFile(this);
        return FR_OK;
    }
};

class FakeEmbeddedFsInFile : public FileSystemInFile
{
    FakeMountedFileSystem filesystem;
public:
    void init(File *f)
    {
        (void)f;
    }

    FileSystem *getFileSystem()
    {
        return &filesystem;
    }

    static FileSystemInFile *test_type(FileInfo *inf)
    {
        if (inf && ends_with_ci(inf->lfname, ".mnt")) {
            return new FakeEmbeddedFsInFile();
        }
        return NULL;
    }
};

FactoryRegistrator<FileInfo *, FileSystemInFile *> g_fake_embedded_fs(
        FileSystemInFile::getEmbeddedFileSystemFactory(), FakeEmbeddedFsInFile::test_type);

FakeTempFile::FakeTempFile(FakeTempFileSystem *fs, const std::string &path) : File(fs), filesystem(fs), path(path), offset(0)
{
}

FRESULT FakeTempFile::close(void)
{
    delete this;
    return FR_OK;
}

FRESULT FakeTempFile::read(void *buffer, uint32_t len, uint32_t *transferred)
{
    (void)buffer;
    (void)len;
    if (transferred) {
        *transferred = 0;
    }
    return FR_OK;
}

FRESULT FakeTempFile::write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    (void)buffer;
    FRESULT fres = filesystem->write_file(path, offset, len, transferred);
    if (fres == FR_OK) {
        offset += len;
    }
    return fres;
}

FRESULT FakeTempFile::seek(uint32_t pos)
{
    if (!filesystem->seek_file(path)) {
        return FR_NO_FILE;
    }
    offset = pos;
    return FR_OK;
}

uint32_t FakeTempFile::get_size(void)
{
    return filesystem->get_file_size(path);
}

uint32_t FakeTempFile::get_inode(void)
{
    return filesystem->get_file_inode(path);
}

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

std::string create_managed_temp_file(TempTestEnvironment &env, const char *suggested_name, uint32_t size)
{
    File *file = NULL;
    mstring path;
    ASSERT_EQ(FR_OK, env.fm->create_temp_file(TempUpload, suggested_name, FA_WRITE | FA_CREATE_ALWAYS, &file, &path));
    ASSERT_TRUE(file != NULL);
    std::vector<uint8_t> data(size, 0xAA);
    uint32_t written = 0;
    ASSERT_EQ(FR_OK, file->write(data.data(), size, &written));
    ASSERT_EQ(size, written);
    env.fm->fclose(file);
    return path.c_str();
}

std::string temp_relative_path(const std::string &absolute_path)
{
    if (absolute_path.find("/Temp") == 0) {
        return absolute_path.substr(5);
    }
    return absolute_path;
}

} // namespace

bool user_if_temp_auto_cleanup_enabled(void)
{
    return g_auto_cleanup;
}

bool user_if_temp_use_cache_subfolder_enabled(void)
{
    return g_use_cache_subfolder;
}

TEST(TempAutoCleanupTest, AutoCleanupDisabledKeepsManagedFiles)
{
    g_auto_cleanup = false;
    g_use_cache_subfolder = true;

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

TEST(TempAutoCleanupTest, AutoCleanupEnabledDeletesOldestManagedFile)
{
    g_auto_cleanup = true;
    g_use_cache_subfolder = true;

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

TEST(TempAutoCleanupTest, CacheSubfolderToggleChangesManagedTempRoot)
{
    g_auto_cleanup = false;
    g_use_cache_subfolder = true;
    {
        TempTestEnvironment env;
        env.reset();
        std::string path = create_managed_temp_file(env, "upload.bin", 8);
        EXPECT_EQ(0, path.find("/Temp/cache/"));
        EXPECT_TRUE(path.find("/Temp/cache/a64/") != 0);
        EXPECT_TRUE(path.find("/Temp/cache/socket/") != 0);
    }

    g_use_cache_subfolder = false;
    {
        TempTestEnvironment env;
        env.reset();
        std::string path = create_managed_temp_file(env, "upload.bin", 8);
        EXPECT_EQ(0, path.find("/Temp/"));
        EXPECT_TRUE(path.find("/Temp/cache/") != 0);
    }
}

TEST(TempAutoCleanupTest, ManagedTempNamesUseDecimalSequence)
{
    g_auto_cleanup = false;
    g_use_cache_subfolder = true;

    TempTestEnvironment env;
    env.reset();

    for (int i = 0; i < 16; i++) {
        std::string path = create_managed_temp_file(env, "upload.bin", 8);
        const std::string prefix = "/Temp/cache/temp";
        const std::string suffix = "_upload.bin";

        ASSERT_EQ(0, path.find(prefix));
        ASSERT_TRUE(path.size() > (prefix.size() + suffix.size()));
        ASSERT_EQ(path.size() - suffix.size(), path.rfind(suffix));

        std::string seq = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
        ASSERT_TRUE(!seq.empty());
        for (size_t j = 0; j < seq.size(); j++) {
            EXPECT_TRUE((seq[j] >= '0') && (seq[j] <= '9'));
        }
    }
}

TEST(TempAutoCleanupTest, NonManagedTempFilesAreUnaffected)
{
    g_auto_cleanup = true;
    g_use_cache_subfolder = true;

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

TEST(TempAutoCleanupTest, SuspendedManagedTempEvictsAfterRestore)
{
    g_auto_cleanup = true;
    g_use_cache_subfolder = true;

    TempTestEnvironment env;
    env.reset();

    std::string suspended_path = create_managed_temp_file(env, "mounted.d64", 16);
    env.fm->suspend_managed_temp(suspended_path.c_str());

    std::vector<std::string> managed_paths;
    for (int i = 0; i < 11; i++) {
        managed_paths.push_back(create_managed_temp_file(env, "upload.bin", 16));
    }

    FileInfo info(64);
    EXPECT_EQ(1, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_OK, env.fm->fstat(suspended_path.c_str(), info));
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(managed_paths.front().c_str(), info));
    EXPECT_EQ(FR_OK, env.fm->fstat(managed_paths.back().c_str(), info));

    env.fm->restore_managed_temp(suspended_path.c_str());

    EXPECT_EQ(2, (int)env.filesystem->get_deleted_paths().size());
    EXPECT_EQ(FR_NO_FILE, env.fm->fstat(suspended_path.c_str(), info));
}

TEST(TempAutoCleanupTest, CountCleanupDefersOpenMountedTempUntilBackingHandleCloses)
{
    g_auto_cleanup = true;
    g_use_cache_subfolder = true;

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
