/*
 * browsable_root.h
 *
 *  Created on: May 5, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_BROWSABLE_ROOT_H_
#define USERINTERFACE_BROWSABLE_ROOT_H_

#include "browsable.h"
#include "filemanager.h"
#include "filetypes.h"
#include "globals.h"
#include "size_str.h"
#include "user_file_interaction.h"

class BrowsableDirEntry : public Browsable
{
	FileInfo *info;
	FileType *type;
	Path *path;
	Path *parent_path;
public:
	BrowsableDirEntry(Path *pp, FileInfo *info, bool sel) {
		this->info = info;
		this->type = NULL;
		this->selectable = sel;
		this->path = 0;
		this->parent_path = pp;
	}

	virtual ~BrowsableDirEntry() {
		if (type)
			delete type;
		if (info)
			delete info;
		if (path)
			FileManager :: getFileManager() -> release_path(path);
	}

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		if (!path) {
			path = FileManager :: getFileManager() -> get_new_path(info->lfname);
			path->cd(parent_path->get_path());
			path->cd(info->lfname);
		}
		IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
		path->get_directory(*infos);

		for(int i=0;i<infos->get_elements();i++) {
			FileInfo *inf = (*infos)[i];
			list.append(new BrowsableDirEntry(path, inf, true)); // pass ownership of the FileInfo to the browsable object
		}
		delete infos; // deletes the indexed list, but not the FileInfos
		return list.get_elements();
	}

	virtual char *getName() {
		return info->lfname;
	}

	virtual char *getDisplayString() {
	    static char buffer[48];
	    static char sizebuf[8];

	    if (info->attrib & AM_VOL) {
	        sprintf(buffer, "\eR%29s\er VOLUME", info->lfname);
	    } else if(info->is_directory()) {
	        sprintf(buffer, "%29s\eJ DIR", info->lfname);
	    } else {
	        size_to_string_bytes(info->size, sizebuf);
	        sprintf(buffer, "%29s\e7 %3s %s", info->lfname, info->extension, sizebuf);
	    }
	    return buffer;
	}

	virtual void fetch_context_items(IndexedList<Action *>&items) {
/*
		if (!type)
			type = Globals :: getFileTypeFactory()->create(info);
		if (type)
			type->fetch_context_items(items);
*/
		UserFileInteraction :: fetch_context_items(info, items);
	}

	virtual int fetch_task_items(IndexedList<Action *> &list) {
		return 0; // UserFileInteraction :: fetch_task_items(info, list);
	}

	virtual bool invalidateMatch(const void *obj) {
		return false;
	}
};

class BrowsableRoot : public Browsable
{
	Path *root;
public:
	BrowsableRoot() {
		root = FileManager :: getFileManager() -> get_new_path("Browsable Root");
	}
	virtual ~BrowsableRoot() {
		FileManager :: getFileManager() -> release_path(root);
	}

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
		FileManager :: getFileManager() -> get_directory(root, *infos);

		for(int i=0;i<infos->get_elements();i++) {
			FileInfo *inf = (*infos)[i];
			list.append(new BrowsableDirEntry(root, inf, true)); // pass ownership of the FileInfo to the browsable object
		}
		delete infos; // deletes the indexed list, but not the FileInfos
		return list.get_elements();
	}

	virtual char *getName() { return "Root"; }

	virtual bool invalidateMatch(void *obj) {
		return false;
/*
		CachedTreeNode *n = (CachedTreeNode *)obj;
		CachedTreeNode *root = FileManager :: getFileManager() -> get_root();
		return (n == root);
*/
	}
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
