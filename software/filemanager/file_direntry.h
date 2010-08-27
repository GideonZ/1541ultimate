#ifndef FILE_DIRENTRY_H
#define FILE_DIRENTRY_H

#include "path.h"
#include "file_system.h"
#include "small_printf.h"

class FileDirEntry : public PathObject
{
    FileDirEntry *attempt_promotion(void);
public:
    FileInfo *info;
    FileDirEntry(PathObject *par, FileInfo *fi);
    virtual ~FileDirEntry();

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
};

class FileTypeFactory
{
    IndexedList<FileDirEntry*> file_types;
public:
    FileTypeFactory() : file_types(20, 0) { }
    ~FileTypeFactory() { }
    
    FileDirEntry *promote(PathObject *obj) {
		FileInfo *info = obj->get_file_info();
        printf("Trying to promote %s.\n", info->lfname);
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

#endif
