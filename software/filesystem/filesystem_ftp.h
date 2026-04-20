#ifndef FILESYSTEM_FTP_H
#define FILESYSTEM_FTP_H

#include "file_system.h"
#include "filemanager.h"
#include "cached_tree_node.h"
#include "ftp_client.h"

class FTPNode; // forward

class FileSystemFTP : public FileSystem
{
    FTPClient *client;
    bool connected;
    FTPNode *root_node;

    FTPNode *find_node(const char *path);
    FTPNode *find_parent_node(const char *path);
    void build_ftp_path(const char *local_path, mstring &out);
    void invalidate_parent(const char *path);
public:
    FileSystemFTP();
    ~FileSystemFTP();

    FTPClient *get_client() { return client; }
    int connect_if_needed();
    void drop_connection();
    void set_root_node(FTPNode *n) { root_node = n; }

    bool is_writable() { return true; }

    FRESULT dir_open(const char *path, Directory **);
    FRESULT dir_create(const char *path);
    FRESULT file_open(const char *filename, uint8_t flags, File **);
    FRESULT file_rename(const char *old_name, const char *new_name);
    FRESULT file_delete(const char *path);
    PathStatus_t walk_path(PathInfo &pathInfo);
};

class FileOnFTP : public File
{
    File *cached;
    uint32_t inode;
    static uint32_t node_count;
    bool needs_upload;          // STOR on close?
    mstring remote_path;        // full FTP path for upload

public:
    FileOnFTP(FileSystem *fs, File *wrapped, const char *ftp_path = NULL, bool upload = false)
        : File(fs), cached(wrapped), needs_upload(upload), remote_path(ftp_path ? ftp_path : "")
    {
        node_count++;
        inode = node_count;
    }
    ~FileOnFTP() {}

    FRESULT close(void);

    FRESULT sync(void) { return cached ? cached->sync() : FR_INT_ERR; }
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) { return cached ? cached->read(buffer, len, transferred) : FR_INT_ERR; }
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) { return cached ? cached->write(buffer, len, transferred) : FR_INT_ERR; }
    FRESULT seek(uint32_t pos) { return cached ? cached->seek(pos) : FR_INT_ERR; }
    uint32_t get_size(void) { return cached ? cached->get_size() : 0; }
    uint32_t get_inode(void) { return inode; }
};

class DirectoryOnFTP : public Directory
{
    FTPNode *node;
    int index;
public:
    DirectoryOnFTP(FTPNode *n) : node(n), index(0) { }
    ~DirectoryOnFTP() { }

    FRESULT get_entry(FileInfo &info);
};

class FTPNode : public CachedTreeNode
{
    FileSystemFTP *ftpfs;
    mstring ftp_path; // absolute path on FTP server

public:
    FTPNode(FileSystemFTP *fs, CachedTreeNode *parent, const char *name, const char *remote_path, uint8_t attrib = AM_DIR);
    ~FTPNode();

    bool is_ready(void) { return true; }
    int probe(void) { return 1; }
    int fetch_children(void);
    void invalidate(void);

    const char *get_ftp_path() { return ftp_path.c_str(); }
};

#endif
