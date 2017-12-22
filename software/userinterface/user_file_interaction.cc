/*
 * user_file_interaction.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "user_file_interaction.h"
#include "userinterface.h"
#include "c1541.h"

#include "c64.h"

#include "home_directory.h"

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
    if((info->size <= 262144)&&(!(info->attrib & AM_DIR))) {
        list.append(new Action("View", UserFileInteraction :: S_view, 0));
        count++;
    }
	if(info->is_writable()) {
		list.append(new Action("Rename", UserFileInteraction :: S_rename, 0));
	    list.append(new Action("Delete", UserFileInteraction :: S_delete, 0));
		count+=2;
	}

    bool showRunWithApp = false;
#ifndef RECOVERYAPP
        const char* app_directory = HomeDirectory :: getHomeDirectory();
	char appname[100];
	strcpy(appname, info->extension);
	strcat(appname, ".prg");
	
	FileManager *fm = FileManager :: getFileManager();
	File *file = 0;
        FRESULT fres = fm->fopen(app_directory, appname, FA_READ, &file);
        if (file)
	{
	   fm->fclose(file);
	   showRunWithApp = true;
	}
#endif

    if((info->size <= 16*1024*1024-4)&&(!(info->attrib & AM_DIR)) && showRunWithApp) {
        list.append(new Action("Run with App", UserFileInteraction :: S_runApp, 0));
        count++;
    }
	return count;
}

int UserFileInteraction :: fetch_task_items(Path *path, IndexedList<Action*> &list)
{

	if(FileManager :: getFileManager() -> is_path_writable(path)) {
        list.append(new Action("Create Directory", UserFileInteraction :: S_createDir, 0));
        return 1;
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

int UserFileInteraction :: S_runApp(SubsysCommand *cmd)
{
#ifndef RECOVERYAPP
	printf("REU Select: %4x\n", cmd->functionID);
	File *file = 0;
	uint32_t bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    uint32_t total_bytes_read;
    int remain;
    
	static char buffer[48];
    uint8_t *dest;
    FileManager *fm = FileManager :: getFileManager();
    FileInfo info(32);
    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), info);


    sectors = (info.size >> 9);
	if(sectors >= 128)
		progress = true;
	secs_per_step = (sectors + 31) >> 5;
	bytes_per_step = secs_per_step << 9;
	remain = info.size;

	printf("REU Load.. %s\nUI = %p", cmd->filename.c_str(), cmd->user_interface);
	if (!cmd->user_interface)
		progress = false;

	FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
	if(file) {
		total_bytes_read = 0;
		// load file in REU memory
		if(progress) {
			cmd->user_interface->show_progress("Loading REU file..", 32);
			dest = (uint8_t *)(REU_MEMORY_BASE+4);
			while(remain > 0) {
				file->read(dest, bytes_per_step, &bytes_read);
				total_bytes_read += bytes_read;
				cmd->user_interface->update_progress(NULL, 1);
				remain -= bytes_per_step;
				dest += bytes_per_step;
			}
			cmd->user_interface->hide_progress();
		} else {
			file->read((void *)(REU_MEMORY_BASE+4), (REU_MAX_SIZE-4), &bytes_read);
			total_bytes_read += bytes_read;
		}
		printf("\nClosing file. ");
		fm->fclose(file);
		file = NULL;
		printf("done.\n");
		*(uint8_t *)(REU_MEMORY_BASE) = total_bytes_read & 0xff;
		*(uint8_t *)(REU_MEMORY_BASE+1) = (total_bytes_read >> 8) & 0xff;
		*(uint8_t *)(REU_MEMORY_BASE+2) = (total_bytes_read >> 16) & 0xff;
		*(uint8_t *)(REU_MEMORY_BASE+3) = (total_bytes_read >> 24) & 0xff;
		
		char appname[100];
		char filen[100];
		strcpy(filen, cmd->filename.c_str());
		char* extension = filen;
		char* tmp = filen;
		while (*tmp)
		{
		   if (*tmp == '.')
		      extension = tmp+1;
	           tmp++;
		}
		extension[3]=0;
		
		strcpy(appname, extension);
		strcat(appname, ".prg");

		// sprintf(buffer, "Bytes loaded: %d ($%8x)", total_bytes_read, total_bytes_read);
		// cmd->user_interface->popup(buffer, BUTTON_OK);

                const char* app_directory = HomeDirectory :: getHomeDirectory(); // cmd->user_interface->cfg->get_string(CFG_USERIF_HOME_DIR);
            	SubsysCommand* c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD, RUNCODE_DMALOAD_RUN, app_directory, appname);
        	c64_command->execute();
	} else {
		printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
		return -2;
	}
#endif
	return 0;
}
