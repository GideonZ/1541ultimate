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
#include "assembly_search.h"

class BrowsableNetwork : public Browsable
{
    Browsable *parent;
    int index;

  public:
    BrowsableNetwork(Browsable *parent, int index)
    {
        this->parent = parent;
        this->index = index;
    }

    const char *getName()
    {
        NetworkInterface *ni = NetworkInterface ::getInterface(index);
        if (ni) {
            return ni->identify();
        }
        return "Unknown Network";
    }

    void getDisplayString(char *buffer, int width)
    {
        NetworkInterface *ni = NetworkInterface ::getInterface(index);
        if (!ni) {
            sprintf(buffer, "Net%d%#s\eJRemoved", index, width - 17, "");
            return;
        }
        ni->getDisplayString(index, buffer, width);
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        NetworkInterface *ni = NetworkInterface ::getInterface(index);
        if (!ni) {
            error = -1;
            return &children;
        }
        ni->getSubItems(this, children, error);
        return &children;
    }

    void fetch_context_items(IndexedList<Action *> &items)
    {
        NetworkInterface *ni = NetworkInterface ::getInterface(index);
        if (ni) {
            ni->fetch_context_items(items);
        }
    }
};

class BrowsableDirEntry : public Browsable
{
	Browsable *parent;
	FileInfo *info;
	FileType *type;
	char *fatname;
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
		this->fatname = NULL;
	}

	virtual ~BrowsableDirEntry() {
		if (type)
			delete type;
		if (info)
			delete info;
		if (fatname)
		    delete fatname;
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

		if (type) {
		    if (type->getCustomBrowsables(this, children) >= 0) {
		        error = 0;
		        return &children; // filetype returned custom browsables
		    }
		}

		setPath();
		IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
		if (FileManager :: getFileManager()->get_directory(path, *infos, NULL) != FR_OK) {
			delete infos;
			error = -1;
		} else {
		    error = 0;
			for(int i=0;i<infos->get_elements();i++) {
				FileInfo *inf = (*infos)[i];
				bool selectable = true;
				if (inf->attrib & AM_VOL) {
					selectable = false;
				}
				if (inf->attrib & AM_DIR) {
					selectable = true;
				}
				children.append(new BrowsableDirEntry(path, this, inf, selectable)); // pass ownership of the FileInfo to the browsable object
			}
			delete infos; // deletes the indexed list, but not the FileInfos
		}
		return &children;
	}

	virtual const char *getName() {
		if (fatname) {
		    return fatname;
		}
	    if (!info) {
		    return "No info!";
		}
		if (info->name_format & NAME_FORMAT_CBM) {
		    fatname = new char[48];
		    info->generate_fat_name(fatname, 48);
		    return fatname;
		}
	    return info->lfname;
	}

	int squeezeToDisplayString(char *string_to_squeeze, char *squeezed_string, int max_width, int squeeze_quarter = 0) {
		int len = strlen(string_to_squeeze);

		if (squeeze_quarter < 0 || squeeze_quarter > 3) {
			squeeze_quarter = 0;
		}

		if (len <= max_width || max_width <= 10 || squeeze_quarter == 0) {
			// We can fit into available space or its too short anyways or no squeeze
			strncpy(squeezed_string, string_to_squeeze, max_width);
		} else {
			// Need to squeeze the string
			int remainder = (max_width * squeeze_quarter) % 4;
			int cut_off_point = (max_width * squeeze_quarter) / 4; // Where's our cut-off point
			int tail_length = max_width - cut_off_point; // How much needs to be copied after cut-off
			strncpy(squeezed_string, string_to_squeeze, cut_off_point); // Copy the beginning
			strncpy(squeezed_string + cut_off_point, string_to_squeeze + len - tail_length, tail_length); // Copy the end

			squeezed_string[cut_off_point] = '~';
		}
        // strcat(squeezed_string, "\er");
        // return 2;
        return 0;
	}

	virtual void getDisplayString(char *buffer, int width, int squeeze_option = 0) {
		static char sizebuf[8];
        int extra;
		if (info->name_format & NAME_FORMAT_DIRECT) {
			FileManager::getFileManager()->get_display_string(parent_path, info->lfname, buffer, width);
		} else {
			int display_space = width - 11;
			char tmp_buffer[display_space + 5];
			memset(tmp_buffer, '\0', display_space * sizeof(char));

			char sel = getSelection() ? '\x13' : ' ';
			if (info->is_directory()) {
				extra = squeezeToDisplayString(info->lfname, tmp_buffer, display_space, squeeze_option);
				sprintf(buffer, "%#s\eJ DIR%c", display_space + extra, tmp_buffer, sel); // FIXME
			} else if (info->attrib & AM_VOL) {
				extra = squeezeToDisplayString(info->lfname, tmp_buffer, display_space, squeeze_option);
				sprintf(buffer, "\eR%#s\er VOLUME", display_space + extra, tmp_buffer);
			} else {
				size_to_string_bytes(info->size, sizebuf);
				extra = squeezeToDisplayString(info->lfname, tmp_buffer, display_space, squeeze_option);
				sprintf(buffer, "%#s\e7 %3s%c%s", display_space + extra, tmp_buffer,
						info->extension, sel, sizebuf);
			}
		}
	}

	virtual void getDisplayString(char *buffer, int width) {
		getDisplayString(buffer, width, 0);
	}

	virtual void fetch_context_items(IndexedList<Action *>&items) {
		if ((!type) && (!(this->info->attrib & AM_DIR)))
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
			fm -> get_directory(root, *infos, NULL);
			// printf("Root get sub items: get_directory of %s returned %d elements.\n", root->get_path(), infos->get_elements());
			for(int i=0;i<infos->get_elements();i++) {
				FileInfo *inf = (*infos)[i];
				children.append(new BrowsableDirEntry(root, this, inf, true)); // pass ownership of the FileInfo to the browsable object
			}
			delete infos; // deletes the indexed list, but not the FileInfos

			// for(int i=0; i < NetworkInterface :: getNumberOfInterfaces(); i++) {
			// 	children.append(new BrowsableNetwork(this, i));
			// }
		}
		error = 0;
		return &children;
	}

	virtual const char *getName() { return "*Root*"; }
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
