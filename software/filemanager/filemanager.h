#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "path.h"
#include "event.h"
#include "poll.h"
#include "file_system.h"

//#include "file_direntry.h"

void poll_filemanager(Event &e);

class FileManager : public PathObject
{
	IndexedList<File *>open_file_list;
public:
    FileManager() : PathObject(NULL), open_file_list(16, NULL) {
        poll_list.append(&poll_filemanager);
    }

    ~FileManager() {
        poll_list.remove(&poll_filemanager);
        cleanup_children();
    }
    
    void handle_event(Event &e);

    File *fopen(char *filename, BYTE flags);
    File *fopen(PathObject *obj, BYTE flags);
    void fclose(File *f);
};

extern FileManager root;

#endif
