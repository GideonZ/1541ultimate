#include "filemanager.h"
#include "file_system.h"
#include "directory.h"
#include "file_direntry.h"
#include "small_printf.h"
#include "size_str.h"
#include "event.h"
#include "userinterface.h"
#include "c1541.h"

#define FILEDIR_RENAME   			0x2001
#define FILEDIR_DELETE   			0x2002
#define FILEDIR_ENTERDIR 			0x2003
#define FILEDIR_DELETE_CONTINUED   	0x2004

#define MENU_CREATE_D64    0x3001
#define MENU_CREATE_DIR    0x3002

/* Debug options */
#define MENU_DUMP_INFO     0x30FE
#define MENU_DUMP_OBJECT   0x30FF

/* Global Instance of file type factory */
FileTypeFactory file_type_factory;



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

    FileInfo fi(32);    
	FRESULT fres;
	
    if(info->is_directory()) {
        printf("Opening dir %s.\n", info->lfname);
        Directory *r = info->fs->dir_open(info);
		if(!r) {
			printf("Error opening directory!\n");
			return -1;
		}
        int i=0;        
        while((fres = r->get_entry(fi)) == FR_OK) {
			if(fi.lfname[0] != '.') {
	            children.append(new FileDirEntry(this, &fi));
	            ++i;
			}
        }
        info->fs->dir_close(r);
        sort_children();
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
	if(info->attrib & AM_DIR) {
		list.append(new MenuItem(this, "Enter", FILEDIR_ENTERDIR));
		count++;
	}
	if(parent->get_file_info()) {
		list.append(new MenuItem(this, "Rename", FILEDIR_RENAME));
	    list.append(new MenuItem(this, "Delete", FILEDIR_DELETE));
		count+=2;
	}
    list.append(new MenuItem(this, "Dump FileInfo", MENU_DUMP_INFO));
    list.append(new MenuItem(this, "Dump PathObject", MENU_DUMP_OBJECT));
    count += 2;
    
	return count;
}

int FileDirEntry :: fetch_task_items(IndexedList<PathObject*> &list)
{
    list.append(new MenuItem(this, "Create D64", MENU_CREATE_D64));
    list.append(new MenuItem(this, "Create Directory", MENU_CREATE_DIR));
    list.append(new MenuItem(this, "Dump FileInfo", MENU_DUMP_INFO));
    list.append(new MenuItem(this, "Dump PathObject", MENU_DUMP_OBJECT));
    return 4;
}


char *FileDirEntry :: get_name()
{
    if(info)
        return info->lfname;
    return "FileDirEntry";
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
    FRESULT fres;
    
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
			
        case MENU_DUMP_INFO:
            info->print_info();
            break;
        
        case MENU_DUMP_OBJECT:
            dump();
            break;
            
        case MENU_CREATE_D64:
        	buffer[0] = 0;
        	if(!(info->attrib & AM_DIR)) {
        		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
        		break;
        	}
        	res = user_interface->string_box("Give name for new disk..", buffer, 22);
        	if(res > 0) {
    			set_extension(buffer, ".d64", 32);
    			fix_filename(buffer);
        		bin = new BinImage;
        		if(bin) {
                    File *f = root.fcreate(buffer, this);
        			if(f) {
                		bin->format(buffer);
                		printf("Result of save: %d.\n", bin->save(f));
                        root.fclose(f);
                		push_event(e_reload_browser);
        			} else {
        				printf("Can't create file '%s'\n", buffer);
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
        		FileInfo *fi = new FileInfo(*info);
        		printf("%p.", &fi);
        		strcpy(fi->lfname, buffer);
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
            break;
    }
	printf("Execute done!\n");
}

int FileDirEntry :: compare(PathObject *obj)
{
	FileDirEntry *b = (FileDirEntry *)obj;

	if ((info->attrib & AM_DIR) && !(b->info->attrib & AM_DIR))
		return -1;
	if (!(info->attrib & AM_DIR) && (b->info->attrib & AM_DIR))
		return 1;

	return stricmp(get_name(), b->get_name());
}
