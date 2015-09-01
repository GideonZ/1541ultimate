#include <stdio.h>
#include "path.h"
#include "filemanager.h"
#include "embedded_fs.h"
#include "file_device.h"
#include <cctype>

int FileManager :: validatePath(Path *p, CachedTreeNode **node)
{
	CachedTreeNode *n = root;

	for (int i=0;i<p->getDepth();i++) {
		n = n->find_child(p->getElement(i));
		if (!n)
			return 0;
		p->update(i, n->get_name());
	}
	*node = n;

	mstring full;
	p->update(n->get_full_path(full));

	return 1;
}

void FileManager :: invalidate(CachedTreeNode *o, int includeSelf)
{
	CachedTreeNode *par;
    BlockDevice *blk;
    MountPoint *m;
    File *f;
    FileDevice *fd;
    Path *path;
    mstring pathString;
    const char *pathStringC;
    int len;

	par = o->parent;

	printf("Invalidate event.. Param = %d. Checking %d files.\n", includeSelf, open_file_list.get_elements());
	pathStringC = o->get_full_path(pathString);
	len = strlen(pathStringC);

	for (int i=0;i<mount_points.get_elements();i++) {
		m = mount_points[i];
		if(!m) {// should never occur
			printf("INTERR: null pointer in mount point list.\n");
			continue;
		}
		f = m->get_file();
		printf("%2d. %s\n", i, m->get_path());
		if (strncmp(f->get_path(), pathStringC, len) == 0) {
			printf("Match!\n");
			fclose(f);
		}
		delete m;
		mount_points.mark_for_removal(i);
	}
	mount_points.purge_list();

	for(int i=0;i<open_file_list.get_elements();i++) {
		f = open_file_list[i];
		if(!f) {// should never occur
			printf("INTERR: null pointer in list.\n");
			continue;
		} else if(!(f->isValid())) { // already invalidated.
			continue;
		}
		printf("%2d. %s %p\n", i, f->info->lfname, f);
		if (strncmp(f->get_path(), pathStringC, len) == 0) {
			printf("Match!\n");
			f->invalidate();
		}
	}

	for(int i=0;i<used_paths.get_elements();i++) {
		path = used_paths[i];
		// path->invalidate(); // clear the validated state  (comment: not yet implemented)
	}

	if(includeSelf == 0) { // 0: the object still exists, just clean up the children
		o->cleanup_children();
	} else { // 1: the node itself disappears
		// if the node is in root, the remove_root_node shall be called.
		if(par && (par != root)) {
			printf("Removing %s from %s\n", o->get_name(), par->get_name());
			par->children.remove(o);
		}
	}
	printf("Invalidation complete.\n");
}

bool FileManager :: is_path_writable(Path *p)
{
	CachedTreeNode *n;
	if (!validatePath(p, &n))
		return false;
	return n->get_file_info()->is_writable();
}

bool FileManager :: is_path_valid(Path *p)
{
	CachedTreeNode *n;
	if (!validatePath(p, &n))
		return false;
	return n->is_ready();
}

void FileManager :: get_display_string(Path *p, const char *filename, char *buffer, int width)
{
	CachedTreeNode *n;
	if (!validatePath(p, &n)) {
		strncpy(buffer, "Invalid path", width);
		return;
	}
	CachedTreeNode *existing = n->find_child(filename);
	if(existing) {
		existing->get_display_string(buffer, width);
	} else {
		strncpy(buffer, "File doesn't exist in path", width);
	}
}

bool FileManager :: reworkPath(Path *path, const char *pathstr, const char *filename, reworked_t& rwp)
{
	int filename_length = strlen(filename);
	if (!filename_length)
		return false;
	if (path && pathstr) // use either path, or path str, not both
		return false;

	// Copy the filename + path into a temporary buffer
	char *copy = new char[filename_length + 1];
	rwp.copy = copy;
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
		} else if(pathstr) {
			temppath->cd(pathstr);
		} else {
			temppath->cd("/SD"); // TODO: Make this configurable
		}
	}
	if (pathname) {
		temppath->cd(pathname);
	}
	rwp.copy = copy;
	rwp.purename = purename;
	rwp.path = temppath;
	return true;
}

FRESULT FileManager :: get_directory(Path *p, IndexedList<FileInfo *> &target)
{
	lock();
	CachedTreeNode *n;
	if (!validatePath(p, &n)) {
    	unlock();
		return FR_NO_PATH;
	}
	int result = n->fetch_children();
	if (result < 0) {
    	unlock();
		return FR_NO_FILE;
	}
	for (int i=0;i<n->children.get_elements();i++) {
		target.append(new FileInfo(*(n->children[i]->get_file_info())));
	}
	unlock();
	return FR_OK;
}

FRESULT FileManager :: fopen(Path *path, const char *filename, uint8_t flags, File **file)
{
	reworked_t rwp;
	if (!reworkPath(path, 0, filename, rwp))
		return FR_INVALID_NAME;

	lock();
	// now do the actual thing
	FRESULT res = fopen_impl(rwp.path, rwp.purename, flags, file);

	// remove the temporary strings and path
	delete[] rwp.copy;
	this->release_path(rwp.path);
	unlock();
	return res;
}

FRESULT FileManager :: fopen(const char *path, const char *filename, uint8_t flags, File **file)
{
	reworked_t rwp;
	if (!reworkPath(0, path, filename, rwp))
		return FR_INVALID_NAME;

	lock();
	// now do the actual thing
	FRESULT res = fopen_impl(rwp.path, rwp.purename, flags, file);

	// remove the temporary strings and path
	delete[] rwp.copy;
	this->release_path(rwp.path);
	unlock();
	return res;
}

FRESULT FileManager :: fopen_impl(Path *path, char *filename, uint8_t flags, File **file)
{

	// pass complete path to the file system's "walk path" function. This function
	// attempts to search through its directory entries. There are various cases:

	// * Entry not found. No match, return status only
	// * Entry not found. Last entry is a valid directory. This is a valid situation for file create.
	// * Entry found. Return: FileInfo structure of entry. This might be an invalid situation when existing files should not get overwritten.
	// * Search terminated. Entry is a file, but there are more entries in the path. Return file info, and remaining path
	// * Search terminated. Entry is a mount point, and the remaining path should be searched in the resulting file system. Return file system and remaining path.

	bool ready;
	FileSystem *fs = rootfs;
	PathInfo pathInfo;
	pathInfo.remainingPath.cd(path->get_path());
	MountPoint *mp;

	FRESULT fres = FR_OK;
	while(!ready) {
		ready = true;
		PathStatus_t ps = fs->walk_path(path, filename, pathInfo);
		switch(ps) {
		case e_DirNotFound:
			fres = FR_NO_PATH;
			break;
		case e_EntryNotFound:
			fres = FR_NO_FILE;
			break;
		case e_EntryFound:
			break;
		case e_TerminatedOnFile:
			mp = find_mount_point(&pathInfo.fileInfo, &pathInfo.pathFromRootOfFileSystem);
			if(mp) {
				fs = mp->get_embedded()->getFileSystem();
				ready = false;
				path = &pathInfo.remainingPath;
			} else {
				fres = FR_NO_FILESYSTEM;
			}
			break;
		case e_TerminatedOnMount:
			fs = pathInfo.fileSystem;
			break;
		default:
			break;
		}
	}

/*
	CachedTreeNode *pathnode = NULL;
	if(!validatePath(path, &pathnode)) {
		return FR_NO_PATH;
	}

	FileInfo *dirinfo = pathnode->get_file_info();
	CachedTreeNode *existing = pathnode->find_child(filename);

	*file = NULL;
	FRESULT res;

	// file does not exist, and we would like it to exist: create.
	if ((!existing) && (flags & (FA_CREATE_NEW | FA_CREATE_ALWAYS))) {
		if (!(dirinfo->attrib & AM_DIR)) {
			return FR_INVALID_OBJECT;
		}

		FileInfo *newInfo = new FileInfo(filename);
		newInfo->dir_clust = dirinfo->cluster;
		newInfo->fs = dirinfo->fs;
		fix_filename(newInfo->lfname);
		get_extension(newInfo->lfname, newInfo->extension);

		res = dirinfo->fs->file_open(newInfo, flags, file);
		if(res == FR_OK) {
			pathnode->cleanup_children(); // force reload from media
			sendEventToObservers(eNodeAdded, path->get_path(), filename);
		} else {
			return res;
		}
	} else if(existing) { // no creation
		FileInfo *fileinfo = existing->get_file_info();
		res = fileinfo->fs->file_open(existing->get_file_info(), flags, file);
	} else {
		res = FR_NO_FILE;
	}

	if(*file) {
		(*file)->set_path(path->get_path());
		open_file_list.append(*file);
	}
*/

	return fres;
}

bool FileManager :: fstat(FileInfo &info, const char *path, const char *filename)
{
	reworked_t rwp;
	if (!reworkPath(0, path, filename, rwp))
		return false;

	lock();
	bool ret = false;
	CachedTreeNode *n, *w;
	if (validatePath(rwp.path, &n)) {
		w = n->find_child(rwp.purename);
		if (w) {
			info.copyfrom(w->get_file_info());
			ret = true;
		}
	}
	delete[] rwp.copy;
	this->release_path(rwp.path);
	unlock();
	return ret;
}

/*
FRESULT FileManager :: fopen_node(CachedTreeNode *node, uint8_t flags, File **file)
{
	lock();

	FileInfo *info = node->get_file_info();
	FRESULT res = info->fs->file_open(info, flags, file);

	if(*file) {
		open_file_list.append(*file);
		mstring work;
		(*file)->set_path(node->parent->get_full_path(work));
	}

	unlock();
	return res;
}
*/

void FileManager :: fclose(File *f)
{
	lock();
	FileInfo *inf = f->info;
	if(inf) {
		// printf("Closing %s...\n", inf->lfname);
	}
	else {
		printf("ERR: Closing invalidated file.\n");
	}
	open_file_list.remove(f);
	f->close();
	unlock();
}

void FileManager :: add_root_entry(CachedTreeNode *obj)
{
	lock();
	root->children.append(obj);
	obj->parent = root;
	sendEventToObservers(eNodeAdded, "/", obj->get_name());
	unlock();
}

void FileManager :: remove_root_entry(CachedTreeNode *obj)
{
	lock();
	invalidate(obj, 1); // cleanup!
	sendEventToObservers(eNodeRemoved, "/", obj->get_name());
	root->children.remove(obj);
	unlock();
}

MountPoint *FileManager :: add_mount_point(File *file, FileSystemInFile *emb)
{
	// printf("FileManager :: add_mount_point: '%s' (%p)\n", file->get_path(), emb);
	MountPoint *mp = new MountPoint(file, emb);
	mount_points.append(mp);
	return mp;
}

/*
MountPoint *FileManager :: find_mount_point(const char *full_path)
{
//	printf("FileManager :: find_mount_point: '%s'\n", full_path);
	lock();
	for(int i=0;i<mount_points.get_elements();i++) {
		if(mount_points[i]->match(full_path)) {
			// printf("Found!\n");
			unlock();
			return mount_points[i];
		}
	}
	unlock();
	return 0;
}
*/
MountPoint *FileManager :: find_mount_point(FileInfo *info, Path *path)
{
	printf("FileManager :: find_mount_point: '%s'\n", info->lfname);
	lock();
	for(int i=0;i<mount_points.get_elements();i++) {
		if(mount_points[i]->match(info->fs, info->cluster)) {
			// printf("Found!\n");
			unlock();
			return mount_points[i];
		}
	}
	if (!path) {
		unlock();
		return 0;
	}
	File *file;
	FileSystemInFile *emb = 0;
	MountPoint *mp = 0;
	FRESULT fr = fopen(path, info->lfname, FA_READ | FA_WRITE, &file);
	if (fr == FR_OK) {
		emb = Globals :: getEmbeddedFileSystemFactory() -> create(info);
		if (emb)
			mp = add_mount_point(file, emb);
	}
	unlock();
	return mp;
}


FRESULT FileManager :: delete_file_by_info(FileInfo *info)
{
/*
	lock();
	FRESULT fres = info->fs->file_delete(info);
	if(fres != FR_OK) {
		unlock();
		return fres;
	} else {
		sendEventToObservers(eNodeRemoved, "/", info->lfname); // FIXME: Path is unknown
	}
	unlock();
*/
	return FR_OK;
}

FRESULT FileManager :: create_dir_in_path(Path *path, const char *name)
{
/*
	CachedTreeNode *node;
	if (!validatePath(path, &node))
		return FR_NO_PATH;

	FileInfo *info = node->get_file_info();

	if(!(info->attrib & AM_DIR)) {
		printf("I don't know what you are trying to do, but the info doesn't point to a DIR!\n");
		return FR_INVALID_OBJECT;
	}
	if (!info->fs) {
		return FR_NO_FILESYSTEM;
	}

	lock();
	FileInfo *fi = new FileInfo(info, name);
	fi->attrib = 0;
	FRESULT fres = fi->fs->dir_create(fi);
	printf("Result of mkdir: %d\n", fres);
	if (fres == FR_OK) {
		node->cleanup_children(); // force reload from media
		sendEventToObservers(eNodeAdded, path->get_path(), name);
	}
	unlock();
	return fres;
*/
	return FR_DENIED;
}

FRESULT FileManager :: fcopy(const char *path, const char *filename, const char *dest)
{
	printf("Copying %s to %s\n", filename, dest);
	FileInfo *info = new FileInfo(64); // I do not use the stack here for the whole structure, because
	// we might recurse deeply. Our stack is maybe small.

	FRESULT ret = FR_OK;
	if (fstat(*info, path, filename)) {
		if (info->attrib & AM_DIR) {
			// create a new directory in our destination path
			Path *dp = get_new_path("copy destination");
			Path *sp = get_new_path("copy source");
			sp->cd(path);
			dp->cd(dest);
			FRESULT dir_create_result = create_dir_in_path(dp, filename);
			if (dir_create_result == FR_OK) {
				IndexedList<FileInfo *> *dirlist = new IndexedList<FileInfo *>(16, NULL);
				sp->cd(filename);
				FRESULT get_dir_result = get_directory(sp, *dirlist);
				if (get_dir_result == FR_OK) {
					dp->cd(filename);
					for (int i=0;i<dirlist->get_elements();i++) {
						FileInfo *el = (*dirlist)[i];
						ret = fcopy(sp->get_path(), el->lfname, dp->get_path());
					}
				} else {
					ret = get_dir_result;
				}
			} else {
				ret = dir_create_result;
			}
			release_path(sp);
			release_path(dp);
		} else if ((info->attrib & AM_VOL) == 0) { // it is a file!
			File *fi = 0;
			ret = fopen(path, filename, FA_READ, &fi);
			if (fi) {
				File *fo = 0;
				ret = fopen(dest, filename, FA_CREATE_NEW | FA_WRITE, &fo);
				if (fo) {
					uint8_t *buffer = new uint8_t[32768];
					uint32_t transferred, written;
					do {
						FRESULT frd_result = fi->read(buffer, 32768, &transferred);
						if (frd_result == FR_OK) {
							FRESULT fwr_result = fo->write(buffer, transferred, &written);
							if (fwr_result != FR_OK) {
								ret = fwr_result;
								break;
							}
						} else {
							ret = frd_result;
							break;
						}
					} while(transferred > 0);

					delete[] buffer;
					fclose(fo);
					fclose(fi);
				} else { // no output file
					printf("Cannot open output file %s\n", filename);
					fclose(fi);
				}
			} else {
				printf("Cannot open input file %s\n", filename);
			}
		} // is file?
	} else {
		printf("Could not stat %s\n", filename);
		ret = FR_NO_FILE;
	}

	delete info;
	return ret;
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
