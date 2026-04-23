#ifndef FILESYSTEM_FTP_H
#define FILESYSTEM_FTP_H

#include "file_system.h"
#include "filemanager.h"
#include "filetypes.h"
#include "cached_tree_node.h"
#include "browsable_root.h"
#include "ftp_client.h"
#include <stdlib.h>

class FTPRootNode; // forward

class BrowsableFTPRoot : public BrowsableDirEntry
{
    static SubsysResultCode_e new_host(SubsysCommand *cmd) { printf("hoi\n"); }
    Path *path;
public:
    BrowsableFTPRoot(Path *p, Browsable *parent, FileInfo *inf): path(p), BrowsableDirEntry(p, parent, inf, true) {}
    virtual ~BrowsableFTPRoot() {}

    void getDisplayString(char *buffer, int width, int sq) {
        sprintf(buffer, "%8s%#s \e\x0d%s", "Ftp", width-18, "Remote FTP Servers", "Ready");
    }

    void fetch_context_items(IndexedList<Action *>&items) {
        BrowsableDirEntry::fetch_context_items(items);
        items.append(new Action("New Host", new_host, 0, 0));
    }
};

class FTPRootNode : public CachedTreeNode, WithBrowsableRootEntry
{
    void load_servers_impl(File *);
    Path path;
public:
    FTPRootNode() : CachedTreeNode(NULL, "ftp"), path("") {
        info.fs = NULL;
        info.cluster = 0; // indicate root dir
        info.attrib = AM_DIR | AM_HID; // ;-)
        info.name_format = NAME_FORMAT_DIRECT; // causes FileSystem::get_display_string to be called instead of on BrowsableDirEntry
    }
    void load_servers();

    int probe() {
        return children.get_elements();
    }

    Browsable *create_browsable(Browsable *parent) {
        return new BrowsableFTPRoot(&path, parent, new FileInfo(info));
    }

    void get_display_string(char *buffer, int width) {
        sprintf(buffer, "huh");
    }
};

class FTPServer : public CachedTreeNode
{
public:
    mstring  alias;
    mstring  host;
    uint16_t port;
    mstring  user;
    mstring  passw;
    mstring  folder;

    FTPServer(CachedTreeNode *par, const char *alias, const char *host, const char *port_str,
              const char *user, const char *passw, const char *folder);
};

class FileSystemFTP : public FileSystem
{
    FTPClient *client;
    FTPServer *server;
    bool connected;

    void build_ftp_path(const char *local_path, mstring &out);
    void invalidate_parent(const char *path);
public:
    FileSystemFTP(FTPServer *serv);
    ~FileSystemFTP();

    FTPClient *get_client() { return client; }
    int connect_if_needed();
    void drop_connection();

    bool is_writable() { return true; }

    FRESULT dir_open(const char *path, Directory **);
    FRESULT dir_create(const char *path);
    FRESULT file_open(const char *filename, uint8_t flags, File **);
    FRESULT file_rename(const char *old_name, const char *new_name);
    FRESULT file_delete(const char *path);
//    PathStatus_t walk_path(PathInfo &pathInfo);
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
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) {
        if (cached) {
            FRESULT fres = cached->write(buffer, len, transferred);
            if (fres == FR_OK) { // write successful
                needs_upload = true;
            }
            return fres;
        } else {
            return FR_INT_ERR;
        }
    }
    FRESULT seek(uint32_t pos) { return cached ? cached->seek(pos) : FR_INT_ERR; }
    uint32_t get_size(void) { return cached ? cached->get_size() : 0; }
    uint32_t get_inode(void) { return inode; }
};

class DirectoryOnFTP : public Directory
{
    int index;
    FileSystemFTP *ftpfs;
    IndexedList<FileInfo *> children;

public:
    DirectoryOnFTP(FileSystemFTP *fs) : ftpfs(fs), index(0), children(16, NULL) { }
    ~DirectoryOnFTP() {
        for(int i=0;i<children.get_elements();i++) {
            FileInfo *inf = children[i];
            if (inf) {
                delete inf;
            }
        }
    }

    FRESULT list(const char *path);
    FRESULT get_entry(FileInfo &info);
};


#endif
