#ifndef FILE_DIRENTRY_H
#define FILE_DIRENTRY_H

#include "path.h"
#include "file_system.h"

class FileDirEntry : public PathObject
{
    FileDirEntry *attempt_promotion(void);
public:
    FileInfo *info;
    FileDirEntry(PathObject *par, char *name);
    FileDirEntry(PathObject *par, FileInfo *fi);
    virtual ~FileDirEntry();

    virtual bool is_writable(void);
    virtual int fetch_children(void);
    virtual int fetch_context_items(IndexedList<PathObject *> &list);
    virtual int fetch_task_items(IndexedList<PathObject*> &list);
    virtual void execute(int selection);
    virtual char *get_name(void);
    virtual char *get_display_string(void);
	virtual int compare(PathObject *obj);
    virtual FileDirEntry *test_type(PathObject *obj);
    virtual FileInfo *get_file_info(void);
    int fetch_context_items_actual(IndexedList<PathObject *> &list);
    void remove_duplicates(void);
};

class FileTypeFactory
{
    IndexedList<FileDirEntry*> file_types;
public:
    FileTypeFactory() : file_types(20, 0) { }
    ~FileTypeFactory() { }
    
    FileDirEntry *promote(PathObject *obj) {
		FileInfo *info = obj->get_file_info();
        FileDirEntry *tester;
        for(int i=0;i<file_types.get_elements();i++) {
            tester = file_types[i];
            FileDirEntry *matched;
            matched = tester->test_type(obj);
            if(matched)
                return matched;
        }
        return 0;
    }
    
    void register_type(FileDirEntry *tester) {
        file_types.append(tester);
    }
};

extern FileTypeFactory file_type_factory;

#define FILEDIR_RENAME   			0x2001
#define FILEDIR_DELETE   			0x2002
#define FILEDIR_ENTERDIR 			0x2003
#define FILEDIR_DELETE_CONTINUED   	0x2004
#define FILEDIR_VIEW                0x2005

#define MENU_CREATE_D64    0x3001
#define MENU_CREATE_DIR    0x3002
#define MENU_CREATE_G64    0x3003

/* Debug options */
#define MENU_DUMP_INFO     0x30FE
#define MENU_DUMP_OBJECT   0x30FF

#endif
