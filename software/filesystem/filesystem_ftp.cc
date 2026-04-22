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

FileSystemFTP::FileSystemFTP(const char *base) : FileSystem(0), client(NULL), connected(false), root_path(base)
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

FRESULT FileOnFTP::close(void)
{
    if (!cached) {
        delete this;
        return FR_INT_ERR;
    }

    FRESULT ret = FR_OK;

    if (needs_upload && remote_path.length() > 0) {
        // Flush and rewind the temp file, then STOR to FTP
        cached->sync();
        uint32_t size = cached->get_size();
        printf("[FTP-FS] Uploading %d bytes to '%s'\n", size, remote_path.c_str());
        cached->seek(0);

        FileSystemFTP *ftpfs = static_cast<FileSystemFTP *>(get_file_system());
        if (ftpfs->connect_if_needed() == 0) {
            if (ftpfs->get_client()->stor(remote_path.c_str(), cached) < 0) {
                printf("[FTP-FS] STOR failed\n");
                ret = FR_DISK_ERR;
            }
        } else {
            ret = FR_DISK_ERR;
        }
    }

    FileManager::getFileManager()->fclose(cached);
    cached = NULL;
    delete this;
    return ret;
}

FRESULT FileSystemFTP::file_open(const char *filename, uint8_t flags, File **file)
{
    printf("[FTP-FS] file_open: '%s' flags=0x%02X\n", filename, flags);
    *file = NULL;

    // Build full FTP path for this file
    mstring ftp_path;
    build_ftp_path(filename, ftp_path);

    // Build a clean filename for /Temp/ cache
    char *fixed = new char[1 + strlen(filename)];
    strcpy(fixed, filename);
    fix_filename(fixed);

    FileManager *fm = FileManager::getFileManager();

    mstring temp_path("/Temp/ftp_");
    temp_path += fixed;
    delete[] fixed;

    bool creating = (flags & FA_CREATE_ANY) != 0;

    if (creating) {
        // Create/open a temp file for creating; upload happens on close()
        File *tmp = NULL;
        flags |= FA_READ; // when writing back to FTP, we need to read it!
        FRESULT fres = fm->fopen(temp_path.c_str(), flags, &tmp);
        if (tmp) {
            *file = new FileOnFTP(this, tmp, ftp_path.c_str(), true);
        }
        return fres;
    }

    // --- Read path: download from FTP if not cached ---
    FileInfo inf(128);
    FRESULT fres = fm->fstat(temp_path.c_str(), inf);
    printf("[FTP-FS] cache '%s': %s\n", temp_path.c_str(), fres == FR_OK ? "HIT" : "MISS");

    if (fres == FR_NO_FILE) {
        if (connect_if_needed() < 0) {
            return FR_DISK_ERR;
        }

        File *tmp = NULL;
        fres = fm->fopen(temp_path.c_str(), FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &tmp);
        if (tmp) {
            int bytes_read = 0;
            int ret = client->retr(ftp_path.c_str(), tmp, &bytes_read);
            fm->fclose(tmp);

            if (ret < 0 || bytes_read <= 0) {
                printf("[FTP-FS] RETR '%s' failed\n", ftp_path.c_str());
                return FR_DISK_ERR;
            }
            printf("[FTP-FS] Downloaded %d bytes\n", bytes_read);
            fres = FR_OK;
        }

        if (fres != FR_OK) {
            printf("[FTP-FS] Failed to save temp file: %d\n", fres);
            return fres;
        }
    }

    // Open the cached file for reading
    File *temp = NULL;
    flags &= ~FA_CREATE_ANY;
    flags |= FA_READ; // when writing back to FTP, we need to read it!

    fres = fm->fopen(temp_path.c_str(), flags, &temp);
    if (temp) {
        *file = new FileOnFTP(this, temp, ftp_path.c_str(), false);
    }
    return fres;
}

FRESULT FileSystemFTP::dir_open(const char *path, Directory **dir)
{
    printf("[FTP-FS] dir_open('%s')\n", path ? path : "(null)");

	DirectoryOnFTP *newdir = new DirectoryOnFTP(this);
	if (!path) {
	    path = "/";
	}

	FRESULT res = newdir->list(path);

	if (res == FR_OK) {
		*dir = newdir;
	} else {
		delete newdir;
	}
	return res;
}

/*************************************************************/
/* DirectoryOnFTP                                            */
/*************************************************************/

FRESULT DirectoryOnFTP::get_entry(FileInfo &info)
{
    if (index >= children.get_elements()) {
        return FR_NO_FILE;
    }

    FileInfo *child = children[index++];
    info.copyfrom(child);
    return FR_OK;
}

/********************/
/* FTP LIST Parse   */
/********************/

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

FRESULT DirectoryOnFTP::list(const char *ftp_path)
{
    if (ftpfs->connect_if_needed() < 0) {
        printf("[FTP-FS] Not connected, cannot list '%s'\n", ftp_path);
        return FR_NO_FILESYSTEM;
    }

    static const int kListBufSize = 16384;
    char *buf = new char[kListBufSize];
    if (!buf) {
        return FR_NOT_ENOUGH_CORE;
    }

    int bytes_read = 0;
    FTPClient *client = ftpfs->get_client();
    if (client->list(ftp_path, buf, kListBufSize, &bytes_read) < 0) {
        printf("[FTP-FS] LIST '%s' failed\n", ftp_path);
        delete[] buf;
        return FR_NO_PATH;
    }

    printf("[FTP-FS] LIST '%s': %d bytes\n", ftp_path, bytes_read);

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
                FileInfo *inf = new FileInfo(name);
                inf->attrib = (is_dir) ? AM_DIR : 0;
                inf->size   = size;
                inf->fs     = ftpfs;
                get_extension(name, inf->extension, true);
                children.append(inf);
            }
        }
        line = next;
    }

    delete[] buf;
    printf("[FTP-FS] '%s': %d entries\n", ftp_path, children.get_elements());
    return FR_OK;
}

/*************************************************************/
/* Writable filesystem operations                            */
/*************************************************************/

// Build the full FTP server path from a local filesystem path.
// Local paths look like "subdir/file.d64"; we prepend the root's base path.
void FileSystemFTP::build_ftp_path(const char *local_path, mstring &out)
{
    out = root_path;
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
    return FR_OK;
}

/*************************************************************/
/* Registration                                              */
/*************************************************************/

static FileSystemFTP *ftp_fs_instance = NULL;

void add_ftp_to_root(void *_context, void *_param)
{
    FTPRootNode *node = new FTPRootNode();
    node->load_servers();
    FileManager::getFileManager()->add_root_entry(node);
    printf("[FTP-FS] FTP root node added.\n");
}

InitFunction init_ftp_fs("FTP Filesystem", add_ftp_to_root, NULL, NULL, 31);

#define CFG_FILEPATH "/flash/config"
#define FTP_SERVERS  "ftp_servers"

void FTPRootNode :: load_servers()
{
    File *fi;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(CFG_FILEPATH, FTP_SERVERS, FA_READ, &fi);
    if (fres == FR_OK) {
        load_servers_impl(fi);
        fm->fclose(fi);
    }
}

void FTPRootNode :: load_servers_impl(File *f)
{
    uint32_t size = f->get_size();
    if ((size > 12288) || (size < 8)) { // max 12K partition paths file
        return;
    }
    char *buffer = new char[size+1];
    char *linebuf = new char[256];
    char *name;

    uint32_t transferred;
    f->read(buffer, size, &transferred);
    buffer[transferred] = 0;

    uint32_t index = 0;

    const char *words[6];
    while(index < size) {
        index = read_line(buffer, index, linebuf, 256);
        int len = strlen(linebuf);
        bool trigger = false;
        if (len > 0) {
            words[0] = linebuf;
            int w = 1;
            for(int i=0; i<len; i++) {
                if ((linebuf[i] == ' ') || (linebuf[i] == '\t')) {
                    linebuf[i] = 0;
                    trigger = true;
                } else if(trigger) {
                    trigger = false;
                    if (w <= 5) {
                        words[w++] = linebuf + i;
                    }
                }
            }
            children.append(new FTPServer(this, words[0], words[1], words[2], words[3], words[4], words[5]));
        }
    }
    delete[] linebuf;
    delete[] buffer;
}
