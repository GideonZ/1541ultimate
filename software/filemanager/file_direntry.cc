extern "C" {
    #include "small_printf.h"
}
#include "filemanager.h"
#include "file_system.h"
#include "directory.h"
#include "file_direntry.h"
#include "size_str.h"
#include "event.h"
#include "userinterface.h"
#include "c1541.h"


/* Global Instance of file type factory */
FileTypeFactory file_type_factory;



FileDirEntry :: FileDirEntry(PathObject *par, char *name) : PathObject(par, name)
{
    info = NULL;
}
   

FileDirEntry :: FileDirEntry(PathObject *par, FileInfo *i) : PathObject(par)
{
    // store info here that we need later..
    if(i) {
//        printf("Creating FileDirEntry %s\n", i->lfname);
        info = new FileInfo(*i);
    } else {
    	info = NULL;
    }
}

FileDirEntry :: ~FileDirEntry()
{
	if(info) {
//        printf("Destroying FileDirEntry %s\n", info->lfname);
        delete info;
    }
}

FileDirEntry *FileDirEntry :: test_type(PathObject *obj)
{
    // we never need to promote to the same type of class
    return NULL;
}

FileInfo *FileDirEntry :: get_file_info(void)
{
	return info;
}

bool FileDirEntry :: is_writable(void)
{
    return (info->is_directory() && info->fs->is_writable());
}

FileDirEntry *FileDirEntry :: attempt_promotion(void)
{    
    FileDirEntry *promoted = file_type_factory.promote(this);
    if(promoted) {
        printf("Promotion success! %s\n", promoted->get_name());
        parent->children.replace(this, promoted); // replace
		detach(true);
		promoted->attach(true);
		//        promoted->parent = parent;
        push_event(e_cleanup_path_object, this);
    }
    return promoted;
}

int FileDirEntry :: fetch_children(void)
{
    cleanup_children();
    int remaining_children = children.get_elements();

    FileInfo fi(32);    
	FRESULT fres;
	
    if(info->is_directory()) {
        printf("Opening dir %s.\n", info->lfname);
        info->print_info();
        Directory *r = info->fs->dir_open(info);
        printf("Directory = %p\n", r);
		if(!r) {
			printf("Error opening directory!\n");
			return -1;
		}
        int i=0;        
        while((fres = r->get_entry(fi)) == FR_OK) {
            printf(".");
			if((fi.lfname[0] != '.') && !(fi.attrib & AM_HID)) {
	            children.append(new FileDirEntry(this, &fi));
	            ++i;
			}
        }
        //printf("close");
        info->fs->dir_close(r);
        //printf("sort");
        sort_children();
        //printf("dup");
        remove_duplicates(); // should be easy!!
        return i;
    } else {
        // Filetype factory->transform 
        FileDirEntry *promoted = attempt_promotion();
        if(promoted) {
            return promoted->fetch_children();
        } else {
            return -1;
        }
    }
}

int FileDirEntry :: fetch_context_items(IndexedList<PathObject *> &list)
{
    FileDirEntry *promoted = attempt_promotion();
    if(promoted) {
        return promoted->fetch_context_items(list);
    }
    // promotion failed.. let's just return our own context    
    return fetch_context_items_actual(list);
}

int FileDirEntry :: fetch_context_items_actual(IndexedList<PathObject *> &list)
{
	int count = 0;
	FileInfo *info = get_file_info(); // parent-> removed!

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

int FileDirEntry :: fetch_task_items(IndexedList<PathObject*> &list)
{
    if(is_writable()) {
        list.append(new MenuItem(this, "Create D64", MENU_CREATE_D64));
        list.append(new MenuItem(this, "Create G64", MENU_CREATE_G64));
        list.append(new MenuItem(this, "Create Directory", MENU_CREATE_DIR));
        return 3;
    }
    return 0;
}


char *FileDirEntry :: get_name()
{
    if(info)
        return info->lfname;
    return PathObject :: get_name();
}

char *FileDirEntry :: get_display_string()
{
    static char buffer[44];
    static char sizebuf[8];
    
    if(!info)
        return "FileDirEntry_display";

    if(info->is_directory()) {
        small_sprintf(buffer, "%29s\032 DIR", info->lfname);
    } else {
        size_to_string_bytes(info->size, sizebuf);
        small_sprintf(buffer, "%29s\027 %3s %s", info->lfname, info->extension, sizebuf);
    }
    
    return buffer;
}

/* Context menu execution dispatcher */
void FileDirEntry :: execute(int selection)
{
    int res;
    char buffer[40];
    BinImage *bin;
    GcrImage *gcr;
    bool save_result;
    FRESULT fres;
    File *f;
    UINT transferred = 0;
    
    printf("FileDirEntry Execute option %4x\n", selection);
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
    		    bin = new BinImage;
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
        			delete bin;
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
            PathObject :: execute(selection);
            break;
    }
}

int FileDirEntry :: compare(PathObject *obj)
{
	FileDirEntry *b = (FileDirEntry *)obj;

	if ((info->attrib & AM_DIR) && !(b->info->attrib & AM_DIR))
		return -1;
	if (!(info->attrib & AM_DIR) && (b->info->attrib & AM_DIR))
		return 1;

    int by_name = stricmp(get_name(), b->get_name());
    if(by_name)
	    return by_name;
	return b->get_ref_count() - get_ref_count(); // for duplicates! (locked item will be first)
}

void FileDirEntry :: remove_duplicates(void)
{
    int count = children.get_elements();
    PathObject *p, *n;
    
    for(int i=0;i<count;i++) {
        p = children[i];
        if(p->get_ref_count()) { // locked?
            n = children[i+1];
            if(n) { // should exist, but just to be sure and avoid crashing
                delete n;
                children.remove(n);
                count--;
            }
        }
    }
}
    
