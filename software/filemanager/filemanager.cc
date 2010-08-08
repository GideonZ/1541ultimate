#include <stdio.h>
#include "small_printf.h"
#include "event.h"
#include "poll.h"
#include "path.h"
#include "filemanager.h"


/* Instantiation of root system */
FileManager root;

/* Poll function for main loop */
void poll_filemanager(Event &e)
{
	root.handle_event(e);
}

void FileManager :: handle_event(Event &e)
{
	PathObject *o, *c;
    o = (PathObject *)e.object;
    File *f;
	switch(e.type) {
	case e_cleanup_path_object:
        printf("Cleaning up %s\n", o->get_name());
        delete o;
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
	default:
		break;
	}
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
			return 0;
		}
	}
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
