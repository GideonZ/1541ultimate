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

class FileSystem;

class FileType
{
public:
	FileType() { }
	virtual ~FileType() { }

	virtual int fetch_context_items(IndexedList<Action *> &list) {
		return 0;
	}
};

#endif /* FILEMANAGER_FILETYPES_H_ */
