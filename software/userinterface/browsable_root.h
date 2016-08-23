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
#include "size_str.h"
#include "user_file_interaction.h"
#include "network_interface.h"

class BrowsableNetwork : public Browsable
{
	Browsable *parent;
	int index;
public:
	BrowsableNetwork(Browsable *parent, int index) {
		this->parent = parent;
		this->index = index;
	}

	void getDisplayString(char *buffer, int width) {
		uint8_t mac[6];
		char ip[16];

		NetworkInterface *ni = NetworkInterface :: getInterface(index);
		ni->getMacAddr(mac);
		if (ni->is_link_up()) {
			sprintf(buffer, "Net%d    IP: %17s  \eELink Up", index, ni->getIpAddrString(ip, 16));
		} else {
			sprintf(buffer, "Net%d    MAC %b:%b:%b:%b:%b:%b  \eJLink Down", index, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
	}

	IndexedList<Browsable *> *getSubItems(int &error) {
		error = -1;
		return &children;
	}
};

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
			char sel = getSelection() ? '\x13': ' ';
			if (info->attrib & AM_VOL) {
				sprintf(buffer, "\eR%#s\er VOLUME", width-11, info->lfname);
			} else if(info->is_directory()) {
				sprintf(buffer, "%#s\eJ DIR%c", width-11, info->lfname, sel);
			} else {
				size_to_string_bytes(info->size, sizebuf);
				sprintf(buffer, "%#s\e7 %3s%c%s", width-11, info->lfname, info->extension, sel, sizebuf);
			}
		}
	}

	virtual void fetch_context_items(IndexedList<Action *>&items) {
		if (!type)
			type = FileType :: getFileTypeFactory()->create(this);
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
		UserFileInteraction :: getUserFileInteractionObject(); // just to make sure the UserFileInteraction class has been initialized
	}
	virtual ~BrowsableRoot() {
		fm -> release_path(root);
	}

	// get parent function not implemented; there is no parent, see base class

	virtual IndexedList<Browsable *> *getSubItems(int &error) {
		if (children.get_elements() == 0) {
			IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
			fm -> get_directory(root, *infos);
			// printf("Root get sub items: get_directory of %s returned %d elements.\n", root->get_path(), infos->get_elements());
			for(int i=0;i<infos->get_elements();i++) {
				FileInfo *inf = (*infos)[i];
				children.append(new BrowsableDirEntry(root, this, inf, true)); // pass ownership of the FileInfo to the browsable object
			}
			delete infos; // deletes the indexed list, but not the FileInfos

			for(int i=0; i < NetworkInterface :: getNumberOfInterfaces(); i++) {
				children.append(new BrowsableNetwork(this, i));
			}
		}
		error = 0;
		return &children;
	}

	virtual const char *getName() { return "*Root*"; }
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
