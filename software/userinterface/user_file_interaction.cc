/*
 * user_file_interaction.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "user_file_interaction.h"

// member
int UserFileInteraction :: fetch_context_items(CachedTreeNode *node, IndexedList<Action *> &list)
{
	FileInfo *info = node->get_file_info();
	int count = 0;
    if (!info) {
        printf("No FileInfo structure. Cannot create context menu.\n");
        return 0;
    }

	if(info->attrib & AM_DIR)  {
        list.append(new Action("Enter", UserFileInteraction :: S_enter, 0, 0));
		count++;
	}
	if(info->is_writable()) {
		list.append(new Action("Rename", UserFileInteraction :: S_rename, node, 0));
	    list.append(new Action("Delete", UserFileInteraction :: S_delete, node, 0));
		count+=2;
        if((info->size <= 262144)&&(!(info->attrib & AM_DIR))) {
            list.append(new Action("View", UserFileInteraction :: S_view, node, 0));
            count++;
        }
	}
	return count;
}

// member
int UserFileInteraction :: fetch_task_items(CachedTreeNode *node, IndexedList<Action*> &list)
{
    if(node->get_file_info()->is_writable()) {
        list.append(new Action("Create D64", UserFileInteraction :: S_createD64, node, 0));
        list.append(new Action("Create G64", UserFileInteraction :: S_createD64, node, (void *)1));
        list.append(new Action("Create Directory", UserFileInteraction :: S_createDir, node, 0));
        return 3;
    }
    return 0;
}

void UserFileInteraction :: S_enter(void *obj, void *param)
{
	push_event(e_browse_into);
}

void UserFileInteraction :: S_rename(void *obj, void *param)
{
	int res;
	char buffer[64];
	FRESULT fres;
	CachedTreeNode *node = (CachedTreeNode *)obj;
	FileInfo *info = node->get_file_info();
	strncpy(buffer, info->lfname, 38);
    buffer[38] = 0;

    res = user_interface->string_box("Give a new name..", buffer, 38);
    if(res > 0) {
    	fres = info->fs->file_rename(info, buffer);
		if(fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			user_interface->popup(buffer, BUTTON_OK);
		} else {
    		push_event(e_refresh_browser);
		}
    }
}

void UserFileInteraction :: S_delete(void *obj, void *param)
{
	char buffer[64];
	CachedTreeNode *node = (CachedTreeNode *)obj;
    int res = user_interface->popup("Are you sure?", BUTTON_YES | BUTTON_NO);
	if(res == BUTTON_YES) {
		FRESULT fres = FileManager :: getFileManager() -> delete_file_by_node(node);
		if (fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			user_interface->popup(buffer, BUTTON_OK);
		}
	}
}

void UserFileInteraction :: S_view(void *obj, void *param)
{
	CachedTreeNode *node = (CachedTreeNode *)obj;
	File *f = FileManager :: getFileManager() -> fopen_node(node, FA_READ);
	uint32_t transferred;
	if(f != NULL) {
    	uint32_t size = f->get_size();
    	char *text_buf = new char[size+1];
        FRESULT fres = f->read(text_buf, size, &transferred);
        printf("Res = %d. Read text buffer: %d bytes\n", fres, transferred);
        text_buf[transferred] = 0;
        user_interface->run_editor(text_buf);
        delete text_buf;
    }
}

void UserFileInteraction :: S_createD64(void *obj, void *param)
{
	int doG64 = (int)param;
	char buffer[64];

	buffer[0] = 0;
	CachedTreeNode *node = (CachedTreeNode *)obj;
	FileInfo *info = node->get_file_info();
	int res;
	BinImage *bin;
	GcrImage *gcr;
	FileManager *fm = FileManager :: getFileManager();
	bool save_result;

	if(!(info->attrib & AM_DIR)) {
		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
		return;
	}

	Path *currentPath = fm->get_new_path("S_createD64");
	mstring work;
	currentPath->cd(node->get_full_path(work));

	res = user_interface->string_box("Give name for new disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
	    bin = &static_bin_image; //new BinImage;
		if(bin) {
    		bin->format(buffer);
			set_extension(buffer, (doG64)?(char *)".g64":(char *)".d64", 32);
            File *f = FileManager :: getFileManager() -> fopen(currentPath, buffer, FA_WRITE | FA_CREATE_NEW );
			if(f) {
                if(doG64) {
                    gcr = new GcrImage;
                    if(gcr) {
                        bin->num_tracks = 40;
                        user_interface->show_progress("Converting..", 120);
                        gcr->convert_disk_bin2gcr(bin, true);
                        user_interface->update_progress("Saving...", 0);
                        save_result = gcr->save(f, true, false); // create image, without alignment, we are aligned already
                        user_interface->hide_progress();
                    } else {
                        printf("No memory to create gcr image.\n");
                    }
                } else {
                    user_interface->show_progress("Creating D64..", 35);
                    save_result = bin->save(f, true);
                    user_interface->hide_progress();
                }
        		printf("Result of save: %d.\n", save_result);
                fm->fclose(f);
        		push_event(e_reload_browser);
			} else {
				printf("Can't create file '%s'\n", buffer);
				user_interface->popup("Can't create file.", BUTTON_OK);
			}
			// delete bin;
		} else {
			printf("No memory to create bin.\n");
		}
	}
	fm->release_path(currentPath);
}

void UserFileInteraction :: S_createDir(void *obj, void *param)
{
	char buffer[64];
	buffer[0] = 0;
	CachedTreeNode *node = (CachedTreeNode *)obj;
	FileInfo *info = node->get_file_info();

	if(!(info->attrib & AM_DIR)) {
		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
		return;
	}

	int res = user_interface->string_box("Give name for new directory..", buffer, 22);
	if(res > 0) {
		FileManager *fm = FileManager :: getFileManager();
		FRESULT fres = fm->create_dir_in_node(node, buffer);
		if(fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			user_interface->popup(buffer, BUTTON_OK);
		} else {
    		push_event(e_reload_browser);
		}
	}
}

