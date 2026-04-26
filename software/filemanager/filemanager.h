#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <stdint.h>

#include "path.h"
#include "file_system.h"
#include "filesystem_root.h"
#include "cached_tree_node.h"
#include "observer.h"
#include "embedded_fs.h"

#ifdef OS
#include "FreeRTOS.h"
#include "semphr.h"
#else
//typedef void * SemaphoreHandle_t;
#endif

#define INFO_SIZE 128

class MountPoint
{
    Path *path;
    File *file;
	FileSystemInFile *emb;
public:
	MountPoint(SubPath *p, File *f, FileSystemInFile *e) {
		path = p->get_new_path();
	    file = f;
		emb = e;
	}

	~MountPoint() {
	    delete path;
	    delete emb;
	}

	File *get_file() {
		return file;
	}

	FileSystemInFile *get_embedded() {
		return emb;
	}

	const char *get_path() {
	    return path->get_path();
	}

	bool match(FileSystem *fs, SubPath *sp) {
		return (file->get_file_system() == fs) && (sp->match(path));
	}
};

typedef enum  {
	eRefreshDirectory,  // Contents of directory have changed
	eNodeAdded,			// New Node
	eNodeRemoved,       // Node no longer exists (deleted)
	eNodeMediaRemoved,  // Node lost all its children
	eNodeUpdated,		// Node status changed (= redraw line)
	eChangeDirectory,   // Request to change current directory within observer task
} eFileManagerEventType;

class FileManagerEvent
{
public:
    eFileManagerEventType eventType;
	mstring pathName;
	mstring newName;

	FileManagerEvent(eFileManagerEventType e, const char *p, const char *n = "") : eventType(e), pathName(p), newName(n) {  }
	~FileManagerEvent() { }
};

struct ManagedTempEntry
{
	mstring path;
	uint32_t open_count;
	bool delete_on_last_close;
};


class FileManager
{
#ifdef OS
    SemaphoreHandle_t serializer;
#endif
	IndexedList<MountPoint *>mount_points;
    IndexedList<File *>open_file_list;
	IndexedList<ManagedTempEntry *>managed_temp_entries;
	uint32_t next_temp_seq;
	bool temp_auto_cleanup_enabled;
	bool temp_use_cache_subfolder_enabled;
	//IndexedList<Path *>used_paths;
	IndexedList<ObserverQueue *>observers;
	CachedTreeNode *root;
	FileSystem *rootfs;

    FileManager() : mount_points(8, NULL), open_file_list(16, NULL), managed_temp_entries(16, NULL), next_temp_seq(0), temp_auto_cleanup_enabled(true), temp_use_cache_subfolder_enabled(true), /*used_paths(8, NULL), */observers(4, NULL) {
        root = new CachedTreeNode(NULL, "RootNode");
        root->get_file_info()->attrib = AM_DIR;
        rootfs = new FileSystem_Root(root);
#ifdef OS
        serializer = xSemaphoreCreateRecursiveMutex();
#endif
    }

    ~FileManager() {
#ifdef OS
    	vSemaphoreDelete(serializer);
#endif
        for(int i=0;i<mount_points.get_elements();i++) {
            release_mount_point(mount_points[i]);
        }

        for(int i=0;i<open_file_list.get_elements();i++) {
        	delete open_file_list[i];
        }
		for(int i=0;i<managed_temp_entries.get_elements();i++) {
		    delete managed_temp_entries[i];
		}
/*
        for(int i=0;i<used_paths.get_elements();i++) {
        	delete used_paths[i];
        }
*/
        delete rootfs;
        delete root;
    }

	FRESULT find_pathentry(PathInfo &pathInfo, bool enter_mount);
	FRESULT fopen_impl(PathInfo &pathInfo, uint8_t flags, File **);
	FRESULT rename_impl(PathInfo &from, PathInfo &to);
	FRESULT delete_file_impl(PathInfo &pathInfo);
	void release_mount_point(MountPoint *mp);
	void get_temp_directory_path(const char *category, mstring &directory_out);
	FRESULT build_temp_path(const char *category, const char *suggested_name, uint32_t seq, bool unique_name, uint32_t suffix,
	        bool create_dirs, mstring &canonical_path_out);
	ManagedTempEntry *find_managed_temp_entry(const char *path);
	bool is_path_under_managed_root(const char *path);
	void note_managed_temp_open(File *file);
	void note_managed_temp_deleted(const char *path);
	void note_managed_temp_renamed(const char *old_path, const char *new_path);
	void enforce_temp_limits(void);

//	friend class FileDirEntry;

    void lock() {
#ifdef OS
    	xSemaphoreTakeRecursive(serializer, portMAX_DELAY);
#endif
    }
    void unlock() {
#ifdef OS
    	xSemaphoreGiveRecursive(serializer);
#endif
    }
public:

    static FileManager* getFileManager() {
#ifndef _NO_FILE_ACCESS
		static FileManager file_manager;
		return &file_manager;
#else
		return 0;
#endif
	}

    static const char *eventStrings[];

    void dump(void) {
		lock();
		printf("** This is a dump of the state of the file manager **\n");
		root->dump(0);

    	printf("\nOpen files %d:\n", open_file_list.get_elements());

    	for(int i=0;i<open_file_list.get_elements();i++) {
    		File *f = open_file_list[i];
    		printf("'%s' (%d), FS=%p\n", f->get_path(), f->get_size(), f->get_file_system());
    	}

    	printf("\nMount Points:\n");
    	for(int i=0;i<mount_points.get_elements();i++) {
    		MountPoint *f = mount_points[i];
    		printf("%p: %s\n", f->get_embedded(), f->get_path());
    	}
		unlock();
	}

	void invalidate(CachedTreeNode *obj);
	void remove_from_parent(CachedTreeNode *o);
    void add_root_entry(CachedTreeNode *obj);
    void remove_root_entry(CachedTreeNode *obj);

    MountPoint *add_mount_point(SubPath *path, File *, FileSystemInFile *);
    MountPoint *find_mount_point(SubPath *path, FileInfo *info);

    // Functions to use / handle path objects:
    Path *get_new_path(const char *owner) {
    	Path *p = new Path();
    	p->owner = owner;
    	//used_paths.append(p);
    	return p;
    }
    void  release_path(Path *p) {
    	//used_paths.remove(p);
    	delete p;
    }

    bool  is_path_valid(Path *p);
    bool  is_path_writable(Path *p);

    void  get_display_string(Path *p, const char *filename, char *buffer, int width);

    FRESULT get_free(Path *path, uint32_t &free, uint32_t &cluster_size);
    FRESULT fs_read_sector(Path *path, uint8_t *buffer, int track, int sector);
    FRESULT fs_write_sector(Path *path, uint8_t *buffer, int track, int sector);

    FRESULT fstat(Path *path, const char *filename, FileInfo &info);
    FRESULT fstat(const char *path, const char *name, FileInfo &info);
    FRESULT fstat(const char *pathname, FileInfo &info);

    FRESULT fopen(Path *path, const char *filename, uint8_t flags, File **);
    FRESULT fopen(const char *path, const char *filename, uint8_t flags, File **);
	FRESULT fopen(const char *pathname, uint8_t flags, File **);

	FRESULT get_temp_path(const char *category, const char *suggested_name, mstring *canonical_path_out);
	FRESULT ensure_temp_directory(const char *category, mstring &directory_out);
	FRESULT create_temp_file(const char *category, const char *suggested_name, uint8_t open_flags, File **file, mstring *canonical_path_out);
	FRESULT save_temp_file(const char *category, const char *suggested_name, const uint8_t *data, uint32_t len, mstring *canonical_path_out);

	void set_temp_auto_cleanup_enabled(bool enabled) { temp_auto_cleanup_enabled = enabled; }
	void set_temp_use_cache_subfolder_enabled(bool enabled) { temp_use_cache_subfolder_enabled = enabled; }
	bool is_temp_auto_cleanup_enabled(void) { return temp_auto_cleanup_enabled; }
	bool is_temp_use_cache_subfolder_enabled(void) { return temp_use_cache_subfolder_enabled; }

    void 	fclose(File *f);
    FRESULT fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool overwrite);

    FRESULT rename(Path *old_path, const char *old_name, Path *new_path, const char *new_name);
    FRESULT rename(Path *path, const char *old_name, const char *new_name);
    FRESULT rename(const char *old_name, const char *new_name);

    FRESULT delete_file(Path *path, const char *name);
    FRESULT delete_file(const char *pathname);
    FRESULT delete_recursive(Path *path, const char *name);

    FRESULT create_dir(Path *path, const char *name);
    FRESULT create_dir(const char *pathname);

    FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target, const char *matchPattern);
    FRESULT print_directory(const char *path);
    FRESULT load_file(const char *path, const char *filename, uint8_t *mem, uint32_t maxlen, uint32_t *transferred);
    FRESULT save_file(bool overwrite, const char *path, const char *filename, uint8_t *mem, uint32_t len, uint32_t *transferred);

    void registerObserver(ObserverQueue *q) {
    	observers.append(q);
    }
    void deregisterObserver(ObserverQueue *q) {
    	observers.remove(q);
    }
    void sendEventToObservers(eFileManagerEventType e, const char *p, const char *n="") {
        // printf("Sending FM event to %d observers: %d %s %s\n", observers.get_elements(), e, p, n);
    	for(int i=0;i<observers.get_elements();i++) {
    		ObserverQueue *q = observers[i];
    		if (!q) {
    		    continue;
    		}
    		FileManagerEvent *ev = new FileManagerEvent(e, p, n);
    		// printf("[%d]", FileManagerEvent :: numFME);
    		if (!(q->putEvent(ev))) {
                // printf("Failed to post message to queue #%d - %s.\n", i, q->getName());
                delete ev;
    		} else {
    		    // printf("Sent FM event at %p to %s. (%s)\n", ev, q->getName(), p);
    		}
    	}
    }
};

#endif
