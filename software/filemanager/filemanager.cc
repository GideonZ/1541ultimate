#include <stdio.h>
#include "event.h"
#include "poll.h"
#include "path.h"
#include "filemanager.h"
#include "file_device.h"

/* Instantiation of root system */
FileManager file_manager;

/* Poll function for main loop */
void poll_filemanager(Event &e)
{
	file_manager.handle_event(e);
}

void FileManager :: handle_event(Event &e)
{
	CachedTreeNode *o, *c;
    o = (CachedTreeNode *)e.object;
    BlockDevice *blk;
    File *f;
    FileDevice *fd;
    FileNodePair pair;
    Path *path;
    string pathString;
    
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
	case e_invalidate:
		printf("Invalidate event.. Param = %d. Checking %d files.\n", e.param, open_file_list.get_elements());
		for(int i=0;i<open_file_list.get_elements();i++) {
			pair = open_file_list[i];
			printf("%2d. %s %p\n", i, pair.file->info->lfname, pair.node);
			if(!pair.file) {// should never occur
				printf("INTERR: null pointer in list.\n");
				continue;
			} else if(!(f->isValid())) { // already invalidated.
				continue;
			}
			c = pair.node;
			while(c) {
				if(c == o) {
					printf("Due to invalidation of %s, file %s is being invalidated.\n", o->get_name(), pair.node->get_name());
					f->invalidate();
					break;
				}
				c = c->parent;
			}
		}
		for(int i=0;i<used_paths.get_elements();i++) {
			path = used_paths[i];
			c = path->current_dir_node;
			printf("%2d. %s (%s)\n", i, path->get_path(), path->owner);
			while(c) {
				if(c == o) {
					printf("Due to invalidation of %s, path %s is being invalidated.\n", o->get_name(), path->get_path());
					if (c->parent) {
						printf("c = %s c->parent = %s\n", c->get_name(), c->parent->get_name());
						if (e.param) { // if > 0, the node itself still exists, but the contents are removed.
							c->get_full_path(pathString);
						} else { // if == 0, the node itself gets removed, including the contents
							c->parent->get_full_path(pathString);
						}
						printf("Path will be reset to: %s\n", pathString.c_str());
						path->cd(pathString.c_str());
					} else {
						printf("c = %s c->parent = NULL\n", c->get_name() );
					}
					break;
				}
				c = c->parent;
			}
		}

		break;
    case e_detach_disk:
        fd = (FileDevice *)e.object;
        fd->detach_disk();
        break;
	default:
		break;
	}
}

File *FileManager :: fopen(Path *path, char *filename, uint8_t flags)
{
	int filename_length = strlen(filename);
	if (!filename_length)
		return NULL;

	// Copy the filename + path into a temporary buffer
	char *copy = new char[filename_length + 1];
	strcpy(copy, filename);

	// find the last separator
	int separator_pos = -1;
	for(int i=filename_length-1; i >= 1; i--) {
		if ((copy[i] == '/') || (copy[i] == '\\')) {
			separator_pos = i;
			break;
		}
	}

	// separate path and filename in two different strings
	char *purename = copy;
	char *pathname = NULL;
	if (separator_pos >= 0) {
		copy[separator_pos] = 0;
		pathname = copy;
		purename = &copy[separator_pos + 1];
	}

	// create a path that is either relative to root, or relative to current
	Path *temppath = this->get_new_path("FM temp");
	if ((filename[0] != '/') && (filename[0] != '\\')) {
		temppath->cd(path->get_path());
	}
	if (pathname) {
		temppath->cd(pathname);
	}

	// now do the actual thing
	File *ret = fopen_impl(temppath, purename, flags);

	// remove the temporary strings and path
	delete[] copy;
	this->release_path(temppath);
	return ret;
}

File *FileManager :: fopen_impl(Path *path, char *filename, uint8_t flags)
{
	FileInfo *dirinfo = path->current_dir_node->get_file_info();
	CachedTreeNode *existing = path->current_dir_node->find_child(filename);
	File *file = NULL;

	// file does not exist, and we would like it to exist: create.
	if ((!existing) && (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS))) {
		CachedTreeNode *newNode = new CachedTreeNode(path->current_dir_node, filename);
		FileInfo *newInfo = newNode->get_file_info();
		newInfo->dir_clust = dirinfo->cluster;
		newInfo->fs = dirinfo->fs;
		fix_filename(newInfo->lfname);
		file = dirinfo->fs->file_open(newInfo, flags);
		if(file) {
			path->current_dir_node->children.append(newNode);
			existing = newNode;
		} else {
			last_error = dirinfo->fs->get_last_error();
			delete newNode;
		}
	} else { // no creation
		file = dirinfo->fs->file_open(existing->get_file_info(), flags);
		last_error = dirinfo->fs->get_last_error();
	}

	if(file) {
		FileNodePair pair;
		pair.file = file;
		pair.node = existing;
		open_file_list.append(pair);
	}

	return file;
}

File *FileManager :: fopen_node(CachedTreeNode *node, uint8_t flags)
{
	FileInfo *inf = node->get_file_info();
	File *file = inf->fs->file_open(node->get_file_info(), flags);
	last_error = inf->fs->get_last_error();

	if(file) {
		FileNodePair pair;
		pair.file = file;
		pair.node = node;
		open_file_list.append(pair);
	}

	return file;
}

/*
File *FileManager :: fcreate(char *filename, CachedTreeNode *dir)
{
    FileInfo *info;
    info = dir->get_file_info();
    
    // printf("fcreate: DIR=%p (%s) info = %p\n", dir, dir->get_name(), info);
    
    if(!info) { // can't create file if I don't know how
        last_error = FR_NO_FILESYSTEM;
        return NULL;
    }
            
    if(!(info->attrib & AM_DIR)) {  // can't create a file in a file - it should be a dir.
        last_error = FR_DENIED;
        return NULL;
    }
            
    // info->print_info();
    
    // create a copy of the file info object
    FileInfo *fi = new FileInfo(info, filename);
    fi->dir_clust = info->cluster; // the new file will be located in the directory DIR_CLUST!
    // remember: our reference object was the directory, so the cluster of that object is our new dir_clust!
    fix_filename(fi->lfname);
    fi->attrib = 0;

    File *f = fi->fs->file_open(fi, FA_CREATE_NEW | FA_WRITE);
    if(!f) {
        last_error = fi->fs->get_last_error();
        return NULL;
    }        
    last_error = FR_OK;
    
    f->print_info();
    printf("File creation successful so far!\n");

    CachedTreeNode *node = new CachedTreeNode(dir, filename);
    f->node = node;
    //dir->children.append(node);
    open_file_list.append(f);    
    return f;
}

File *FileManager :: fcreate(char *filename, char *dirname)
{
	Path *path = new Path;
	//dump(); // dumps file system from root..
	if(path->cd(dirname)) {
		CachedTreeNode *po = path->get_path_object();
		return fcreate(filename, po);
	}
	return NULL;
}

File *FileManager :: fopen(char *filename, CachedTreeNode *dir, BYTE flags)
{
    last_error = FR_OK;
    CachedTreeNode *obj = dir->find_child(filename);
    if(obj) {
        return fopen(obj, flags);
    }

    // file not found. Check flags if we are allowed to create it
    if(flags & FA_CREATE_NEW) {
        return fcreate(filename, dir);
    }
    last_error = FR_NO_FILE;
    return NULL;    
}


File *FileManager :: fopen(char *filename, BYTE flags)
{
	Path *path = new Path;
	//dump(); // dumps file system from root..
	if(path->cd(filename)) {
		CachedTreeNode *po = path->get_path_object();
		FileInfo *fi = po->get_file_info();
		if(fi) {
			File *f = fi->fs->file_open(fi, flags);
			if(f) {
				f->node = po;
				f->path = path;
				open_file_list.append(f);
                last_error = FR_OK;
				return f; // could be 0
			} else {
                last_error = FR_NO_FILE;
	        }		    
		} else {
            last_error = FR_NO_FILESYSTEM;
			delete path;
			return 0;
		}
	} else {
        last_error = FR_NO_FILE;
    }
	delete path;
	return 0;
}

File *FileManager :: fopen(CachedTreeNode *obj, BYTE flags)
{
	FileInfo *fi = obj->get_file_info();
	if(fi) {
		File *f = fi->fs->file_open(fi, flags);
		if(f) {
			f->node = obj;
			f->path = NULL;
			open_file_list.append(f);
            last_error = FR_OK;
			return f; // could be 0
		} else {
		    last_error = fi->fs->get_last_error();
		}
	}
    last_error = FR_NO_FILESYSTEM;
	return 0;
}
*/

void FileManager :: fclose(File *f)
{
	FileInfo *inf = f->info;
	if(inf) {
		printf("Closing %s...\n", inf->lfname);
	}
	else {
		printf("Closing invalidated file.\n");
	}
	for(int i=0;i<open_file_list.get_elements();i++) {
		if (open_file_list[i].file == f) {
			open_file_list.mark_for_removal(i);
		}
	}
	open_file_list.purge_list();
	f->close();
}

void FileManager :: add_root_entry(CachedTreeNode *obj)
{
	root->children.append(obj);
	obj->parent = root;
}

void FileManager :: remove_root_entry(CachedTreeNode *obj)
{
	root->children.remove(obj);
}

CachedTreeNode *FileManager :: get_root()
{
	return root;
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
