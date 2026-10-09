// Real Assembly64, HTTP parser, writer and cache code; only the network address
// and storage medium are adapted to loopback and a private host directory.
#include "assembly.h"
#include "filesystem_a64.h"
#include "node_directfs.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
extern "C" void outbyte(int c) { fputc(c, stdout); }
static int checks, failures;
static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

// These link wrappers keep firmware hostnames and ports out of the test network.
static uint16_t fixture_port;
static int connections;
extern "C" int __real_gethostbyname_r(const char *, struct hostent *, char *, size_t,
                                     struct hostent **, int *);
extern "C" int __wrap_gethostbyname_r(const char *, struct hostent *host, char *buffer,
                                     size_t size, struct hostent **result, int *error)
{
    return __real_gethostbyname_r("127.0.0.1", host, buffer, size, result, error);
}
extern "C" int __real_connect(int, const struct sockaddr *, socklen_t);
extern "C" int __wrap_connect(int fd, const struct sockaddr *, socklen_t)
{
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = fixture_port;
    connections++;
    return __real_connect(fd, (sockaddr *)&address, sizeof(address));
}

class Peer
{
    int listener;
    std::atomic<bool> stop;
    std::thread worker;
public:
    explicit Peer(const std::string &response) : stop(false)
    {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener < 0 || bind(listener, (sockaddr *)&address, sizeof(address)) || listen(listener, 1)) {
            perror("fixture listen");
            exit(2);
        }
        socklen_t size = sizeof(address);
        getsockname(listener, (sockaddr *)&address, &size);
        fixture_port = address.sin_port;
        worker = std::thread([this, response]() {
            pollfd event = { listener, POLLIN, 0 };
            while (!stop) {
                if (poll(&event, 1, 20) <= 0) continue;
                int client = accept(listener, NULL, NULL);
                if (client < 0) return;
                timeval limit = {2, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
                char request[2048];
                recv(client, request, sizeof(request), 0);
                size_t offset = 0;
                while (offset < response.size()) {
                    ssize_t sent = send(client, response.data() + offset, response.size() - offset, MSG_NOSIGNAL);
                    if (sent <= 0) break;
                    offset += sent;
                }
                shutdown(client, SHUT_WR);
                close(client);
                return;
            }
        });
    }
    ~Peer() { stop = true; worker.join(); close(listener); }
};

class HostFile : public File
{
    FILE *stream;
    int &opened;
public:
    HostFile(FileSystem *filesystem, FILE *stream, int &opened)
        : File(filesystem), stream(stream), opened(opened) { opened++; }
    FRESULT close() { ::fclose(stream); opened--; delete this; return FR_OK; }
    FRESULT read(void *data, uint32_t size, uint32_t *done)
    { *done = fread(data, 1, size, stream); return ferror(stream) ? FR_DISK_ERR : FR_OK; }
    FRESULT write(const void *data, uint32_t size, uint32_t *done)
    { *done = fwrite(data, 1, size, stream); return *done == size ? FR_OK : FR_DISK_ERR; }
    uint32_t get_size()
    { long at = ftell(stream); fseek(stream, 0, SEEK_END); long size = ftell(stream); fseek(stream, at, SEEK_SET); return size; }
};

class HostDirectory : public Directory
{
    FileSystem *filesystem;
    fs::directory_iterator entry, end;
public:
    HostDirectory(FileSystem *filesystem, const fs::path &path) : filesystem(filesystem), entry(path) { }
    FRESULT get_entry(FileInfo &info)
    {
        if (entry == end) return FR_NO_FILE;
        info.fs = filesystem;
        info.attrib = entry->is_directory() ? AM_DIR : AM_ARC;
        info.size = entry->is_directory() ? 0 : entry->file_size();
        info.name_format = NAME_FORMAT_DIRECT;
        strncpy(info.lfname, entry->path().filename().c_str(), info.lfsize - 1);
        info.lfname[info.lfsize - 1] = 0;
        info.extension[0] = 0;
        ++entry;
        return FR_OK;
    }
};

class HostStorage : public FileSystem
{
    fs::path root;
    fs::path path(const char *name) { return root / fs::path(name).relative_path(); }
public:
    int opened = 0;
    HostStorage() : FileSystem(NULL)
    {
        char directory[] = "/tmp/assembly-test-XXXXXX";
        char *created = mkdtemp(directory);
        if (!created) { perror("mkdtemp"); exit(2); }
        root = created;
    }
    ~HostStorage() { fs::remove_all(root); }
    bool is_writable() { return true; }
    FRESULT dir_open(const char *name, Directory **out)
    {
        if (!fs::is_directory(path(name))) return FR_NO_PATH;
        *out = new HostDirectory(this, path(name));
        return FR_OK;
    }
    FRESULT dir_create(const char *name)
    { return fs::create_directory(path(name)) ? FR_OK : FR_EXIST; }
    FRESULT file_open(const char *name, uint8_t flags, File **out)
    {
        FILE *stream = ::fopen(path(name).c_str(), flags & FA_WRITE ? "wb" : "rb");
        if (!stream) return FR_NO_FILE;
        *out = new HostFile(this, stream, opened);
        return FR_OK;
    }
    FRESULT file_delete(const char *name) { return fs::remove(path(name)) ? FR_OK : FR_NO_FILE; }
    FRESULT file_rename(const char *from, const char *to)
    { fs::rename(path(from), path(to)); return FR_OK; }
    int files() const
    {
        int count = 0;
        for (const auto &entry : fs::recursive_directory_iterator(root)) count += entry.is_regular_file();
        return count;
    }
    void clear()
    {
        for (const auto &entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                std::string name = "/Temp/" + fs::relative(entry.path(), root).string();
                FileManager::getFileManager()->delete_file(name.c_str());
            }
        }
    }
};

static JSON *query(Assembly &client, int mode)
{
    if (mode == 0) return client.get_presets();
    if (mode == 1) return client.send_query("test");
    return client.request_entries("test", 0);
}

static void test_json()
{
    // Syntactically valid JSON must still be rejected if its HTTP body is short.
    const char *bad[] = {"", "HTTP/1.1 200 OK\r\nContent-Length:",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n"};
    for (int mode = 0; mode < 3; mode++) {
        for (const char *wire : bad) {
            Assembly client;
            { Peer peer(wire);
              JSON *json = query(client, mode);
              check(json == NULL, "incomplete JSON response rejected");
              if (mode != 0) delete json;
              check(client.get_user_context() == NULL, "failed JSON buffer released"); }
            int before = connections;
            { Peer peer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");
              JSON *json = query(client, mode);
              check(json != NULL, "JSON recovery succeeds");
              if (mode != 0) delete json; }
            check(connections == before + 1, "failed JSON did not populate presets cache");
        }
    }
}

static void test_cache(HostStorage &storage)
{
    FileSystemA64 a64;
    const char *bad[] = {"", "HTTP/1.1 200 OK\r\nContent-Length:",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 999\r\nContent-Type: multipart/form-data; boundary=part\r\n\r\n"
        "--part\r\nContent-Disposition: form-data; name=\"one\"; filename=\"one.bin\"\r\n\r\nab\r\n"
        "--part\r\nContent-Disposition: form-data; name=\"two\"; filename=\"two.bin\"\r\n\r\ncd"};
    for (const char *wire : bad) {
        storage.clear();
        for (int repeat = 0; repeat < 2; repeat++) {
            int before = connections;
            Peer peer(wire);
            File *file = NULL;
            FRESULT result = a64.file_open("/test/0/truncated.bin", FA_READ, &file);
            check(result != FR_OK && file == NULL, "incomplete download is not exposed as a file");
            if (file) file->close();
            check(connections == before + 1, "failed download is retried over HTTP");
            check(storage.files() == 0, "no partial upload or cache file remains");
            check(storage.opened == 0, "failed download closes storage handles");
            check(assembly.get_user_context() == NULL, "failed download clears writer ownership");
        }
        int before = connections;
        for (int repeat = 0; repeat < 2; repeat++) {
            Peer peer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabcde");
            File *file = NULL;
            check(a64.file_open("/test/0/truncated.bin", FA_READ, &file) == FR_OK && file,
                  "complete retry and cache hit succeed");
            if (file) {
                char bytes[16] = {};
                uint32_t count = 0;
                check(file->read(bytes, sizeof(bytes), &count) == FR_OK && count == 5 &&
                      memcmp(bytes, "abcde", 5) == 0, "cache contains exactly the complete body");
                file->close();
            }
            check(connections == before + 1, "valid cache hit makes no extra HTTP request");
            check(storage.files() == 1 && storage.opened == 0, "one complete cached file and no open handles");
        }
    }
    storage.clear();
}

static void test_complete_bodies(HostStorage &storage)
{
    FileSystemA64 a64;
    const char *responses[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n3\r\ncde\r\n0\r\n\r\n"
    };
    for (int i = 0; i < 2; i++) {
        storage.clear();
        int before = connections;
        for (int repeat = 0; repeat < 2; repeat++) {
            Peer peer(responses[i]);
            File *file = NULL;
            check(a64.file_open("/test/0/complete.bin", FA_READ, &file) == FR_OK && file,
                  "complete empty/chunked response is readable");
            if (file) {
                char bytes[16] = {};
                uint32_t count = 99;
                check(file->read(bytes, sizeof(bytes), &count) == FR_OK && count == (i ? 5u : 0u) &&
                      (!i || memcmp(bytes, "abcde", 5) == 0), "complete empty/chunked body preserved");
                file->close();
            }
            check(connections == before + 1, "complete empty/chunked cache hit avoids HTTP");
            check(storage.opened == 0, "complete response closes storage handles");
        }
    }
    storage.clear();
}

int main()
{
    HostStorage storage;
    FileManager *fm = FileManager::getFileManager();
    Node_DirectFS *node = new Node_DirectFS(&storage, "Temp", AM_DIR);
    fm->set_temp_auto_cleanup_enabled(false);
    fm->add_root_entry(node);
    test_json();
    test_cache(storage);
    test_complete_bodies(storage);
    fm->remove_root_entry(node);
    delete node;
    printf("Assembly completion: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
