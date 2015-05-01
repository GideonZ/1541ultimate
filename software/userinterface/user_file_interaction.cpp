/*
 * user_file_interaction.cpp
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "user_file_interaction.h"

UserFileInteraction::UserFileInteraction() {
	// TODO Auto-generated constructor stub

}

UserFileInteraction::~UserFileInteraction() {
	// TODO Auto-generated destructor stub
}

int FileDirEntry :: fetch_context_items(IndexedList<CachedTreeNode *> &list)
{
    FileDirEntry *promoted = attempt_promotion();
    if(promoted) {
        return promoted->fetch_context_items(list);
    }
    // promotion failed.. let's just return our own context
    return fetch_context_items_actual(list);
}

int FileDirEntry :: fetch_context_items_actual(IndexedList<CachedTreeNode *> &list)
{
	int count = 0;
	FileInfo *info = get_file_info(); // parent-> removed!
    if (!info) {
        printf("No FileInfo structure. Cannot create context menu.\n");
        return 0;
    }

	if(info->attrib & AM_DIR) {
        list.append(new MenuItem(this, "Enter", FILEDIR_ENTERDIR));
		count++;
	}
	if(info && info->is_writable()) {
		list.append(new MenuItem(this, "Rename", FILEDIR_RENAME));
	    list.append(new MenuItem(this, "Delete", FILEDIR_DELETE));
		count+=2;
        if((info->size <= 262144)&&(!(info->attrib & AM_DIR))) {
            list.append(new MenuItem(this, "View",   FILEDIR_VIEW));
            count++;
        }
	}
/*
    list.append(new MenuItem(this, "Dump FileInfo", MENU_DUMP_INFO));
    list.append(new MenuItem(this, "Dump PathObject", MENU_DUMP_OBJECT));
    count += 2;
*/

	return count;
}

int FileDirEntry :: fetch_task_items(IndexedList<CachedTreeNode*> &list)
{
    if(is_writable()) {
        list.append(new MenuItem(this, "Create D64", MENU_CREATE_D64));
        list.append(new MenuItem(this, "Create G64", MENU_CREATE_G64));
        list.append(new MenuItem(this, "Create Directory", MENU_CREATE_DIR));
        return 3;
    }
    return 0;
}

/* Context menu execution dispatcher */
void FileDirEntry :: execute(int selection)
{
    int res;
    char buffer[40];
    BinImage *bin;
    GcrImage *gcr;
    int save_result;
    FRESULT fres;
    File *f;
    UINT transferred = 0;

    printf("FileDirEntry Execute. option=%4x\n", selection);
    switch(selection) {
        case FILEDIR_RENAME:
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
            break;
        case FILEDIR_DELETE:
            res = user_interface->popup("Are you sure?", BUTTON_YES | BUTTON_NO);
			if(res == BUTTON_YES) {
				push_event(e_invalidate, this);
				push_event(e_path_object_exec_cmd, this, FILEDIR_DELETE_CONTINUED);
			}
			break;

		case FILEDIR_DELETE_CONTINUED:
			fres = info->fs->file_delete(info);
			if(fres != FR_OK) {
				sprintf(buffer, "Error: %s", FileSystem :: get_error_string(fres));
				user_interface->popup(buffer, BUTTON_OK);
			} else {
				if(!parent->children.remove(this)) {
					printf("ERROR: Couldn't remove child from list!!\n");
				} else {
					detach(true);
					push_event(e_cleanup_path_object, this);
					push_event(e_refresh_browser);
				}
			}
            break;

		case FILEDIR_ENTERDIR:
			push_event(e_browse_into);
			break;

        case FILEDIR_VIEW:
            f = info->fs->file_open(info, FA_READ);
            if(f != NULL) {
                char *text_buf = new char[info->size+1];
                fres = info->fs->file_read(f, text_buf, info->size, &transferred);
                printf("Res = %d. Read text buffer: %d bytes\n", fres, transferred);
                text_buf[transferred] = 0;
                // printf(text_buf);
                user_interface->run_editor(text_buf);
                delete text_buf;
            }
            break;

        case MENU_DUMP_INFO:
            info->print_info();
            break;

        case MENU_DUMP_OBJECT:
            dump();
            break;

        case MENU_CREATE_D64:
        case MENU_CREATE_G64:
        	buffer[0] = 0;
        	if(!(info->attrib & AM_DIR)) {
        		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
        		break;
        	}
        	res = user_interface->string_box("Give name for new disk..", buffer, 22);
        	if(res > 0) {
    			fix_filename(buffer);
    		    bin = &static_bin_image; //new BinImage;
        		if(bin) {
            		bin->format(buffer);
        			set_extension(buffer, (selection == MENU_CREATE_G64)?(char *)".g64":(char *)".d64", 32);
                    File *f = root.fcreate(buffer, this);
        			if(f) {
                        if(selection == MENU_CREATE_G64) {
                            gcr = new GcrImage;
                            if(gcr) {
                                bin->num_tracks = 40;
                                user_interface->show_status("Converting..", 120);
                                gcr->convert_disk_bin2gcr(bin, true);
                                user_interface->update_status("Saving...", 0);
                                save_result = gcr->save(f, true, false); // create image, without alignment, we are aligned already
                                user_interface->hide_status();
                            } else {
                                printf("No memory to create gcr image.\n");
                            }
                        } else {
                            user_interface->show_status("Creating D64..", 35);
                            save_result = bin->save(f, true);
                            user_interface->hide_status();
                        }
                		printf("Result of save: %d.\n", save_result);
                        root.fclose(f);
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
        	break;

        case MENU_CREATE_DIR:
        	buffer[0] = 0;
        	if(!(info->attrib & AM_DIR)) {
        		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
        		break;
        	}
        	res = user_interface->string_box("Give name for new directory..", buffer, 22);
        	if(res > 0) {
        		FileInfo *fi = new FileInfo(info, buffer);
        		fi->attrib = 0;
				fres = fi->fs->dir_create(fi);
			    printf("Result of mkdir: %d\n", fres);
				if(fres != FR_OK) {
					printf("Can't create directory '%s'\n", buffer);
					user_interface->popup("Can't create directory.", BUTTON_OK);
				} else {
            		push_event(e_reload_browser);
				}
				delete fi;
				printf("Done!\n");
        	}
        	break;
        default:
        	CachedTreeNode :: execute(selection);
            break;
    }
}

