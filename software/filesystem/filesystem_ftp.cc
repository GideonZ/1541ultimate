/*
 * filesystem_ftp.cc
 *
 * FTP remote filesystem — mounts an FTP server as a browsable
 * directory in the file tree. Files are downloaded to /Temp/ on
 * first access (lazy caching).
 */

#include "filesystem_ftp.h"
#include "filemanager.h"
#include "node_directfs.h"
#include "network_config.h"
#include "init_function.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*************************************************************/
/* FileSystemFTP                                             */
/*************************************************************/
uint32_t FileOnFTP::node_count = 0;

FileSystemFTP::FileSystemFTP() : FileSystem(0), client(NULL), connected(false), root_node(NULL)
{
    client = new FTPClient();
}

FileSystemFTP::~FileSystemFTP()
{
    if (client) {
        client->disconnect();
        delete client;
    }
}

int FileSystemFTP::connect_if_needed()
{
    if (connected && client->is_connected()) {
        return 0;
    }
    connected = false;

    ConfigStore *cfg = networkConfig.cfg;
    if (!cfg) {
        return -1;
    }

    const char *host = cfg->get_string(CFG_NETWORK_FTP_CLIENT_HOST);
    const char *port_str = cfg->get_string(CFG_NETWORK_FTP_CLIENT_PORT);
    const char *user = cfg->get_string(CFG_NETWORK_FTP_CLIENT_USER);
    const char *pass = cfg->get_string(CFG_NETWORK_FTP_CLIENT_PASS);

    if (!host || !host[0]) {
        printf("[FTP-FS] No server configured\n");
        return -1;
    }

    uint16_t port = (uint16_t)strtol(port_str ? port_str : "21", NULL, 10);
    if (port == 0) {
        port = 21;
    }

    printf("[FTP-FS] Connecting to %s:%d as '%s'\n", host, port, user ? user : "anonymous");

    if (client->open(host, port) < 0) {
        printf("[FTP-FS] Connection failed\n");
        return -1;
    }

    if (client->login(user ? user : "anonymous", pass ? pass : "") < 0) {
        printf("[FTP-FS] Login failed\n");
        client->disconnect();
        return -1;
    }

    client->type_binary();

    const char *base = cfg->get_string(CFG_NETWORK_FTP_CLIENT_PATH);
    if (base && base[0] && strcmp(base, "/") != 0) {
        if (client->cwd(base) < 0) {
            printf("[FTP-FS] CWD '%s' failed\n", base);
        }
    }

    connected = true;
    printf("[FTP-FS] Connected OK\n");
    return 0;
}

void FileSystemFTP::drop_connection()
{
    if (client) {
        client->disconnect();
    }
    connected = false;
}

FRESULT FileSystemFTP::file_open(const char *filename, uint8_t flags, File **file)
{
    printf("[FTP-FS] file_open: '%s'\n", filename);
    *file = NULL;

    // Build a clean filename for /Temp/ cache
    char *fixed = new char[1 + strlen(filename)];
    strcpy(fixed, filename);
    fix_filename(fixed);

    FileManager *fm = FileManager::getFileManager();

    // Check temp cache
    mstring temp_path("/Temp/ftp_");
    temp_path += fixed;
    delete[] fixed;

    FileInfo inf(128);
    FRESULT fres = fm->fstat(temp_path.c_str(), inf);
    printf("[FTP-FS] cache '%s': %s\n", temp_path.c_str(), fres == FR_OK ? "HIT" : "MISS");

    if (fres == FR_NO_FILE) {
        // Download from FTP
        if (connect_if_needed() < 0) {
            return FR_DISK_ERR;
        }

        // Allocate download buffer (max 4MB)
        static const int kMaxFileSize = 4 * 1024 * 1024;
        uint8_t *buf = new uint8_t[kMaxFileSize];
        if (!buf) {
            printf("[FTP-FS] Out of memory for download buffer\n");
            return FR_NOT_ENOUGH_CORE;
        }

        // Build full FTP path: base_path + filename
        mstring full_path(root_node ? root_node->get_ftp_path() : "");
        if (full_path.length() > 0 && full_path.c_str()[full_path.length() - 1] == '/') {
            // base has trailing slash, skip leading slash in filename
            if (filename[0] == '/') filename++;
        }
        full_path += filename;

        int bytes_read = 0;
        int ret = client->retr(full_path.c_str(), buf, kMaxFileSize, &bytes_read);
        if (ret < 0 || bytes_read <= 0) {
            printf("[FTP-FS] RETR '%s' failed\n", full_path.c_str());
            delete[] buf;
            return FR_DISK_ERR;
        }

        printf("[FTP-FS] Downloaded %d bytes\n", bytes_read);

        // Save to temp
        File *tmp = NULL;
        fres = fm->fopen(temp_path.c_str(), FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &tmp);
        if (tmp) {
            uint32_t written;
            tmp->write(buf, bytes_read, &written);
            fm->fclose(tmp);
            fres = FR_OK;
        }
        delete[] buf;

        if (fres != FR_OK) {
            printf("[FTP-FS] Failed to save temp file: %d\n", fres);
            return fres;
        }
    }

    // Open the cached file
    File *temp = NULL;
    fres = fm->fopen(temp_path.c_str(), flags, &temp);
    if (temp) {
        *file = new FileOnFTP(this, temp);
    }
    return fres;
}

FTPNode *FileSystemFTP::find_node(const char *path)
{
    if (!root_node) {
        return NULL;
    }

    // Empty path or "/" means root
    if (!path || !path[0] || (path[0] == '/' && !path[1])) {
        return root_node;
    }

    // Walk the FTPNode tree element by element
    CachedTreeNode *node = root_node;
    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        // Extract next path element
        const char *end = strchr(p, '/');
        int len = end ? (int)(end - p) : (int)strlen(p);
        if (len == 0) {
            p++;
            continue;
        }

        char element[128];
        if (len >= (int)sizeof(element)) len = sizeof(element) - 1;
        memcpy(element, p, len);
        element[len] = '\0';

        // Make sure children are populated
        node->fetch_children();
        CachedTreeNode *child = node->find_child(element);
        if (!child) {
            return NULL;
        }
        node = child;

        p += len;
        if (*p == '/') p++;
    }

    // Only return if it's actually an FTPNode (directory)
    if (!(node->get_file_info()->attrib & AM_DIR)) {
        return NULL;
    }
    return static_cast<FTPNode *>(node);
}

FRESULT FileSystemFTP::dir_open(const char *path, Directory **dir)
{
    printf("[FTP-FS] dir_open('%s')\n", path ? path : "(null)");

    FTPNode *node = find_node(path);
    if (!node) {
        printf("[FTP-FS] dir_open: node not found for '%s'\n", path ? path : "(null)");
        return FR_NO_PATH;
    }

    // Populate children via FTP LIST
    node->fetch_children();

    *dir = new DirectoryOnFTP(node);
    return FR_OK;
}

PathStatus_t FileSystemFTP::walk_path(PathInfo &pathInfo)
{
    // Walk through the FTPNode tree to resolve path elements
    CachedTreeNode *node = root_node;

    while (pathInfo.hasMore()) {
        const char *element = pathInfo.workPath.getElement(pathInfo.index);

        if (node) {
            node->fetch_children();
            CachedTreeNode *child = node->find_child(element);
            if (child) {
                node = child;
            } else {
                node = NULL; // Remaining elements are unresolved (could be a file)
            }
        }

        FileInfo *inf = pathInfo.getNewInfoPointer();
        strncpy(inf->lfname, element, inf->lfsize);
        inf->fs = this;

        if (node) {
            inf->attrib = node->get_file_info()->attrib;
            inf->size = node->get_file_info()->size;
            memcpy(inf->extension, node->get_file_info()->extension, 4);
        } else {
            inf->attrib = 0;
            get_extension(element, inf->extension, true);
        }

        pathInfo.index++;
    }

    return e_EntryFound;
}

/*************************************************************/
/* DirectoryOnFTP                                            */
/*************************************************************/

FRESULT DirectoryOnFTP::get_entry(FileInfo &info)
{
    // Make sure children are populated
    node->fetch_children();

    if (index >= node->children.get_elements()) {
        return FR_NO_FILE;
    }

    CachedTreeNode *child = node->children[index++];
    info.copyfrom(child->get_file_info());
    return FR_OK;
}

/*************************************************************/
/* FTPNode — CachedTreeNode that populates from FTP LIST     */
/*************************************************************/

FTPNode::FTPNode(FileSystemFTP *fs, CachedTreeNode *parent, const char *name, const char *remote_path, uint8_t attrib)
    : CachedTreeNode(parent, name), ftpfs(fs), ftp_path(remote_path)
{
    info.attrib = attrib;
    info.fs = fs;
}

FTPNode::~FTPNode()
{
}

static bool parse_ftp_list_line(const char *line, char *name, int namesize, uint32_t *size, bool *is_dir)
{
    *is_dir = false;
    *size = 0;
    name[0] = '\0';

    if (!line || !line[0]) {
        return false;
    }

    // Unix-style: "drwxr-xr-x  2 user group  4096 Jan 01 12:00 dirname"
    if (line[0] == 'd' || line[0] == '-' || line[0] == 'l') {
        *is_dir = (line[0] == 'd');

        // Find the filename — it's after the date/time field.
        // Skip permissions, links, owner, group, size, month, day, time/year
        // Strategy: scan from the right for the name after the last time/year field
        const char *p = line;
        int fields = 0;
        while (*p && fields < 8) {
            // skip whitespace
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (!*p) {
                break;
            }
            // Parse size from field 4 (0-indexed)
            if (fields == 4) {
                *size = (uint32_t)strtoul(p, NULL, 10);
            }
            // skip field content
            while (*p && *p != ' ' && *p != '\t') {
                p++;
            }
            fields++;
        }
        // skip whitespace before filename
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p) {
            // For symlinks, strip " -> target"
            const char *arrow = strstr(p, " -> ");
            int len = arrow ? (int)(arrow - p) : (int)strlen(p);
            // Trim trailing \r\n
            while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) {
                len--;
            }
            if (len >= namesize) {
                len = namesize - 1;
            }
            memcpy(name, p, len);
            name[len] = '\0';
            return len > 0;
        }
        return false;
    }

    // Windows-style: "01-01-24  12:00PM  <DIR>  dirname" or "01-01-24  12:00PM  12345 filename"
    if (line[0] >= '0' && line[0] <= '9') {
        const char *p = line;
        // Skip date
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        // Skip time
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        // <DIR> or size
        if (strncmp(p, "<DIR>", 5) == 0) {
            *is_dir = true;
            p += 5;
        } else {
            *size = (uint32_t)strtoul(p, NULL, 10);
        }
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        // Remainder is filename
        int len = (int)strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) {
            len--;
        }
        if (len >= namesize) {
            len = namesize - 1;
        }
        memcpy(name, p, len);
        name[len] = '\0';
        return len > 0;
    }

    return false;
}

int FTPNode::fetch_children()
{
    if (children.get_elements() > 0) {
        return children.get_elements();
    }

    if (ftpfs->connect_if_needed() < 0) {
        printf("[FTP-FS] Not connected, cannot list '%s'\n", ftp_path.c_str());
        return 0;
    }

    static const int kListBufSize = 16384;
    char *buf = new char[kListBufSize];
    if (!buf) {
        return 0;
    }

    int bytes_read = 0;
    FTPClient *client = ftpfs->get_client();
    if (client->list(ftp_path.c_str(), buf, kListBufSize, &bytes_read) < 0) {
        printf("[FTP-FS] LIST '%s' failed\n", ftp_path.c_str());
        delete[] buf;
        return 0;
    }

    printf("[FTP-FS] LIST '%s': %d bytes\n", ftp_path.c_str(), bytes_read);

    // Parse line by line
    char name[128];
    uint32_t size;
    bool is_dir;
    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        if (parse_ftp_list_line(line, name, sizeof(name), &size, &is_dir)) {
            // Skip . and ..
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                if (is_dir) {
                    mstring child_path(ftp_path);
                    if (child_path.length() > 0 && child_path.c_str()[child_path.length() - 1] != '/') {
                        child_path += "/";
                    }
                    child_path += name;
                    FTPNode *child = new FTPNode(ftpfs, this, name, child_path.c_str(), AM_DIR);
                    children.append(child);
                } else {
                    // Regular file node
                    CachedTreeNode *child = new CachedTreeNode(this, name);
                    child->get_file_info()->attrib = 0;
                    child->get_file_info()->size = size;
                    child->get_file_info()->fs = ftpfs;
                    get_extension(name, child->get_file_info()->extension, true);
                    children.append(child);
                }
            }
        }
        line = next;
    }

    delete[] buf;
    sort_children();
    printf("[FTP-FS] '%s': %d entries\n", ftp_path.c_str(), children.get_elements());
    return children.get_elements();
}

void FTPNode::invalidate(void)
{
    // Delete all children so next fetch_children() re-reads from server
    int n = children.get_elements();
    for (int i = 0; i < n; i++) {
        delete children[i];
    }
    children.clear_list();
}

/*************************************************************/
/* Writable filesystem operations                            */
/*************************************************************/

// Build the full FTP server path from a local filesystem path.
// Local paths look like "subdir/file.d64"; we prepend the root's base path.
void FileSystemFTP::build_ftp_path(const char *local_path, mstring &out)
{
    out = root_node ? root_node->get_ftp_path() : "/";
    if (out.length() > 0 && out.c_str()[out.length() - 1] != '/') {
        out += "/";
    }
    if (local_path && local_path[0] == '/') {
        local_path++;
    }
    if (local_path) {
        out += local_path;
    }
}

// Find the FTPNode for the parent directory of a path.
FTPNode *FileSystemFTP::find_parent_node(const char *path)
{
    if (!path || !path[0]) {
        return root_node;
    }

    // Find last '/' (skip trailing slash)
    int len = (int)strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    int last_sep = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_sep = i;
            break;
        }
    }

    if (last_sep <= 0) {
        return root_node;
    }

    // Extract parent path using substring constructor
    mstring parent_path(path, 0, last_sep);
    return find_node(parent_path.c_str());
}

// Invalidate the cached children of the parent directory.
void FileSystemFTP::invalidate_parent(const char *path)
{
    FTPNode *parent = find_parent_node(path);
    if (parent) {
        parent->invalidate();
    }
}

FRESULT FileSystemFTP::dir_create(const char *path)
{
    printf("[FTP-FS] dir_create('%s')\n", path);

    if (connect_if_needed() < 0) {
        return FR_DISK_ERR;
    }

    mstring ftp_path;
    build_ftp_path(path, ftp_path);

    if (client->mkd(ftp_path.c_str()) < 0) {
        printf("[FTP-FS] MKD '%s' failed\n", ftp_path.c_str());
        return FR_DENIED;
    }

    invalidate_parent(path);
    return FR_OK;
}

FRESULT FileSystemFTP::file_delete(const char *path)
{
    printf("[FTP-FS] file_delete('%s')\n", path);

    if (connect_if_needed() < 0) {
        return FR_DISK_ERR;
    }

    mstring ftp_path;
    build_ftp_path(path, ftp_path);

    // Try file delete first, then directory delete
    if (client->dele(ftp_path.c_str()) < 0) {
        if (client->rmd(ftp_path.c_str()) < 0) {
            printf("[FTP-FS] DELE/RMD '%s' failed\n", ftp_path.c_str());
            return FR_DENIED;
        }
    }

    invalidate_parent(path);
    return FR_OK;
}

FRESULT FileSystemFTP::file_rename(const char *old_name, const char *new_name)
{
    printf("[FTP-FS] file_rename('%s' -> '%s')\n", old_name, new_name);

    if (connect_if_needed() < 0) {
        return FR_DISK_ERR;
    }

    mstring old_ftp, new_ftp;
    build_ftp_path(old_name, old_ftp);
    build_ftp_path(new_name, new_ftp);

    if (client->rnfr_rnto(old_ftp.c_str(), new_ftp.c_str()) < 0) {
        printf("[FTP-FS] RNFR/RNTO failed\n");
        return FR_DENIED;
    }

    invalidate_parent(old_name);
    // If renamed across directories, invalidate destination too
    invalidate_parent(new_name);
    return FR_OK;
}

/*************************************************************/
/* Registration                                              */
/*************************************************************/

static FileSystemFTP *ftp_fs_instance = NULL;

void add_ftp_to_root(void *_context, void *_param)
{
    ConfigStore *cfg = networkConfig.cfg;
    if (!cfg || !cfg->get_value(CFG_NETWORK_FTP_CLIENT_EN)) {
        return;
    }

    const char *base_path = cfg->get_string(CFG_NETWORK_FTP_CLIENT_PATH);
    if (!base_path || !base_path[0]) {
        base_path = "/";
    }

    ftp_fs_instance = new FileSystemFTP();
    FTPNode *node = new FTPNode(ftp_fs_instance, NULL, "FTP", base_path, AM_DIR);
    ftp_fs_instance->set_root_node(node);
    FileManager::getFileManager()->add_root_entry(node);
    printf("[FTP-FS] FTP root node added (base: %s)\n", base_path);
}

InitFunction init_ftp_fs("FTP Filesystem", add_ftp_to_root, NULL, NULL, 31);
