#include <stdio.h>
#include "path.h"
#include "filemanager.h"
#include "embedded_fs.h"
#include "file_device.h"
#include <cctype>

void FileManager::invalidate(CachedTreeNode *o)
{
    MountPoint *m;
    File *f;
    mstring pathString;
    const char *pathStringC;
    int len;

    pathStringC = o->get_full_path(pathString);
    len = strlen(pathStringC);
    printf("Invalidate event on %s.. Checking %d mountpoints.\n", o->get_file_info()->lfname,
            mount_points.get_elements());

    for (int i = 0; i < mount_points.get_elements(); i++) {
        m = mount_points[i];
        if (!m) { // should never occur
            printf("INTERR: null pointer in mount point list.\n");
            continue;
        }
        f = m->get_file();
        printf("%2d. %s\n", i, f->get_path());
        if (strncmp(f->get_path(), pathStringC, len) == 0) {
            printf("Match!\n");
            fclose(f);
            delete m;
            mount_points.mark_for_removal(i);
        }
    }
    mount_points.purge_list();

    printf("Invalidate event on %s.. Checking %d files.\n", o->get_file_info()->lfname, open_file_list.get_elements());
    for (int i = 0; i < open_file_list.get_elements(); i++) {
        f = open_file_list[i];
        if (!f) { // should never occur
            printf("INTERR: null pointer in list.\n");
            continue;
        }
        else if (!(f->isValid())) { // already invalidated.
            continue;
        }
        printf("%2d. %s\n", i, f->get_path());
        if (strncmp(f->get_path(), pathStringC, len) == 0) {
            printf("Match!\n");
            f->invalidate();
        }
    }

    // if the node is in root, the remove_root_node shall be called.
    o->cleanup_children();
    printf("Invalidation complete.\n");
}

void FileManager::remove_from_parent(CachedTreeNode *o)
{
    CachedTreeNode *par;
    par = o->parent;
    if (par && (par != root)) {
        printf("Removing %s from %s\n", o->get_name(), par->get_name());
        par->children.remove(o);
    }
}

void FileManager::get_display_string(Path *p, const char *filename, char *buffer, int width)
{
    CachedTreeNode *n = root;

    for (int i = 0; i < p->getDepth(); i++) {
        n = n->find_child(p->getElement(i));
        if (!n) {
            strncpy(buffer, "Invalid path", width);
            return;
        }
    }
    n = n->find_child(filename);
    if (!n) {
        strncpy(buffer, "Invalid entry", width);
        return;
    }
    n->get_display_string(buffer, width);
}


FRESULT FileManager::find_pathentry(PathInfo &pathInfo, bool open_mount)
{
    // pass complete path to the file system's "walk path" function. This function
    // attempts to search through its directory entries. There are various cases:

    // * Entry not found. No match, return status only
    // * Entry not found. Last entry is a valid directory. This is a valid situation for file create.
    // * Entry found. Return: FileInfo structure of entry. This might be an invalid situation when existing files should not get overwritten.
    // * Search terminated. Entry is a file, but there are more entries in the path. Return file info, and remaining path
    // * Search terminated. Entry is a mount point, and the remaining path should be searched in the resulting file system. Return file system and remaining path.

    // printf("FileManager :: find_pathentry('%s', %d)\n", filepath, open_mount);

    bool ready = false;
    FileSystem *fs = pathInfo.getLastInfo()->fs;
    MountPoint *mp;
    mstring pathFromFSRoot;
    mstring dirFromFSRoot;

    FRESULT fres = FR_OK;
    while (!ready) {
        ready = true;
        PathStatus_t ps = fs->walk_path(pathInfo);
        switch (ps) {
            case e_DirNotFound:
                fres = FR_NO_PATH;
                break;
            case e_EntryNotFound:
                fres = FR_NO_FILE;
                break;
            case e_EntryFound:
                fres = FR_OK;
                // we end on a file, and we're requested to force open it
                if (open_mount && !(pathInfo.last->attrib & AM_DIR)) {
                    mp = find_mount_point(pathInfo.getSubPath(), pathInfo.last);
                    if (mp) {
                        fs = mp->get_embedded()->getFileSystem();
                        pathInfo.enterFileSystem(fs);
                    }
                    else {
                        fres = FR_NO_FILESYSTEM;
                    }
                }
                break;
            case e_TerminatedOnFile:
                mp = find_mount_point(pathInfo.getSubPath(), pathInfo.last);
                if (mp) {
                    fs = mp->get_embedded()->getFileSystem();
                    pathInfo.enterFileSystem(fs);
                    ready = false;
                }
                else {
                    fres = FR_NO_FILESYSTEM;
                }
                break;
            case e_TerminatedOnMount:
                fs = pathInfo.last->fs;
                pathInfo.enterFileSystem(fs);
                ready = !pathInfo.hasMore();
                break;
            default:
                break;
        }
    }

    return fres;
}

FRESULT FileManager::fopen_impl(PathInfo &pathInfo, uint8_t flags, File **file)
{
    // Reset output file pointer
    *file = NULL;

    FRESULT fres = find_pathentry(pathInfo, false);

    if (fres == FR_NO_PATH)
        return fres;

    if ((fres == FR_OK) && (flags & FA_CREATE_NEW)) {
        return FR_EXIST;
    }

    bool create = (flags & FA_CREATE_NEW) && (fres == FR_NO_FILE);
    create |= (flags & FA_CREATE_ALWAYS) && (fres == FR_OK);
    create |= (flags & FA_CREATE_ALWAYS) && (fres == FR_NO_FILE);
    create |= (flags & FA_OPEN_ALWAYS) && (fres == FR_NO_FILE);

    // If the file should not be created, it should exist
    if ((fres != FR_OK) && (!create))
        return fres;

    FileSystem *fs = pathInfo.getLastInfo()->fs;
    FileInfo *relativeDir = 0;
    mstring workpath;

    fres = fs->file_open(pathInfo.getPathFromLastFS(), flags, file);
    if (fres == FR_OK) {
        open_file_list.append(*file);
        pathInfo.workPath.getTail(0, (*file)->get_path_reference());
        if (create) {
            const char *pathstring = pathInfo.getFullPath(workpath, -1);
            sendEventToObservers(eRefreshDirectory, pathstring, "");
        }
    }
    return fres;
}

FRESULT FileManager::delete_file_impl(PathInfo &pathInfo)
{
    FRESULT fres = find_pathentry(pathInfo, false);
    if (fres != FR_OK) {
        return fres;
    }
    FileSystem *fs = pathInfo.getLastInfo()->fs;
    fres = fs->file_delete(pathInfo.getPathFromLastFS());
    if (fres == FR_OK) {
        mstring work;
        pathInfo.workPath.getHead(work);
        sendEventToObservers(eNodeRemoved, pathInfo.workPath.getSub(0, pathInfo.index - 1, work),
                pathInfo.getFileName());
    }
    return fres;
}

FRESULT FileManager::priv_get_free(Path *path, uint32_t &free, uint32_t &cluster_size)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path);
    FRESULT fres = find_pathentry(pathInfo, true);
    if (fres != FR_OK) {
        return fres;
    }
    FileInfo *inf = pathInfo.getLastInfo();
    if (!inf || !(inf->fs)) {
        return FR_NO_FILESYSTEM;
    }
    fres = inf->fs->get_free(&free, &cluster_size);
    return fres;
}

FRESULT FileManager::priv_fs_read_sector(Path *path, uint8_t *buffer, int track, int sector)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path);
    FRESULT fres = find_pathentry(pathInfo, true);
    if (fres != FR_OK) {
        return fres;
    }
    FileInfo *inf = pathInfo.getLastInfo();
    if (!inf || !(inf->fs)) {
        return FR_NO_FILESYSTEM;
    }
    fres = inf->fs->read_sector(buffer, track, sector);
    return fres;
}

FRESULT FileManager::priv_fs_write_sector(Path *path, uint8_t *buffer, int track, int sector)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path);
    FRESULT fres = find_pathentry(pathInfo, true);
    if (fres != FR_OK) {
        return fres;
    }
    FileInfo *inf = pathInfo.getLastInfo();
    if (!inf || !(inf->fs)) {
        return FR_NO_FILESYSTEM;
    }
    fres = inf->fs->write_sector(buffer, track, sector);
    return fres;
}


void FileManager::add_root_entry(CachedTreeNode *obj)
{
    printf("Adding %s to root\n", obj->get_name());
    root->children.append(obj);
    obj->parent = root;
    sendEventToObservers(eNodeAdded, "/", obj->get_name());
}

void FileManager::remove_root_entry(CachedTreeNode *obj)
{
    invalidate(obj); // cleanup all file references that are dependent on this node
    remove_from_parent(obj);
    sendEventToObservers(eNodeRemoved, "/", obj->get_name());
    root->children.remove(obj);
}

MountPoint *FileManager::add_mount_point(SubPath *path, File *file, FileSystemInFile *emb)
{
    printf("FileManager :: add_mount_point: (FS=%p, path='%s')\n", file->get_file_system(), path->get_path());
    MountPoint *mp = new MountPoint(path, file, emb);
    mount_points.append(mp);
    return mp;
}

MountPoint *FileManager::find_mount_point(SubPath *path, FileInfo *info)
{
    //printf("FileManager :: find_mount_point: '%s' (FS=%p, Path='%s')\n", info->lfname, info->fs, path->get_path());
    for (int i = 0; i < mount_points.get_elements(); i++) {
        if (mount_points[i]->match(info->fs, path)) {
            //printf("Found!\n");
            return mount_points[i];
        }
    }

    File *file;
    FileSystemInFile *emb = 0;
    MountPoint *mp = 0;

    uint8_t flags = (info->attrib & AM_RDO) ? FA_READ : FA_READ | FA_WRITE;

    FRESULT fr = info->fs->file_open(path->get_path(), flags, &file);
    if (fr == FR_OK) {
        path->get_root_path(file->get_path_reference());
        emb = FileSystemInFile::getEmbeddedFileSystemFactory()->create(info);
        if (emb) {
            emb->init(file);
            if (emb->getFileSystem()) {
                mp = add_mount_point(path, file, emb);
            }
            else {
                delete emb;
            }
        }
    } else {
        printf("FileManager -> can't open file %s to create mountpoint.%s\n", path->get_path(), FileSystem :: get_error_string(fr));
    }
    return mp;
}

FRESULT FileManager::rename_impl(PathInfo &from, PathInfo &to)
{
    FRESULT fres = find_pathentry(from, false);
    if (fres != FR_OK) {
        return fres;
    }

    // source file was found
    fres = find_pathentry(to, false);
    if (fres == FR_NO_PATH) {
        return fres;
    }

    if (fres == FR_NO_FILE) { // we MAY be able to do this..
        if (to.getLastInfo()->fs != from.getLastInfo()->fs) {
            printf("Trying to move a file from one file system to another.\n");
            return FR_INVALID_DRIVE;
        }
        fres = from.getLastInfo()->fs->file_rename(from.getPathFromLastFS(), to.getPathFromLastFS());
        if (fres == FR_OK) {
            mstring work1, work2;
            const char *from_path = from.getFullPath(work1, -1);
            const char *to_path = to.getFullPath(work2, -1);
            sendEventToObservers(eRefreshDirectory, from_path, "");
            if (strcmp(from_path, to_path) != 0) {
                sendEventToObservers(eRefreshDirectory, to_path, "");
            }
        }
        return fres;
    }

    if (fres == FR_OK)
        return FR_EXIST;
    return fres;
}

const char *FileManager::eventStrings[] = {
        "eRefreshDirectory",  // Contents of directory have changed
        "eNodeAdded",         // New Node
        "eNodeRemoved",       // Node no longer exists (deleted)
        "eNodeMediaRemoved",  // Node lost all its children
        "eNodeUpdated",       // Node status changed (= redraw line)
        "eChangeDirectory",   // Request to change current directory within observer task
        };

FRESULT FileManager::priv_stat(Path *path_obj, const char *path, const char *filename, FileInfo &info, bool open_mount)
{
    printf("FileManager::priv_stat: '%s' '%s' '%s'\n", (path_obj)?path_obj->get_path():"null", path, filename);
    PathInfo pathInfo(rootfs);
    pathInfo.init(path_obj, path, filename);

    FRESULT fres = find_pathentry(pathInfo, open_mount);

    if (fres != FR_OK) {
        return fres;
    }
    info.copyfrom(pathInfo.getLastInfo());
    printf("Priv stat file system of last: %p (rootfs:%p)\n", info.fs, rootfs);
    return FR_OK;
}

FRESULT FileManager::priv_open(Path *path_obj, const char *path, const char *filename, uint8_t flags, File **file)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path_obj, path, filename);
    // now do the actual thing
    FRESULT res = fopen_impl(pathInfo, flags, file);
    return res;
}

FRESULT FileManager::priv_close(File *f)
{
    if (f->get_file_system()) {
        // printf("Closing %s...\n", inf->lfname);
    }
    else {
        printf("ERR: Closing invalidated file.\n");
    }
    open_file_list.remove(f);
    return f->close();
}

FRESULT FileManager::priv_sync(File *f)
{
    return f->sync();
}

FRESULT FileManager::priv_delete(Path *path, const char *pathname, const char *filename)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path, pathname, filename);
    return delete_file_impl(pathInfo);
}

FRESULT FileManager::priv_create_dir(Path *path, const char *pathname, const char *filename)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path, pathname, filename);

    FRESULT fres = find_pathentry(pathInfo, false);
    if (fres == FR_OK) {
        return FR_EXIST;
    }
    if (fres == FR_OK) {
        return FR_EXIST;
    }
    if (fres == FR_NO_FILE) {
        FileSystem *fs = pathInfo.getLastInfo()->fs;
        fres = fs->dir_create(pathInfo.getPathFromLastFS());
        if (fres == FR_OK) {
            mstring work;
            sendEventToObservers(eNodeAdded, pathInfo.getFullPath(work, -1), pathInfo.getFileName());
        }
    }
    return fres;
}

FRESULT FileManager::priv_open_dir(const char *pathname, Directory **dir)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(pathname);
    printf("FileManager::priv_open_dir: '%s'\n", pathname);
    FRESULT res = find_pathentry(pathInfo, true);
    if (res != FR_OK) {
        return res;
    }
    FileSystem *fs = pathInfo.getLastInfo()->fs;
    printf("FileSystem: %p\n", fs);
    res = fs->dir_open(pathInfo.getPathFromLastFS(), dir);
    printf("res = %s, dir: %p\n", FileSystem::get_error_string(res), *dir);
    return res;
}

FRESULT FileManager::priv_read_dir(Directory *dir, FileInfo &info)
{
    return dir->get_entry(info);
}

FRESULT FileManager::priv_close_dir(Directory *dir)
{
    delete dir;
    return FR_OK;
}

FRESULT FileManager::priv_rename(const char *old_name, const char *new_name)
{
    PathInfo pathInfoFrom(rootfs);
    pathInfoFrom.init(old_name);
    PathInfo pathInfoTo(rootfs);
    pathInfoTo.init(new_name);
    return rename_impl(pathInfoFrom, pathInfoTo);
}

FRESULT FileManager::priv_read(File *fp, uint8_t *buffer, uint32_t size, uint32_t *transferred)
{
    return fp->read(buffer, size, transferred);
}

FRESULT FileManager::priv_write(File *fp, uint8_t *buffer, uint32_t size, uint32_t *transferred)
{
    return fp->write(buffer, size, transferred);  
}

FRESULT FileManager::priv_seek(File *fp, uint32_t offset)
{
    return fp->seek(offset);
}

FRESULT FileManager::priv_is_path_writable(Path *p)
{
    FileManager *fm = FileManager::getFileManager();
    PathInfo pathInfo(rootfs);
    pathInfo.init(p);
    FRESULT res = find_pathentry(pathInfo, false);
    if (res != FR_OK) {
        return res;
    }
    if (pathInfo.getLastInfo()->is_writable()) {
        return FR_OK;
    }
    return FR_DENIED;
}

/// New FileManager Service wrappers; these are the only functions that should be called from outside the FileManager class.
#define SendCommand(cmd) \
    cmd.caller = xTaskGetCurrentTaskHandle(); \
    FileCommand *ptr = &cmd; \
    xQueueSend(fm->fileCommandQueue, &ptr, portMAX_DELAY); \
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY)
    
FRESULT FileManager::fopen(Path *path, const char *filename, uint8_t flags, File **file)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_open(path, nullptr, filename, flags, file);
    }

    FileCommand cmd;
    cmd.type = FileOpType::OPEN;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = filename;
    cmd.flags = flags;
    cmd.file_ptr = file;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fopen(const char *path, const char *filename, uint8_t flags, File **file)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_open(nullptr, path, filename, flags, file);
    }
    FileCommand cmd;
    cmd.type = FileOpType::OPEN;
    cmd.path_obj = nullptr;
    cmd.path = path;
    cmd.filename = filename;
    cmd.flags = flags;
    cmd.file_ptr = file;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fopen(const char *pathname, uint8_t flags, File **file)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_open(nullptr, pathname, nullptr, flags, file);
    }
    FileCommand cmd;
    cmd.type = FileOpType::OPEN;
    cmd.path_obj = nullptr;
    cmd.path = nullptr;
    cmd.filename = pathname;
    cmd.flags = flags;
    cmd.file_ptr = file;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::read(File *fp, void *buffer, const uint32_t size, uint32_t *transferred)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_read(fp, (uint8_t *)buffer, size, transferred);
    }
    FileCommand cmd;
    cmd.type = FileOpType::READ;
    cmd.file_ptr = &fp;
    cmd.buffer = (uint8_t *)buffer;
    cmd.size = size;
    cmd.transferred = transferred;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::write(File *fp, void *buffer, const uint32_t size, uint32_t *transferred)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_write(fp, (uint8_t *)buffer, size, transferred);
    }
    FileCommand cmd;
    cmd.type = FileOpType::WRITE;
    cmd.file_ptr = &fp;
    cmd.buffer = (uint8_t *)buffer;
    cmd.size = size;
    cmd.transferred = transferred;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::seek(File *fp, uint32_t offset)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_seek(fp, offset);
    }
    FileCommand cmd;
    cmd.type = FileOpType::SEEK;
    cmd.file_ptr = &fp;
    cmd.size = offset;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::sync(File* f)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_sync(f);
    }
    FileCommand cmd;
    cmd.type = FileOpType::SYNC;
    cmd.file_ptr = &f;
    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fclose(File* f)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_close(f);
    }
    FileCommand cmd;
    cmd.type = FileOpType::CLOSE;
    cmd.file_ptr = &f;
    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fstat(Path *path, const char *filename, FileInfo &info, bool open_mount)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_stat(path, nullptr, filename, info, open_mount);
    }
    FileCommand cmd;
    cmd.type = FileOpType::FSTAT;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = filename;
    cmd.file_info = &info;
    cmd.open_mount = open_mount;

    SendCommand(cmd);
    printf("fstat result: %d\n", cmd.result);
    return cmd.result;
}

FRESULT FileManager::fstat(const char *path, const char *filename, FileInfo &info)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_stat(nullptr, path, filename, info, false);
    }
    FileCommand cmd;
    cmd.type = FileOpType::FSTAT;
    cmd.path_obj = nullptr;
    cmd.path = path;
    cmd.filename = filename;
    cmd.file_info = &info;
    cmd.open_mount = false;

    SendCommand(cmd);
    printf("fstat result: %d\n", cmd.result);
    return cmd.result;
}

FRESULT FileManager::fstat(const char *pathname, FileInfo &info)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_stat(nullptr, pathname, nullptr, info, false);
    }
    FileCommand cmd;
    cmd.type = FileOpType::FSTAT;
    cmd.path_obj = nullptr;
    cmd.path = nullptr;
    cmd.filename = pathname;
    cmd.file_info = &info;
    cmd.open_mount = false;

    SendCommand(cmd);
    printf("fstat result: %d\n", cmd.result);
    return cmd.result;
}

FRESULT FileManager::delete_file(Path *path, const char *name)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_delete(path, nullptr, name);
    }
    FileCommand cmd;
    cmd.type = FileOpType::DELETE;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = name;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::delete_file(const char *pathname)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_delete(nullptr, pathname, nullptr);
    }
    FileCommand cmd;
    cmd.type = FileOpType::DELETE;
    cmd.path_obj = nullptr;
    cmd.path = pathname;
    cmd.filename = nullptr;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::create_dir(const char *pathname)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_create_dir(nullptr, pathname, nullptr);
    }
    FileCommand cmd;
    cmd.type = FileOpType::CREATE_DIR;
    cmd.path_obj = nullptr;
    cmd.path = pathname;
    cmd.filename = nullptr;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::create_dir(Path *path, const char *name)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_create_dir(path, nullptr, name);
    }
    FileCommand cmd;
    cmd.type = FileOpType::CREATE_DIR;
    cmd.path_obj = path;
    cmd.path = name;
    cmd.filename = nullptr;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::open_dir(const char *pathname, Directory **dir)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_open_dir(pathname, dir);
    }
    FileCommand cmd;
    cmd.type = FileOpType::OPEN_DIR;
    cmd.path_obj = nullptr;
    cmd.path = pathname;
    cmd.filename = nullptr;
    cmd.dir = dir;
    cmd.file_info = nullptr;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::read_dir(Directory *dir, FileInfo &info)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_read_dir(dir, info);
    }
    FileCommand cmd;
    cmd.type = FileOpType::READ_DIR;
    cmd.path_obj = nullptr;
    cmd.path = nullptr;
    cmd.filename = nullptr;
    cmd.dir = &dir;
    cmd.file_info = &info;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::close_dir(Directory *dir)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_close_dir(dir);
    }
    FileCommand cmd;
    cmd.type = FileOpType::CLOSE_DIR;
    cmd.path_obj = nullptr;
    cmd.path = nullptr;
    cmd.filename = nullptr;
    cmd.dir = &dir;
    cmd.file_info = nullptr;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::rename(const char* old_name, const char* new_name)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_rename(old_name, new_name);
    }
    FileCommand cmd;
    cmd.type = FileOpType::RENAME;
    cmd.path_obj = nullptr;
    cmd.filename = nullptr;
    cmd.path = old_name;
    cmd.new_name = new_name;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::get_free(Path* path, uint32_t& free, uint32_t& cluster_size)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_get_free(path, free, cluster_size);
    }
    FileCommand cmd;
    cmd.type = FileOpType::GET_FREE;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = nullptr;
    cmd.transferred = &free;
    cmd.size = cluster_size;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fs_read_sector(Path* path, uint8_t* buffer, int track, int sector)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_fs_read_sector(path, buffer, track, sector);
    }
    FileCommand cmd;
    cmd.type = FileOpType::FS_READ_SECTOR;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = nullptr;
    cmd.buffer = buffer;
    cmd.track = track;
    cmd.sector = sector;

    SendCommand(cmd);
    return cmd.result;
}

FRESULT FileManager::fs_write_sector(Path* path, uint8_t* buffer, int track, int sector)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_fs_write_sector(path, buffer, track, sector);
    }
    FileCommand cmd;
    cmd.type = FileOpType::FS_WRITE_SECTOR;
    cmd.path_obj = path;
    cmd.path = nullptr;
    cmd.filename = nullptr;
    cmd.buffer = buffer;
    cmd.track = track;
    cmd.sector = sector;

    SendCommand(cmd);
    return cmd.result;
}

bool FileManager::is_path_writable(Path *p)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->in_context()) {
        return fm->priv_is_path_writable(p) == FR_OK;
    }
    FileCommand cmd;
    cmd.type = FileOpType::IS_WRITEABLE;
    cmd.path_obj = p;

    SendCommand(cmd);
    return (cmd.result == FR_OK);
}

bool FileManager::is_path_valid(Path *p)
{
    FileInfo info(INFO_SIZE);
    FRESULT res = FileManager::fstat(p, nullptr, info, false);
    return res == FR_OK;
}
