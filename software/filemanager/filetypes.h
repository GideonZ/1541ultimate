/*
 * filetypes.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef FILEMANAGER_FILETYPES_H_
#define FILEMANAGER_FILETYPES_H_

class FileType
{
public:
	FileType();

    virtual FileDirEntry *test_type(CachedTreeNode *obj);
	FileDirEntry *attempt_promotion(void);
    int fetch_context_items_actual(IndexedList<CachedTreeNode *> &list);
    virtual int fetch_context_items(IndexedList<CachedTreeNode *> &list);
    virtual int fetch_task_items(IndexedList<CachedTreeNode*> &list);
    virtual void execute(int selection);

};

typedef FileType *(*fileTypeTestFunction_t)(FileInfo *inf);

class FileTypeFactory
{
    IndexedList<fileTypeTestFunction_t> file_types;
public:
    FileTypeFactory() : file_types(16, 0) { }
    ~FileTypeFactory() { }

    FileType *find(FileInfo *inf) {
        for(int i=0;i<file_types.get_elements();i++) {
        	fileTypeTestFunction_t tester = file_types[i];
            FileSystem *matched = tester(inf);
            if(matched)
                return matched;
        }
        return 0;
    }

    void register_type(fileTypeTestFunction_t tester) {
    	file_types.append(tester);
    }
};

extern FileTypeFactory file_type_factory;

class FileTypeRegistrator
{
public:
	FileTypeRegistrator(fileTypeTestFunction_t func) {
		file_type_factory.register_type(func);
	}
};


#endif /* FILEMANAGER_FILETYPES_H_ */
