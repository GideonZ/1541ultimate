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
	Browsable *parent;
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
	BrowsableDirEntry(Path *pp, Browsable *parent, FileInfo *info, bool sel) {
		this->info = info;
		this->type = NULL;
		this->selectable = sel;
		this->path = 0;
		this->parent = parent;
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

	FileInfo *getInfo(void) {
		return this->info;
	}

	Path *getPath(void) {
		return parent_path;
	}

	Browsable *getParent(void) {
		return parent;
	}

	virtual IndexedList<Browsable *> *getSubItems(int &error) {
		if (children.get_elements() != 0) {
			error = 0;
			return &children; // cached version OK
		}

		setPath();
		IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
		if (path->get_directory(*infos) != FR_OK) {
			delete infos;
			error = -1;
		} else {
			for(int i=0;i<infos->get_elements();i++) {
				FileInfo *inf = (*infos)[i];
				children.append(new BrowsableDirEntry(path, this, inf, !(inf->attrib & AM_VOL))); // pass ownership of the FileInfo to the browsable object
			}
			delete infos; // deletes the indexed list, but not the FileInfos
		}
		return &children;
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
};

class BrowsableRoot : public Browsable
{
	Path *root;
	FileManager *fm;
public:
	BrowsableRoot()  {
		fm = FileManager :: getFileManager();
		root = fm -> get_new_path("Browsable Root");
	}
	virtual ~BrowsableRoot() {
		fm -> release_path(root);
	}

	// get parent function not implemented; there is no parent, see base class

	virtual IndexedList<Browsable *> *getSubItems(int &error) {
		printf("ROOT GET SUBITEMS\n");
		if (children.get_elements() == 0) {
			IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
			fm -> get_directory(root, *infos);
			// printf("Root get sub items: get_directory of %s returned %d elements.\n", root->get_path(), infos->get_elements());
			for(int i=0;i<infos->get_elements();i++) {
				FileInfo *inf = (*infos)[i];
				children.append(new BrowsableDirEntry(root, this, inf, true)); // pass ownership of the FileInfo to the browsable object
			}
			delete infos; // deletes the indexed list, but not the FileInfos
		}
		error = 0;
		printf("ROOT GET SUBITEMS DONE\n");
		return &children;
	}

	virtual const char *getName() { return "*Root*"; }
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
