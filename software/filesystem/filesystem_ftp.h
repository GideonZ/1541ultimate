#ifndef FILESYSTEM_FTP_H
#define FILESYSTEM_FTP_H

#include "file_system.h"
#include "filemanager.h"
#include "cached_tree_node.h"
#include "ftp_client.h"

class FileSystemFTP : public FileSystem
{
    FTPClient *client;
    bool connected;

    int ensure_connected();
public:
    FileSystemFTP();
    ~FileSystemFTP();

    FTPClient *get_client() { return client; }
    int connect_if_needed();
    void drop_connection();

    FRESULT file_open(const char *filename, uint8_t flags, File **);
    PathStatus_t walk_path(PathInfo &pathInfo);
};

class FileOnFTP : public File
{
    File *cached;
    uint32_t inode;
    static uint32_t node_count;

public:
    FileOnFTP(FileSystem *fs, File *wrapped) : File(fs), cached(wrapped)
    {
        node_count++;
        inode = node_count;
    }
    ~FileOnFTP() {}

    FRESULT close(void)
    {
        if (cached) {
            FileManager::getFileManager()->fclose(cached);
            cached = NULL;
            delete this;
            return FR_OK;
        }
        delete this;
        return FR_INT_ERR;
    }

    FRESULT sync(void) { return cached ? cached->sync() : FR_INT_ERR; }
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) { return cached ? cached->read(buffer, len, transferred) : FR_INT_ERR; }
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) { return cached ? cached->write(buffer, len, transferred) : FR_INT_ERR; }
    FRESULT seek(uint32_t pos) { return cached ? cached->seek(pos) : FR_INT_ERR; }
    uint32_t get_size(void) { return cached ? cached->get_size() : 0; }
    uint32_t get_inode(void) { return inode; }
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

    const char *get_ftp_path() { return ftp_path.c_str(); }
};

#endif
