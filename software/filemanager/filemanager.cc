#include <stdio.h>
extern "C" {
    #include "small_printf.h"
}
#include "event.h"
#include "poll.h"
#include "path.h"
#include "filemanager.h"
#include "file_device.h"

/* Instantiation of root system */
FileManager root("Root");

/* Poll function for main loop */
void poll_filemanager(Event &e)
{
	root.handle_event(e);
}

void FileManager :: handle_event(Event &e)
{
	PathObject *o, *c;
    o = (PathObject *)e.object;
    BlockDevice *blk;
    File *f;
    FileDevice *fd;
    
	switch(e.type) {
	case e_cleanup_path_object:
        printf("Cleaning up %s\n", o->get_name());
        delete o;
        break;
    case e_cleanup_block_device:
        blk = (BlockDevice *)e.object;
        printf("Cleaning up Blockdevice %p\n", blk);
        delete blk;
        break;
	case e_path_object_exec_cmd:
		o->execute(e.param);
		break;
	case e_invalidate:
		printf("Invalidate event.. Checking %d files.\n", open_file_list.get_elements());
		for(int i=0;i<open_file_list.get_elements();i++) {
			f = open_file_list[i];
			if(!f) {// should never occur
				printf("INTERR: null pointer in list.\n");
				continue;
			} else if(!f->node) { // already invalidated.
				continue;
			}
			c = f->node;
			while(c) {
				if(c == o) {
					printf("Due to invalidation of %s, file %s is being invalidated.\n", o->get_name(), f->node->get_name());
					f->node->detach();
					f->invalidate();
					break;
				}
				c = c->parent;
			}
		}
		break;
    case e_detach_disk:
        fd = (FileDevice *)e.object;
        fd->detach_disk();
	default:
		break;
	}
}

File *FileManager :: fcreate(char *filename, PathObject *dir)
{
    FileInfo *info;
    info = dir->get_file_info();
    
    if(!info) // can't create file if I don't know how
        return NULL;
        
    if(!(info->attrib & AM_DIR))  // can't create a file in a file - it should be a dir.
        return NULL;
        
    // create a copy of the file info object
    FileInfo *fi = new FileInfo(info, filename);
    fi->dir_clust = info->cluster; // the new file will be located in the directory DIR_CLUST!
    // remember: our reference object was the directory, so the cluster of that object is our new dir_clust!
    fix_filename(fi->lfname);
    fi->attrib = 0;

    File *f = fi->fs->file_open(fi, FA_CREATE_NEW | FA_WRITE);
    if(!f)
        return NULL;
        
//    f->print_info();
    PathObject *node = new PathObject(dir, filename);
    f->node = node;
    dir->children.append(node);
    open_file_list.append(f);    
    node->attach();
    return f;
}

File *FileManager :: fopen(char *filename, PathObject *dir, BYTE flags)
{
    PathObject *obj = dir->find_child(filename);
    if(obj) {
        return fopen(obj, flags);
    }

    // file not found. Check flags if we are allowed to create it
    if(flags & FA_CREATE_NEW) {
        return fcreate(filename, dir);
    }
    return NULL;    
}


File *FileManager :: fopen(char *filename, BYTE flags)
{
	Path *path = new Path;
	//dump(); // dumps file system from root..
	if(path->cd(filename)) {
		PathObject *po = path->get_path_object();
		FileInfo *fi = po->get_file_info();
		if(fi) {
			File *f = fi->fs->file_open(fi, flags);
			if(f) {
				po->attach();
				f->node = po;
				f->path = path;
				open_file_list.append(f);
				return f; // could be 0
			}
		} else {
			delete path;
			return 0;
		}
	}
	delete path;
	return 0;
}

File *FileManager :: fopen(PathObject *obj, BYTE flags)
{
	FileInfo *fi = obj->get_file_info();
	if(fi) {
		File *f = fi->fs->file_open(fi, flags);
		if(f) {
			obj->attach();
			f->node = obj;
			f->path = NULL;
			open_file_list.append(f);
			return f; // could be 0
		}
	} else {
		return 0;
	}
	return 0;
}

void FileManager :: fclose(File *f)
{
	PathObject *obj = f->node;
	Path *p = f->path;
	if(obj) {
		printf("Closing %s...\n", obj->get_name());
		obj->detach();
	}
	else
		printf("Closing invalidated file.\n");
	open_file_list.remove(f);
	f->close();
	if(p) {
		printf("File closed, now destructing path.\n");
		delete p;
		printf("ok!\n");
	}
}

/* some handy functions */
void set_extension(char *buffer, char *ext, int buf_size)
{
	int ext_len = strlen(ext);
	if(buf_size < 1+ext_len)
		return; // cant append, even to an empty base

	// try to remove the extension
	int name_len = strlen(buffer);
	int min_dot = name_len-ext_len;
	if(min_dot < 0)
		min_dot = 0;
	for(int i=name_len-1;i>=min_dot;i--) {
		if(buffer[i] == '.')
			buffer[i] = 0;
	}

	name_len = strlen(buffer);
	if(name_len + ext_len + 1 > buf_size) {
		buffer[buf_size-ext_len] = 0; // truncate to make space for extension!
	}
	strcat(buffer, ext);
}

void fix_filename(char *buffer)
{
	const char illegal[] = "\"*:<>\?|,\x7F";
	int illegal_count = strlen(illegal);
	int len = strlen(buffer);

	for(int i=0;i<len;i++)
		for(int j=0;j<illegal_count;j++)
			if(buffer[i] == illegal[j])
				buffer[i] = '_';

}
