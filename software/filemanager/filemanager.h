#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "file_system.h"
#include "filesystem_root.h"
#include "cached_tree_node.h"
#include "globals.h"
#include "observer.h"
#include "FreeRTOS.h"
#include "semphr.h"

void set_extension(char *buffer, char *ext, int buf_size);
void get_extension(const char *name, char *ext);
void fix_filename(char *buffer);

class MountPoint
{
	File *file;
	FileSystemInFile *emb;
public:
	MountPoint(File *f, FileSystemInFile *e) {
		file = f;
		emb = e;
	}

	File *get_file() {
		return file;
	}

	FileSystemInFile *get_embedded() {
		return emb;
	}

	bool match(FileSystem *fs, uint32_t inode) {
		return (file->get_file_system() == fs) && (file->get_inode() == inode);
	}
};

typedef enum  {
	eNodeAdded,			// New Node
	eNodeRemoved,       // Node no longer exists (deleted)
	eNodeMediaRemoved,  // Node lost all its children
	eNodeUpdated,		// Node status changed (= redraw line)
} eFileManagerEventType;

class FileManagerEvent
{
public:
	eFileManagerEventType eventType;
	mstring pathName;
	mstring newName;

	FileManagerEvent(eFileManagerEventType e, const char *p, const char *n = "") : eventType(e), pathName(p), newName(n) { }
};


class FileManager
{
	SemaphoreHandle_t serializer;

	IndexedList<MountPoint *>mount_points;
    IndexedList<File *>open_file_list;
	IndexedList<Path *>used_paths;
	IndexedList<ObserverQueue *>observers;
	CachedTreeNode *root;
	FileSystem *rootfs;

    FileManager() : mount_points(8, NULL), open_file_list(16, NULL), used_paths(8, NULL), observers(4, NULL) {
        root = new CachedTreeNode(NULL, "RootNode");
        root->get_file_info()->attrib = AM_DIR;
        rootfs = new FileSystem_Root(root);
#ifdef OS
        serializer = xSemaphoreCreateRecursiveMutex();
#else
        serializer = 0;
#endif
    }

    ~FileManager() {
#ifdef OS
    	vSemaphoreDelete(serializer);
#endif
    	for(int i=0;i<open_file_list.get_elements();i++) {
        	delete open_file_list[i];
        }
        for(int i=0;i<used_paths.get_elements();i++) {
        	delete used_paths[i];
        }
        for(int i=0;i<mount_points.get_elements();i++) {
        	delete mount_points[i];
        }
        delete rootfs;
        delete root;
    }

	FRESULT find_pathentry(PathInfo &pathInfo, bool enter_mount);
	FRESULT fopen_impl(PathInfo &pathInfo, uint8_t flags, File **);

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

	void dump(void) {
		lock();
		printf("** This is a dump of the state of the file manager **\n");
		root->dump(0);

    	printf("\nOpen files:\n");
    	for(int i=0;i<open_file_list.get_elements();i++) {
    		File *f = open_file_list[i];
    		printf("'%s' (%d)\n", f->get_name(), f->get_size());
    	}
    	printf("\nUsed paths:\n");
    	for(int i=0;i<used_paths.get_elements();i++) {
    		Path *p = used_paths[i];
    		printf("'%s'\n", p->get_path());
    	}
    	printf("\nMount Points:\n");
    	for(int i=0;i<mount_points.get_elements();i++) {
    		MountPoint *f = mount_points[i];
    		printf("%p:\n", mount_points[i]->get_embedded()); // , mount_points[i]->get_path()
    	}
		unlock();
	}

	void invalidate(CachedTreeNode *obj, int includeSelf);
    void add_root_entry(CachedTreeNode *obj);
    void remove_root_entry(CachedTreeNode *obj);

    MountPoint *add_mount_point(File *, FileSystemInFile *);
    MountPoint *find_mount_point(FileInfo *info, FileInfo *parent, const char *filepath);

    // Functions to use / handle path objects:
    Path *get_new_path(const char *owner) {
    	Path *p = new Path();
    	p->owner = owner;
    	used_paths.append(p);
    	return p;
    }
    void  release_path(Path *p) {
    	used_paths.remove(p);
    	delete p;
    }

    bool  is_path_valid(Path *p);
    bool  is_path_writable(Path *p);

    void  get_display_string(Path *p, const char *filename, char *buffer, int width);

    FRESULT fstat(Path *path, const char *filename, FileInfo &info);
    FRESULT fstat(const char *pathname, FileInfo &info);

    FRESULT fopen(Path *path, const char *filename, uint8_t flags, File **);
    FRESULT fopen(const char *pathname, uint8_t flags, File **);

    void 	fclose(File *f);
    FRESULT fcopy(const char *path, const char *filename, const char *dest);

    FRESULT delete_file(Path *path, const char *name);
    FRESULT delete_file(const char *pathname);

    FRESULT create_dir(Path *path, const char *name);
    FRESULT create_dir(const char *pathname);

    FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target);
    FRESULT print_directory(const char *path);

    void registerObserver(ObserverQueue *q) {
    	observers.append(q);
    }
    void deregisterObserver(ObserverQueue *q) {
    	observers.remove(q);
    }
    void sendEventToObservers(eFileManagerEventType e, const char *p, const char *n="") {
    	printf("Sending FM event to %d observers: %d %s %s\n", observers.get_elements(), e, p, n);
    	for(int i=0;i<observers.get_elements();i++) {
    		FileManagerEvent *ev = new FileManagerEvent(e, p, n);
    		observers[i]->putEvent(ev);
    	}
    }
};

#endif
