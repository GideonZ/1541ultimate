#ifndef FILEMANAGER_TESTS_FAKE_FILESYSTEM_H
#define FILEMANAGER_TESTS_FAKE_FILESYSTEM_H

// In-memory FileSystem/File/Directory fakes shared by the filemanager host
// tests. Kept in an anonymous namespace so each test translation unit gets its
// own independent copy of the fake state.

#include "../filemanager.h"
#include "../node_directfs.h"
#include "../../filesystem/embedded_fs.h"
#include "../../components/pattern.h"

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
        // CBM file type ("PRG", "SEQ", ...) for entries that model a D64/T64
        // style filesystem, where the type is metadata and is not part of the
        // filename. Left empty for FAT style entries, whose extension is
        // derived from the filename instead.
        std::string cbm_type;
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
        if (!entry.cbm_type.empty()) {
            // D64/T64 style: the type is metadata, the name does not carry it.
            strncpy(info.extension, entry.cbm_type.c_str(), 4);
            info.extension[3] = 0;
        } else {
            // FAT style: the extension is derived from the name itself, and is
            // truncated to three characters (see filesystem_fat.h).
            get_extension(info.lfname, info.extension, true);
        }
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
    std::map<std::string, std::string> cbm_types;
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
                std::map<std::string, std::string>::const_iterator type = cbm_types.find(it->first);
                entries.push_back({ remainder, false, it->second.size,
                                    (type == cbm_types.end()) ? std::string() : type->second });
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

    // Marks a file as carrying a CBM file type, so that this filesystem reports
    // its extension the way a D64/T64 filesystem does: as metadata that is not
    // part of the filename.
    void set_cbm_type(const std::string &path, const std::string &type)
    {
        cbm_types[normalize_path(path.c_str())] = type;
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
} // namespace

#endif // FILEMANAGER_TESTS_FAKE_FILESYSTEM_H
