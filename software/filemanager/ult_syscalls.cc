#include "filemanager.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* It turns out that older newlib versions use different symbol names which goes
 * against newlib recommendations. Anyway this is fixed in later version.
 */
#if (__NEWLIB__ <= 2 && __NEWLIB_MINOR__ <= 5) || (__NEWLIB__ == 3)
#    define _sbrk sbrk
#    define _write write
#    define _open open
#    define _close close
#    define _lseek lseek
#    define _read read
#    define _fstat fstat
#    define _isatty isatty
#endif

#define MAX_FILES 200
static File *_fm_file_mapping[MAX_FILES]; // initialized with zero due to bss

static int find_slot()
{
    // reserve 0, 1 and 2 for stdin, stdout and stderr
    for (int i=3;i++;i<MAX_FILES) {
        if (! _fm_file_mapping[i]) {
            return i;
        }
    }
    return 0;
}

extern "C" {

int _fstat(int file, struct stat *st)
{
    st->st_mode = S_IFCHR; // all files are "character special files"
    return 0;
}

int _open(const char *name, int oflag, ...)
{
    FileManager *fm = FileManager::getFileManager();
    int fd = find_slot();
    if (!fd) {
        errno = -ENFILE;
        return -1;
    }
    printf("Trying to open with mode %02x. %s\n", oflag, name);
    uint8_t u8flags = FA_READ;

// This argument formed by OR'ing together optional parameters and (from <fcntl.h>) one of:
// O_RDONLY, O_RDWR and O_WRONLY
// O_APPEND data written will be appended to the end of the file. The file operations will always adjust the position pointer to the end of the file.
// O_CREAT  Create the file if it does not exist; otherwise the open fails setting errno to ENOENT.
// O_EXCL   Used with O_CREAT if the file already exists, then fail, setting errno to EEXIST.
// O_TRUNC  If the file already exists then discard its previous contents, reducing it to an empty file. Not applicable for a device or named pipe.

    if (oflag & O_RDONLY) u8flags |= FA_READ;
    if (oflag & O_WRONLY) { u8flags |= FA_WRITE; u8flags &= ~FA_READ; }
    if (oflag & O_RDWR)   u8flags |= FA_READ | FA_WRITE;
    if (oflag & O_CREAT)  u8flags |= FA_CREATE_ALWAYS;
    if (oflag & O_EXCL)   u8flags |= FA_CREATE_NEW;
    if (oflag & O_TRUNC)  u8flags |= FA_CREATE_ALWAYS;

    File *file = NULL;
    FRESULT res = fm->fopen(name, u8flags, &file);
    if (res == FR_OK) {
        _fm_file_mapping[fd] = file;
        if (oflag & O_APPEND) {
            uint32_t size = file->get_size();
            res = file->seek(size);
        }
        return fd; // OK
    }
    errno = -ENOENT;
    return -1;

}

int _close(int fd)
{
    if (! _fm_file_mapping[fd]) {
        errno = -EBADF;
        return -1;
    }
    FileManager *fm = FileManager::getFileManager();
    fm->fclose(_fm_file_mapping[fd]); // returns void?!
    _fm_file_mapping[fd] = NULL;

    return 0; // always ok?
}

ssize_t _read(int fd, void *ptr, size_t len)
{
    File *f = _fm_file_mapping[fd];
    if (! f) {
        errno = -EBADF;
        return -1;
    }
    uint32_t trans = 0;
    FRESULT fres = f->read(ptr, len, &trans);
    if (fres == FR_OK) {
        return (ssize_t) trans;
    }
    errno = -EIO; // TODO: map to correct errno value
    return -1;
}

ssize_t _write(int fd, const void *ptr, size_t len)
{
    File *f = _fm_file_mapping[fd];
    if (! f) {
        errno = -EBADF;
        return -1;
    }
    uint32_t trans = 0;
    FRESULT fres = f->write(ptr, len, &trans);
    if (fres == FR_OK) {
        return (ssize_t) trans;
    }
    errno = -EIO; // TODO: map to correct errno value
    return -1;
}

int _lstat(const char *file, struct stat *st)
{
    printf("Trying to call lstat on %s. Not implemented.\n", file);
    return -1;
}

off_t _lseek(int fd, off_t ptr, int dir)
{
    File *f = _fm_file_mapping[fd];
    if (! f) {
        errno = -EBADF;
        return -1;
    }
    FRESULT fres;
    switch(dir) {
        case SEEK_CUR:
            errno = -EPERM; // not implemented
            return -1;
        case SEEK_SET:
            fres = f->seek((uint32_t)ptr);
            return ptr; // assume it worked for now
        case SEEK_END:
            fres = f->seek((uint32_t)ptr + f->get_size());
            return (off_t)f->get_size(); // assume that we don't support reading after the file size
        default:
            errno = -EINVAL;
            return -1;
    }
}

int isatty(int fd)
{
    if (fd < 3) {
        return 1;
    }
    errno = -ENOTTY;
    return 0;
}

}
