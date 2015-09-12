/*
 * user_file_interaction.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "user_file_interaction.h"
#include "userinterface.h"
#include "c1541.h"

// member
int UserFileInteraction :: fetch_context_items(BrowsableDirEntry *br, IndexedList<Action *> &list)
{
	int count = 0;
    if (!br)
    	return 0;

    FileInfo *info = br->getInfo();
    if (!info) {
        printf("No FileInfo structure. Cannot create context menu.\n");
        return 0;
    }
	if(info->attrib & AM_DIR)  {
        list.append(new Action("Enter", UserFileInteraction :: S_enter, 0));
		count++;
	}
	if(info->is_writable()) {
		list.append(new Action("Rename", UserFileInteraction :: S_rename, 0));
	    list.append(new Action("Delete", UserFileInteraction :: S_delete, 0));
		count+=2;
	}
    if((info->size <= 262144)&&(!(info->attrib & AM_DIR))) {
        list.append(new Action("View", UserFileInteraction :: S_view, 0));
        count++;
    }
	return count;
}

int UserFileInteraction :: fetch_task_items(Path *path, IndexedList<Action*> &list)
{

	if(FileManager :: getFileManager() -> is_path_writable(path)) {
        list.append(new Action("Create D64", UserFileInteraction :: S_createD64, 0, 0));
        list.append(new Action("Create G64", UserFileInteraction :: S_createD64, 0, 1));
        list.append(new Action("Create Directory", UserFileInteraction :: S_createDir, 0));
        return 3;
    }
    return 0;
}

int UserFileInteraction :: S_enter(SubsysCommand *cmd)
{
	if(cmd->user_interface) {
		return cmd->user_interface->enterSelection();
	}
	return -1;
}

int UserFileInteraction :: S_rename(SubsysCommand *cmd)
{
	int res;
	char buffer[64];
	FRESULT fres;

	FileManager *fm = FileManager :: getFileManager();

	Path *p = fm->get_new_path("S_rename");
	p->cd(cmd->path.c_str());

	strncpy(buffer, cmd->filename.c_str(), 38);
    buffer[38] = 0;

    res = cmd->user_interface->string_box("Give a new name..", buffer, 38);
    if(res > 0) {
    	fres = fm->rename(p, cmd->filename.c_str(), buffer);
		if(fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			cmd->user_interface->popup(buffer, BUTTON_OK);
		}
    }
    fm->release_path(p);
    return 0;
}

int UserFileInteraction :: S_delete(SubsysCommand *cmd)
{
	char buffer[64];
	FileManager *fm = FileManager :: getFileManager();
    int res = cmd->user_interface->popup("Are you sure?", BUTTON_YES | BUTTON_NO);
    Path *p = fm->get_new_path("S_delete");
    p->cd(cmd->path.c_str());
    if(res == BUTTON_YES) {
		FRESULT fres = FileManager :: getFileManager() -> delete_file(p, cmd->filename.c_str());
		if (fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			cmd->user_interface->popup(buffer, BUTTON_OK);
		}
	}
    fm->release_path(p);
	return 0;
}

int UserFileInteraction :: S_view(SubsysCommand *cmd)
{
	FileManager *fm = FileManager :: getFileManager();
	File *f = 0;
	FRESULT fres = fm -> fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &f);
	uint32_t transferred;
	if(f != NULL) {
    	uint32_t size = f->get_size();
    	char *text_buf = new char[size+1];
        FRESULT fres = f->read(text_buf, size, &transferred);
        printf("Res = %d. Read text buffer: %d bytes\n", fres, transferred);
        text_buf[transferred] = 0;
        cmd->user_interface->run_editor(text_buf);
        delete text_buf;
    }
	return 0;
}

int UserFileInteraction :: S_createD64(SubsysCommand *cmd)
{
	int doG64 = cmd->mode;
	char buffer[64];

	buffer[0] = 0;
	int res;
	BinImage *bin;
	GcrImage *gcr;
	FileManager *fm = FileManager :: getFileManager();
	bool save_result;

	res = cmd->user_interface->string_box("Give name for new disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
	    bin = &static_bin_image; //new BinImage;
		if(bin) {
    		bin->format(buffer);
			set_extension(buffer, (doG64)?(char *)".g64":(char *)".d64", 32);
            File *f = 0;
            FRESULT fres = fm -> fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW, &f);
			if(f) {
                if(doG64) {
                    gcr = new GcrImage;
                    if(gcr) {
                        bin->num_tracks = 40;
                        cmd->user_interface->show_progress("Converting..", 120);
                        gcr->convert_disk_bin2gcr(bin, cmd->user_interface);
                        cmd->user_interface->update_progress("Saving...", 0);
                        save_result = gcr->save(f, false, cmd->user_interface); // create image, without alignment, we are aligned already
                        cmd->user_interface->hide_progress();
                    } else {
                        printf("No memory to create gcr image.\n");
                        return -3;
                    }
                } else {
                    cmd->user_interface->show_progress("Creating D64..", 35);
                    save_result = bin->save(f, cmd->user_interface);
                    cmd->user_interface->hide_progress();
                }
        		printf("Result of save: %d.\n", save_result);
                fm->fclose(f);
			} else {
				printf("Can't create file '%s'\n", buffer);
				cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
				return -2;
			}
			// delete bin;
		} else {
			printf("No memory to create bin.\n");
			return -1;
		}
	}
	return 0;
}

int UserFileInteraction :: S_createDir(SubsysCommand *cmd)
{
	char buffer[64];
	buffer[0] = 0;

	FileManager *fm = FileManager :: getFileManager();
	Path *path = fm->get_new_path("createDir");
	path->cd(cmd->path.c_str());

	int res = cmd->user_interface->string_box("Give name for new directory..", buffer, 22);
	if(res > 0) {
		FRESULT fres = fm->create_dir(path, buffer);
		if(fres != FR_OK) {
			sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
			cmd->user_interface->popup(buffer, BUTTON_OK);
		}
	}
	fm->release_path(path);
	return 0;
}
