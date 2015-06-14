#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "event.h"
#include "poll.h"
#include "file_system.h"
#include "cached_tree_node.h"
#include "globals.h"

void set_extension(char *buffer, char *ext, int buf_size);
void get_extension(const char *name, char *ext);
void fix_filename(char *buffer);

class MountPoint
{
	mstring full_path;
	File *file;
	FileSystemInFile *emb;
	// int ref_count;
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

	bool match(char *name) {
		return (full_path == name);
	}
};

class FileManager
{
    FRESULT last_error;
    IndexedList<MountPoint *>mount_points;
    IndexedList<File *>open_file_list;
	IndexedList<Path *>used_paths;
	CachedTreeNode *root;

    File *fopen_impl(Path *path, char *filename, uint8_t flags);

    FileManager() : mount_points(8, NULL), open_file_list(16, NULL), used_paths(8, NULL) {
        MainLoop :: addPollFunction(poll_filemanager);
        last_error = FR_OK;
        root = new CachedTreeNode(NULL, "RootNode");
    }

    ~FileManager() {
    	MainLoop :: removePollFunction(poll_filemanager);
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

public:
	static FileManager* getFileManager() {
#ifndef _NO_FILE_ACCESS
		static FileManager file_manager;
		return &file_manager;
#else
		return 0;
#endif
	}

	static void poll_filemanager(Event &e)
	{
		static FileManager *fm = getFileManager();
		fm->handle_event(e);
	}

	void dump(void) {
    	root->dump(0);

    	printf("\nOpen files:\n");
    	for(int i=0;i<open_file_list.get_elements();i++) {
    		File *f = open_file_list[i];
    		printf("'%s' (%d) in '%s'\n", f->info->lfname, f->get_size(), f->get_path());
    	}
    	printf("\nMount Points:\n");
    	for(int i=0;i<mount_points.get_elements();i++) {
    		MountPoint *f = mount_points[i];
    		printf("%p: '%s'\n", mount_points[i]->get_embedded(), mount_points[i]->get_path());
    	}

	}
    void handle_event(Event &e);
    void add_root_entry(CachedTreeNode *obj);
    void remove_root_entry(CachedTreeNode *obj);
    void add_mount_point(File *, FileSystemInFile *);
    FileSystemInFile *find_mount_point(char *full_path);

    FRESULT get_last_error(void) {
        return last_error;
    }
    
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

    bool  is_path_writable(Path *p);

    File *fopen(Path *path, char *filename, uint8_t flags);
    void fclose(File *f);
    FRESULT delete_file_by_info(FileInfo *info);
    FRESULT create_dir_in_path(Path *path, char *name);

    FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target)
    {
    	CachedTreeNode *n;
    	if (!validatePath(p, &n)) {
    		return FR_NO_PATH;
    	}
    	int result = n->fetch_children();
    	if (result < 0) {
    		return FR_NO_FILE;
    	}
    	for (int i=0;i<n->children.get_elements();i++) {
    		target.append(new FileInfo(*(n->children[i]->get_file_info())));
    	}
    	return FR_OK;
    }
};

#endif
