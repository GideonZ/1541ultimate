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
 
struct vfs;
struct vfs_file;
struct vfs_dir;
struct vfs_dirent;

struct vfs {
    void *path; // owned
    struct vfs_dirent *last_direntry; // reference only
    struct vfs_file *open_file; // reference only
};

struct vfs_file {
    void *file; // reference to File* class object from file manager
    struct vfs *parent_fs; // reference to whom we belong
    int eof;
};

struct vfs_dirent {
    void *path_object;    
    char *name;
};

struct vfs_dir {
    void *path_object;   // reference only 
    int index;
    struct vfs_dirent *entry; // owned
    struct vfs *parent_fs;    // reference only
};

typedef struct vfs        vfs_t;       
typedef struct vfs_file   vfs_file_t;  
typedef struct vfs_dir    vfs_dir_t;   
typedef struct vfs_dirent vfs_dirent_t;

//

typedef struct _vfs_stat_t {
    int st_size;
    time_t st_mtime;
    int st_mode;
} vfs_stat_t;

EXTERNC void   vfs_load_plugin();
EXTERNC vfs_t *vfs_openfs();
EXTERNC void   vfs_closefs(vfs_t *);

EXTERNC vfs_file_t *vfs_open(vfs_t *fs, const char *name, char *flags);
EXTERNC void vfs_close(vfs_file_t *file);
EXTERNC int  vfs_read(void *buffer, int chunks, int chunk_len, vfs_file_t *file);
EXTERNC int  vfs_write(void *buffer, int chunks, int chunk_len, vfs_file_t *file);
EXTERNC int  vfs_eof(vfs_file_t *file);

EXTERNC vfs_dir_t *vfs_opendir(vfs_t *fs, const char *name);
EXTERNC void vfs_closedir(vfs_dir_t *dir);
EXTERNC vfs_dirent_t *vfs_readdir(vfs_dir_t *dir);

EXTERNC int  vfs_stat(vfs_t *fs, const char *name, vfs_stat_t *st);
EXTERNC int  vfs_chdir(vfs_t *fs, const char *name);
EXTERNC char *vfs_getcwd(vfs_t *fs, void *args, int dummy);
EXTERNC int  vfs_rename(vfs_t *fs, const char *old_name, const char *new_name);
EXTERNC int  vfs_mkdir(vfs_t *fs, const char *name, int flags);
EXTERNC int  vfs_rmdir(vfs_t *fs, const char *name);
EXTERNC int  vfs_remove(vfs_t *fs, const char *name);

#endif
