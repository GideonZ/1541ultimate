#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "event.h"
#include "poll.h"
#include "file_system.h"
#include "menu.h"

#define MENU_DUMP_ROOT     0x30FD

void poll_filemanager(Event &e);

void set_extension(char *buffer, char *ext, int buf_size);
void fix_filename(char *buffer);

class FileManager : public PathObject
{
	IndexedList<File *>open_file_list;
public:
    FileManager(char *n) : PathObject(NULL, n), open_file_list(16, NULL) {
        poll_list.append(&poll_filemanager);
    }

    ~FileManager() {
        poll_list.remove(&poll_filemanager);
        cleanup_children();
    }
    
    void handle_event(Event &e);

	int  fetch_children()  {
		return children.get_elements(); // just return the number of elements that
			// are added to the file manager by other threads (event polls)
	}

    int fetch_task_items(IndexedList<PathObject*> &item_list) {
//        item_list.append(new MenuItem(this, "Dump Root", MENU_DUMP_ROOT));
//        return 1;
        return 0;
    }
	
    void execute(int select) {
        dump();
    }

    File *fopen(char *filename, BYTE flags);
    File *fopen(char *filename, PathObject *dir, BYTE flags);
    File *fopen(PathObject *obj, BYTE flags);
    File *fcreate(char *filename, PathObject *dir);
    
    void fclose(File *f);
};

extern FileManager root;

#endif
