#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "file_system.h"
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
	mstring full_path;
	File *file;
	FileSystemInFile *emb;
public:
	MountPoint(File *f, FileSystemInFile *e) {
		file = f;
		emb = e;
		full_path = f->get_path();
		full_path += f->getFileInfo()->lfname;
		full_path += "/";
		printf("Created mount point with full path = '%s'\n", full_path.c_str());
	}

	const char *get_path() {
		return (const char *)full_path.c_str();
	}

	File *get_file() {
		return file;
	}

	FileSystemInFile *get_embedded() {
		return emb;
	}

	bool match(const char *name) {
		return (full_path == name);
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
	struct reworked_t {
		char *copy;
		char *purename;
		Path *path;
	};

	SemaphoreHandle_t serializer;

	FRESULT last_error;
	IndexedList<MountPoint *>mount_points;
    IndexedList<File *>open_file_list;
	IndexedList<Path *>used_paths;
	IndexedList<ObserverQueue *>observers;
	CachedTreeNode *root;

	bool  reworkPath(Path *path, const char *pathname, const char *filename, reworked_t& rwp);
    File *fopen_impl(Path *path, char *filename, uint8_t flags);

    FileManager() : mount_points(8, NULL), open_file_list(16, NULL), used_paths(8, NULL), observers(4, NULL) {
        last_error = FR_OK;
        root = new CachedTreeNode(NULL, "RootNode");
        serializer = xSemaphoreCreateRecursiveMutex();
    }

    ~FileManager() {
    	vSemaphoreDelete(serializer);
    	for(int i=0;i<open_file_list.get_elements();i++) {
        	delete open_file_list[i];
        }
        for(int i=0;i<used_paths.get_elements();i++) {
        	delete used_paths[i];
        }
        for(int i=0;i<mount_points.get_elements();i++) {
        	delete mount_points[i];
        }
        delete root;
    }

    friend class FileDirEntry;
    int validatePath(Path *p, CachedTreeNode **n);
    File *fopen_node(CachedTreeNode *info, uint8_t flags);

    void lock() {
    	xSemaphoreTake(serializer, portMAX_DELAY);
    }
    void unlock() {
    	xSemaphoreGive(serializer);
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
    		printf("'%s' (%d) in '%s'\n", f->info->lfname, f->get_size(), f->get_path());
    	}
    	printf("\nUsed paths:\n");
    	for(int i=0;i<used_paths.get_elements();i++) {
    		Path *p = used_paths[i];
    		printf("'%s'\n", p->get_path());
    	}
    	printf("\nMount Points:\n");
    	for(int i=0;i<mount_points.get_elements();i++) {
    		MountPoint *f = mount_points[i];
    		printf("%p: '%s'\n", mount_points[i]->get_embedded(), mount_points[i]->get_path());
    	}
		unlock();
	}

	void invalidate(CachedTreeNode *obj, int includeSelf);
    void add_root_entry(CachedTreeNode *obj);
    void remove_root_entry(CachedTreeNode *obj);
    void add_mount_point(File *, FileSystemInFile *);
    MountPoint *find_mount_point(const char *full_path);

    FRESULT get_last_error(void) {
        return last_error;
    }
    
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

    bool fstat(FileInfo &info, const char *path, const char *filename);
    File *fopen(Path *path, const char *filename, uint8_t flags);
    File *fopen(const char *path, const char *filename, uint8_t flags);
    void fclose(File *f);
    FRESULT delete_file_by_info(FileInfo *info);
    FRESULT create_dir_in_path(Path *path, char *name);
    FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target);

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
