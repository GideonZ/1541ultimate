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

// Test settings live on the FileManager directly via setters; these helpers
// keep the existing tests terse.
void set_auto_cleanup(bool enabled)
{
    FileManager::getFileManager()->set_temp_auto_cleanup_enabled(enabled);
}

void set_use_cache_subfolder(bool enabled)
{
    FileManager::getFileManager()->set_temp_use_cache_subfolder_enabled(enabled);
}

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
    std::map<std::string, FRESULT> delete_failures;
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
        delete_failures.clear();
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
        std::map<std::string, FRESULT>::iterator fail = delete_failures.find(normalized);
        if (fail != delete_failures.end()) {
            FRESULT fres = fail->second;
            delete_failures.erase(fail);
            return fres;
        }
        std::map<std::string, FakeFileRecord>::iterator it = files.find(normalized);
        if (it == files.end()) {
            return FR_NO_FILE;
        }
        deleted_paths.push_back(normalized);
        files.erase(it);
        return FR_OK;
    }

    void remove_file_without_delete(const std::string &path)
    {
        files.erase(normalize_path(path.c_str()));
    }

    void fail_delete_once(const std::string &path, FRESULT fres)
    {
        delete_failures[normalize_path(path.c_str())] = fres;
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
