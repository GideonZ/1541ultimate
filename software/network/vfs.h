#ifndef VFS_H
#define VFS_H

#define VFS_IRWXU 1
#define VFS_IRWXG 2
#define VFS_IRWXO 4
#define VFS_ISDIR(x)   ( x & 1 ) // ((x) && (x) type == VFS_DIR) //! Macro to know if a given entry represents a directory 
#define VFS_ISREG(x)   ( x & 2 )

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif
 
#include <stdint.h>

struct vfs;
struct vfs_file;
struct vfs_dir;
struct vfs_dirent;

typedef enum {
    e_vfs_files_only = 0,
    e_vfs_dirs_only,
    e_vfs_double_listed,
} into_mode_t;

struct vfs {
    void *path; // owned by file manager
    struct vfs_file *open_file; // reference only
};

struct vfs_file {
    void *file; // reference to File* class object from file manager
    struct vfs *parent_fs; // reference to whom we belong
    int eof;
};

struct vfs_dirent {
    void *file_info;
};

struct vfs_dir {
    void *entries;   // owned only
    int index;
    bool do_alternative;
    into_mode_t into_mode;
    struct vfs_dirent *entry; // owned
    struct vfs *parent_fs;    // reference only
};

typedef struct vfs        vfs_t;       
typedef struct vfs_file   vfs_file_t;  
typedef struct vfs_dir    vfs_dir_t;   
typedef struct vfs_dirent vfs_dirent_t;

typedef struct _vfs_stat_t {
    int st_size;
    int st_mode;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hr;
    uint8_t min;
    uint8_t sec;
    char name[64];
} vfs_stat_t;

EXTERNC void   vfs_load_plugin();
EXTERNC vfs_t *vfs_openfs();
EXTERNC void   vfs_closefs(vfs_t *);

EXTERNC vfs_file_t *vfs_open(vfs_t *fs, const char *name, const char *flags);
EXTERNC void vfs_close(vfs_file_t *file);
EXTERNC int  vfs_read(void *buffer, int chunks, int chunk_len, vfs_file_t *file);
EXTERNC int  vfs_write(const void *buffer, int chunks, int chunk_len, vfs_file_t *file);
EXTERNC int  vfs_eof(vfs_file_t *file);

EXTERNC vfs_dir_t *vfs_opendir(vfs_t *fs, const char *name, into_mode_t into_mode);
EXTERNC void vfs_closedir(vfs_dir_t *dir);
EXTERNC vfs_dirent_t *vfs_readdir(vfs_dir_t *dir);

EXTERNC int  vfs_stat_dirent(vfs_dirent_t *ent, vfs_stat_t *st);
EXTERNC int  vfs_stat(vfs_t *fs, const char *name, vfs_stat_t *st);
EXTERNC int  vfs_chdir(vfs_t *fs, const char *name);
EXTERNC char *vfs_getcwd(vfs_t *fs, void *args, int dummy);
EXTERNC int  vfs_rename(vfs_t *fs, const char *old_name, const char *new_name);
EXTERNC int  vfs_mkdir(vfs_t *fs, const char *name, int flags);
EXTERNC int  vfs_rmdir(vfs_t *fs, const char *name);
EXTERNC int  vfs_remove(vfs_t *fs, const char *name);

#endif
