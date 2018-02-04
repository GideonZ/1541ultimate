/*
 * filetypes.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef FILEMANAGER_FILETYPES_H_
#define FILEMANAGER_FILETYPES_H_

#include "factory.h"
#include "action.h"
#include "file_info.h"
class BrowsableDirEntry;
class Browsable;

class FileType
{
public:
	FileType() { }
	virtual ~FileType() { }

	static Factory<BrowsableDirEntry *, FileType *>* getFileTypeFactory() {
		static Factory<BrowsableDirEntry *, FileType *> file_type_factory;
		return &file_type_factory;
	}

	virtual int fetch_context_items(IndexedList<Action *> &list) {
		return 0;
	}

	virtual int getCustomBrowsables(Browsable *parent, IndexedList<Browsable *> &list) {
	    return -1;
	}
};

#endif /* FILEMANAGER_FILETYPES_H_ */
