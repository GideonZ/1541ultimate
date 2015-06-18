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

	void setPath(void) {
		if (!path) {
			path = FileManager :: getFileManager() -> get_new_path(info->lfname);
			path->cd(parent_path->get_path());
			path->cd(info->lfname);
		}
	}
public:
	BrowsableDirEntry(Path *pp, FileInfo *info, bool sel) {
		this->info = info;
		this->type = NULL;
		this->selectable = sel;
		this->path = 0;
		this->parent_path = pp;
	}

	FileInfo *getInfo(void) {
		return this->info;
	}

	Path *getPath(void) {
		return parent_path;
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
		setPath();
		IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
		path->get_directory(*infos);

		for(int i=0;i<infos->get_elements();i++) {
			FileInfo *inf = (*infos)[i];
			list.append(new BrowsableDirEntry(path, inf, !(inf->attrib & AM_VOL))); // pass ownership of the FileInfo to the browsable object
		}
		delete infos; // deletes the indexed list, but not the FileInfos
		return list.get_elements();
	}

	virtual const char *getName() {
		return info->lfname;
	}

	virtual void getDisplayString(char *buffer, int width) {
		static char sizebuf[8];
		if (info->special_display) {
			parent_path->get_display_string(info->lfname, buffer, width);
		} else {
			if (info->attrib & AM_VOL) {
				sprintf(buffer, "\eR%#s\er VOLUME", width-11, info->lfname);
			} else if(info->is_directory()) {
				sprintf(buffer, "%#s\eJ DIR", width-11, info->lfname);
			} else {
				size_to_string_bytes(info->size, sizebuf);
				sprintf(buffer, "%#s\e7 %3s %s", width-11, info->lfname, info->extension, sizebuf);
			}
		}
	}

	virtual void fetch_context_items(IndexedList<Action *>&items) {
		if (!type)
			type = Globals :: getFileTypeFactory()->create(this);
		if (type) {
			type->fetch_context_items(items);
		}

		UserFileInteraction :: getUserFileInteractionObject() -> fetch_context_items(this, items);
	}

	virtual bool checkValid() {
		setPath();
		return path->isValid();
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

	virtual const char *getName() { return "*Root*"; }

	virtual bool checkValid() {
		return true;
	}
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
