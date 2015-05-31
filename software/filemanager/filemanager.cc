#include <stdio.h>
#include "event.h"
#include "poll.h"
#include "path.h"
#include "filemanager.h"
#include "file_device.h"
#include <cctype>

/* Poll function for main loop */
//FileManager *FileManager :: file_manager;

void FileManager :: handle_event(Event &e)
{
	CachedTreeNode *o, *c, *par;
    o = (CachedTreeNode *)e.object;
    BlockDevice *blk;
    File *f;
    FileDevice *fd;
    FileNodePair pair;
    Path *path;
    mstring pathString;
    
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
		par = o->parent;
		for(int i=0;i<open_file_list.get_elements();i++) {
			pair = open_file_list[i];
			f = pair.file;
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
		if(par && (par != root)) {
			printf("Removing %s from %s\n", o->get_name(), par->get_name());
			par->children.remove(o);
		}
		printf("Invalidation complete.\n");
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
		if(path) {
			temppath->cd(path->get_path());
		} else {
			temppath->cd("/SD"); // TODO: Make this configurable
		}
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
		CachedTreeNode *newNode = new FileDirEntry(path->current_dir_node, filename);
		FileInfo *newInfo = newNode->get_file_info();
		newInfo->dir_clust = dirinfo->cluster;
		newInfo->fs = dirinfo->fs;
		fix_filename(newInfo->lfname);
		get_extension(newInfo->lfname, newInfo->extension);
		file = dirinfo->fs->file_open(newInfo, flags);
		if(file) {
			// path->current_dir_node->children.append(newNode);
			path->current_dir_node->cleanup_children(); // force reload from media
			existing = newNode;
		} else {
			last_error = dirinfo->fs->get_last_error();
			delete newNode;
		}
	} else if(existing) { // no creation
		file = dirinfo->fs->file_open(existing->get_file_info(), flags);
		last_error = dirinfo->fs->get_last_error();
	} else {
		return 0;
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

FRESULT FileManager :: delete_file_by_node(CachedTreeNode *node)
{
	FileInfo *info = node->get_file_info();

	FRESULT fres = info->fs->file_delete(info);
	if(fres != FR_OK) {
		return fres;
	} else {
		push_event(e_invalidate, node);
		push_event(e_cleanup_path_object, node);
		push_event(e_reload_browser);
	}
	return FR_OK;
}

FRESULT FileManager :: create_dir_in_node(CachedTreeNode *node, char *name)
{
	FileInfo *info = node->get_file_info();

	if (!info) {
		return FR_NO_PATH;
	}
	if(!(info->attrib & AM_DIR)) {
		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
		return FR_INVALID_OBJECT;
	}
	if (!info->fs) {
		return FR_NO_FILESYSTEM;
	}

	FileInfo *fi = new FileInfo(info, name);
	fi->attrib = 0;
	FRESULT fres = fi->fs->dir_create(fi);
	printf("Result of mkdir: %d\n", fres);
	node->cleanup_children(); // force reload from media
	push_event(e_reload_browser);
	return fres;
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

void get_extension(const char *name, char *ext)
{
	int len = strlen(name);
	ext[0] = 0;

	for (int i=len-1;i>=0;i--) {
		if (name[i] == '.') { // last . found
			for(int j=0;j<3;j++) { // copy max 3 chars
				ext[j+1] = 0; // the char after the current is always end
				if (!name[i+1+j])
					break;
				ext[j] = toupper(name[i+1+j]);
			}
			break;
		}
	}
}
