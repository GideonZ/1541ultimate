#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "event.h"
#include "poll.h"
#include "file_system.h"

//#include "file_direntry.h"

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
	
    File *fopen(char *filename, BYTE flags);
    File *fopen(PathObject *obj, BYTE flags);
    File *fcreate(char *filename, PathObject *dir);
    
    void fclose(File *f);
};

extern FileManager root;

#endif
