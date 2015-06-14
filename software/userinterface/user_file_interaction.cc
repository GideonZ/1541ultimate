/*
 * user_file_interaction.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "user_file_interaction.h"

// member
int UserFileInteraction :: fetch_context_items(FileInfo *info, IndexedList<Action *> &list)
{
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
		list.append(new Action("Rename", UserFileInteraction :: S_rename, info, 0));
	    list.append(new Action("Delete", UserFileInteraction :: S_delete, info, 0));
		count+=2;
        if((info->size <= 262144)&&(!(info->attrib & AM_DIR))) {
            list.append(new Action("View", UserFileInteraction :: S_view, info, 0));
            count++;
        }
	}
	return count;
}

// member
int UserFileInteraction :: fetch_task_items(Path *path, IndexedList<Action*> &list)
{

	if(FileManager :: getFileManager() -> is_path_writable(path)) {
        list.append(new Action("Create D64", UserFileInteraction :: S_createD64, path, 0));
        list.append(new Action("Create G64", UserFileInteraction :: S_createD64, path, (void *)1));
        list.append(new Action("Create Directory", UserFileInteraction :: S_createDir, path, 0));
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
	FileInfo *info = (FileInfo *)obj;
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
	FileInfo *info = (FileInfo *)obj;
    int res = user_interface->popup("Are you sure?", BUTTON_YES | BUTTON_NO);
	if(res == BUTTON_YES) {
		FRESULT fres = FileManager :: getFileManager() -> delete_file_by_info(info);
		if (fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			user_interface->popup(buffer, BUTTON_OK);
		}
	}
}

void UserFileInteraction :: S_view(void *obj, void *param)
{
/*
	FileInfo *info = (FileInfo *)obj;
	File *f = FileManager :: getFileManager() -> fopen_info(info, FA_READ);
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
*/
}

void UserFileInteraction :: S_createD64(void *obj, void *param)
{
	int doG64 = (int)param;
	char buffer[64];

	buffer[0] = 0;
	Path *path = (Path *)obj;
	int res;
	BinImage *bin;
	GcrImage *gcr;
	FileManager *fm = FileManager :: getFileManager();
	bool save_result;

	res = user_interface->string_box("Give name for new disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
	    bin = &static_bin_image; //new BinImage;
		if(bin) {
    		bin->format(buffer);
			set_extension(buffer, (doG64)?(char *)".g64":(char *)".d64", 32);
            File *f = fm -> fopen(path, buffer, FA_WRITE | FA_CREATE_NEW );
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
}

void UserFileInteraction :: S_createDir(void *obj, void *param)
{
	char buffer[64];
	buffer[0] = 0;
	Path *path = (Path *)obj;

	int res = user_interface->string_box("Give name for new directory..", buffer, 22);
	if(res > 0) {
		FileManager *fm = FileManager :: getFileManager();
		FRESULT fres = fm->create_dir_in_path(path, buffer);
		if(fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			user_interface->popup(buffer, BUTTON_OK);
		} else {
    		push_event(e_reload_browser);
		}
	}
}

