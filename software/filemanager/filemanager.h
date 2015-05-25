#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "event.h"
#include "poll.h"
#include "file_system.h"
#include "cached_tree_node.h"
#include "globals.h"

//#include "menu.h"
#define MENU_DUMP_ROOT     0x30FD

void set_extension(char *buffer, char *ext, int buf_size);
void fix_filename(char *buffer);

struct FileNodePair
{
	File *file;
	CachedTreeNode *node;
};
const FileNodePair empty_pair = { NULL, NULL };

class FileManager
{
    FRESULT last_error;
	IndexedList<FileNodePair>open_file_list;
	IndexedList<Path *>used_paths;
	CachedTreeNode *root;

    File *fopen_impl(Path *path, char *filename, uint8_t flags);

    FileManager() : open_file_list(16, empty_pair), used_paths(8, NULL) {
        MainLoop :: addPollFunction(poll_filemanager);
        last_error = FR_OK;
        root = new CachedTreeNode(NULL, "RootNode");
    }

    ~FileManager() {
    	MainLoop :: removePollFunction(poll_filemanager);
        for(int i=0;i<open_file_list.get_elements();i++) {
        	delete open_file_list[i].file;
        }
        for(int i=0;i<used_paths.get_elements();i++) {
        	delete used_paths[i];
        }
        delete root;
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

	static void poll_filemanager(Event &e)
	{
		static FileManager *fm = getFileManager();
		fm->handle_event(e);
	}

	void dump(void) {
    	root->dump(0);
    }

    void handle_event(Event &e);
    CachedTreeNode *get_root();
    void add_root_entry(CachedTreeNode *obj);
    void remove_root_entry(CachedTreeNode *obj);

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

    File *fopen(Path *path, char *filename, uint8_t flags);
    File *fopen_node(CachedTreeNode *node, uint8_t flags);
    void fclose(File *f);
};

#endif
