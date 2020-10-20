#include <stdio.h>
#include "path.h"
#include "filemanager.h"
#include "embedded_fs.h"
#include "file_device.h"
#include <cctype>

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
		printf("%2d. \n", i); //, m->get_path());
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
		printf("%2d. %p:%s\n", i, f, f->get_path());
		if (strncmp(f->get_path(), pathStringC, len) == 0) {
			printf("Match!\n");
			f->invalidate();
		}
	}

/*
	for(int i=0;i<used_paths.get_elements();i++) {
		path = used_paths[i];
		// path->invalidate(); // clear the validated state  (comment: not yet implemented)
	}
*/

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
	PathInfo pathInfo(rootfs);
	pathInfo.init(p);
	FRESULT res = find_pathentry(pathInfo, true);
	if (res != FR_OK) {
		return false; // no other way to say it failed
	}
	return pathInfo.getLastInfo()->is_writable();
}

bool FileManager :: is_path_valid(Path *p)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(p);
	FRESULT res = find_pathentry(pathInfo, true);
	return (res == FR_OK);
}

void FileManager :: get_display_string(Path *p, const char *filename, char *buffer, int width)
{
	CachedTreeNode *n = root;

	for(int i=0; i<p->getDepth(); i++) {
		n = n->find_child(p->getElement(i));
		if (!n) {
			strncpy(buffer, "Invalid path", width);
			return;
		}
	}
	n = n->find_child(filename);
	if (!n) {
		strncpy(buffer, "Invalid entry", width);
		return;
	}
	n->get_display_string(buffer, width);
}

FRESULT FileManager :: get_directory(Path *p, IndexedList<FileInfo *> &target, const char *matchPattern)
{
	lock();

	PathInfo pathInfo(rootfs);
	pathInfo.init(p);
	FRESULT res = find_pathentry(pathInfo, true);
	if (res != FR_OK) {
		unlock();
		return res;
	}
	Directory *dir;
	mstring pathFromFSRoot;
	FileSystem *fs = pathInfo.getLastInfo()->fs;
	res = fs->dir_open(pathInfo.getPathFromLastFS(pathFromFSRoot), &dir, pathInfo.getLastInfo());
	FileInfo info(INFO_SIZE);
	if (res == FR_OK) {
		while(1) {
			res = dir->get_entry(info);
			if (res != FR_OK)
				break;
			if ((info.lfname[0] == '.') || (info.attrib & AM_HID))
				continue; // skip files to be hidden.
            if (matchPattern && (strlen(matchPattern) > 0) && !pattern_match(matchPattern, info.lfname, false)) {
                continue;
            }
			target.append(new FileInfo(info));
		}
		delete dir;
	}
	unlock();
	if (fs->needs_sorting()) {
		target.sort(FileInfo :: compare);
	}
	return FR_OK;
}

FRESULT FileManager :: print_directory(const char *path)
{
	FileSystem *fs = 0;
	printf("*** %s ***\n", path);
	PathInfo pathInfo(rootfs);
	pathInfo.init(path);

	FRESULT fres = find_pathentry(pathInfo, true);
//	pathInfo.dumpState("Resulting");

	if (fres != FR_OK) {
		printf("%s\n***\n", FileSystem :: get_error_string(fres));
	} else {
		Directory *dir;
		FileInfo info(INFO_SIZE);
		mstring pathFromFSRoot;
		fs = pathInfo.getLastInfo()->fs;
		//fres = fs->dir_open(pathInfo.getPathFromLastFS(pathFromFSRoot), &dir, pathInfo.getLastInfo());
        fres = fs->dir_open(NULL, &dir, pathInfo.getLastInfo());
		if (fres == FR_OK) {
			while(1) {
				fres = dir->get_entry(info);
				if (fres != FR_OK)
					break;
				printf("%32s (%s) %10d\n", info.lfname, (info.attrib &AM_DIR)?"DIR ":"FILE", info.size);
			}
			delete dir;
		}
		printf("***\n");
	}

	return fres;
}

FRESULT FileManager :: fopen(Path *path, const char *filename, uint8_t flags, File **file)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, filename);

	lock();
	// now do the actual thing
	FRESULT res = fopen_impl(pathInfo, flags, file);

	unlock();
	return res;
}

FRESULT FileManager :: fopen(const char *path, const char *filename, uint8_t flags, File **file)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, filename);

	lock();
	// now do the actual thing
	FRESULT res = fopen_impl(pathInfo, flags, file);

	unlock();
	return res;
}

FRESULT FileManager :: fopen(const char *pathname, uint8_t flags, File **file)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(pathname);

	lock();
	// now do the actual thing
	FRESULT res = fopen_impl(pathInfo, flags, file);

	unlock();
	return res;
}

FRESULT FileManager :: find_pathentry(PathInfo &pathInfo, bool open_mount)
{
	// pass complete path to the file system's "walk path" function. This function
	// attempts to search through its directory entries. There are various cases:

	// * Entry not found. No match, return status only
	// * Entry not found. Last entry is a valid directory. This is a valid situation for file create.
	// * Entry found. Return: FileInfo structure of entry. This might be an invalid situation when existing files should not get overwritten.
	// * Search terminated. Entry is a file, but there are more entries in the path. Return file info, and remaining path
	// * Search terminated. Entry is a mount point, and the remaining path should be searched in the resulting file system. Return file system and remaining path.

	// printf("FileManager :: find_pathentry('%s', %d)\n", filepath, open_mount);

	bool ready = false;
	FileSystem *fs = pathInfo.getLastInfo()->fs;
	MountPoint *mp;
	mstring pathFromFSRoot;
	mstring dirFromFSRoot;

	FRESULT fres = FR_OK;
	while(!ready) {
		ready = true;

		PathStatus_t ps = fs->walk_path(pathInfo);
		switch(ps) {
		case e_DirNotFound:
			fres = FR_NO_PATH;
			break;
		case e_EntryNotFound:
			fres = FR_NO_FILE;
			break;
		case e_EntryFound:
			fres = FR_OK;
			// we end on a file, and we're requested to force open it
			if (open_mount && !(pathInfo.last->attrib & AM_DIR)) {
				mp = find_mount_point(pathInfo.last, pathInfo.previous);
				if(mp) {
					fs = mp->get_embedded()->getFileSystem();
					pathInfo.enterFileSystem(fs);
				} else {
					fres = FR_NO_FILESYSTEM;
				}
			}
			break;
		case e_TerminatedOnFile:
			mp = find_mount_point(pathInfo.last, pathInfo.previous);
			if(mp) {
				fs = mp->get_embedded()->getFileSystem();
				pathInfo.enterFileSystem(fs);
				ready = false;
			} else {
				fres = FR_NO_FILESYSTEM;
			}
			break;
		case e_TerminatedOnMount:
			fs = pathInfo.last->fs;
			pathInfo.enterFileSystem(fs);
			ready = !pathInfo.hasMore();
			break;
		default:
			break;
		}
	}

	return fres;
}

FRESULT FileManager :: fopen_impl(PathInfo &pathInfo, uint8_t flags, File **file)
{
	FRESULT fres = find_pathentry(pathInfo, false);

	if (fres == FR_NO_PATH)
		return fres;

	if ((fres == FR_OK) && (flags & FA_CREATE_NEW)) {
		return FR_EXIST;
	}

	// printf("Path %s was accessed with result: %s\n", path->get_path(), FileSystem :: get_error_string(fres));
	bool create = (flags & FA_CREATE_NEW) && (fres == FR_NO_FILE);
	create |= (flags & FA_CREATE_ALWAYS) && (fres == FR_OK);
	create |= (flags & FA_CREATE_ALWAYS) && (fres == FR_NO_FILE);
    create |= (flags & FA_OPEN_ALWAYS) && (fres == FR_NO_FILE);

	if ((fres != FR_OK) && (!create))
		return fres;

	// info.print_info();
	// printf("The path from the root of this filesystem: %s\n", rem.c_str());

	FileSystem *fs = pathInfo.getLastInfo()->fs;
	FileInfo *relativeDir = 0;
	mstring workpath;

	if (create) {
        relativeDir = pathInfo.getLastInfo();
	} else {
	    relativeDir = pathInfo.getParentInfo();
	}

	mstring workPathFromFSRoot;
	char *filename = (char *)pathInfo.getFileName();
	if (create) {
		fix_filename(filename);
	}

	fres = fs->file_open(filename, flags, file, relativeDir);
	if (fres == FR_OK) {
	    open_file_list.append(*file);
	    pathInfo.workPath.getTail(0, (*file)->get_path_reference());
	    if (create) {
            const char *pathstring = pathInfo.getFullPath(workpath, -1);
            sendEventToObservers(eRefreshDirectory, pathstring, "");
	    }
	}
	return fres;
}

FRESULT FileManager :: get_filesystem(Path *path, FileSystem **filesys)
{
    PathInfo pathInfo(rootfs);
    pathInfo.init(path);
    FRESULT fres = find_pathentry(pathInfo, true);
    if (fres != FR_OK) {
        return fres;
    }
    FileInfo *inf = pathInfo.getLastInfo();
    if (!inf || !(inf->fs)) {
        return FR_NO_FILESYSTEM;
    }
    *filesys = inf->fs;
    return FR_OK;
}

FRESULT FileManager :: fstat(Path *path, const char *filename, FileInfo &info)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, filename);
	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres != FR_OK) {
		return fres;
	}
	info.copyfrom(pathInfo.getLastInfo());
	return FR_OK;
}

FRESULT FileManager :: fstat(const char *path, const char *filename, FileInfo &info)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, filename);
	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres != FR_OK) {
		return fres;
	}
	info.copyfrom(pathInfo.getLastInfo());
	return FR_OK;
}

FRESULT FileManager :: fstat(const char *pathname, FileInfo &info)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(pathname);
	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres != FR_OK) {
		return fres;
	}
	info.copyfrom(pathInfo.getLastInfo());
	return FR_OK;
}

void FileManager :: fclose(File *f)
{
	lock();
	if(f->get_file_system()) {
		// printf("Closing %s...\n", inf->lfname);
	}
	else {
		printf("ERR: Closing invalidated file.\n");
	}
//	printf("CLOSE '%s'\n", f->get_path());
	if (f->was_written_to()) {
	    sendEventToObservers(eNodeUpdated, f->get_path(), "*");
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

MountPoint *FileManager :: find_mount_point(FileInfo *info, FileInfo *parent)
{
	// printf("FileManager :: find_mount_point: '%s' (parent: %s)\n", info->lfname, parent->lfname);
	lock();
	for(int i=0;i<mount_points.get_elements();i++) {
		if(mount_points[i]->match(info->fs, info->cluster)) {
			// printf("Found!\n");
			unlock();
			return mount_points[i];
		}
	}

	File *file;
	FileSystemInFile *emb = 0;
	MountPoint *mp = 0;

	uint8_t flags = (info->attrib & AM_RDO) ? FA_READ : FA_READ | FA_WRITE;

	FRESULT fr = info->fs->file_open(info->lfname, flags, &file, parent);
	if (fr == FR_OK) {
		emb = FileSystemInFile :: getEmbeddedFileSystemFactory() -> create(info);
		if (emb) {
			emb->init(file);
			if (emb->getFileSystem()) {
				mp = add_mount_point(file, emb);
			}
		}
	}
	unlock();
	return mp;
}


FRESULT FileManager :: delete_file(const char *pathname)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(pathname);
	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres != FR_OK) {
		return fres;
	}
	FileSystem *fs = pathInfo.getLastInfo()->fs;
	mstring work;
	fres = fs->file_delete(pathInfo.getPathFromLastFS(work));
	if (fres == FR_OK) {
		pathInfo.workPath.getHead(work);
		sendEventToObservers(eNodeRemoved, pathInfo.workPath.getSub(0, pathInfo.index-1, work), pathInfo.getFileName());
	}
// LOCK & UNLOCK
	return fres;
}

FRESULT FileManager :: delete_file(Path *path, const char *name)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, name);
	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres != FR_OK) {
		return fres;
	}
	FileSystem *fs = pathInfo.getLastInfo()->fs;
	mstring work;
	fres = fs->file_delete(pathInfo.getPathFromLastFS(work));
	if (fres == FR_OK) {
		pathInfo.workPath.getHead(work);
		sendEventToObservers(eNodeRemoved, pathInfo.workPath.getSub(0, pathInfo.index-1, work), pathInfo.getFileName());
	}
// LOCK & UNLOCK
	return fres;
}

FRESULT FileManager :: rename(Path *path, const char *old_name, const char *new_name)
{
	PathInfo pathInfoFrom(rootfs);
	pathInfoFrom.init(path, old_name);
	PathInfo pathInfoTo(rootfs);
	pathInfoTo.init(path, new_name);
	return rename_impl(pathInfoFrom, pathInfoTo);
}

FRESULT FileManager :: rename(Path *old_path, const char *old_name, Path *new_path, const char *new_name)
{
    PathInfo pathInfoFrom(rootfs);
    pathInfoFrom.init(old_path, old_name);
    PathInfo pathInfoTo(rootfs);
    pathInfoTo.init(new_path, new_name);
    return rename_impl(pathInfoFrom, pathInfoTo);
}

FRESULT FileManager :: rename(const char *old_name, const char *new_name)
{
	PathInfo pathInfoFrom(rootfs);
	pathInfoFrom.init(old_name);
	PathInfo pathInfoTo(rootfs);
	pathInfoTo.init(new_name);
	return rename_impl(pathInfoFrom, pathInfoTo);
}

FRESULT FileManager :: rename_impl(PathInfo &from, PathInfo &to)
{
	FRESULT fres = find_pathentry(from, false);
	if (fres != FR_OK)
		return fres;

	// source file was found
	fres = find_pathentry(to, false);
	if (fres == FR_NO_PATH)
		return fres;

	if (fres == FR_NO_FILE) { // we MAY be able to do this..
		if (to.getLastInfo()->fs != from.getLastInfo()->fs) {
			printf("Trying to move a file from one file system to another.\n");
			return FR_INVALID_DRIVE;
		}
		mstring work1, work2;
		fres = from.getLastInfo()->fs->file_rename(
					from.getPathFromLastFS(work1),
					to.getPathFromLastFS(work2));
		if (fres == FR_OK) {
			const char *from_path = from.getFullPath(work1, -1);
			const char *to_path = to.getFullPath(work2, -1);
			sendEventToObservers(eRefreshDirectory, from_path, "");
			if (strcmp(from_path, to_path) != 0) {
				sendEventToObservers(eRefreshDirectory, to_path, "");
			}
		}
		return fres;
	}
	if (fres == FR_OK)
		return FR_EXIST;
	return fres;
}

FRESULT FileManager :: create_dir(const char *pathname)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(pathname);

	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres == FR_OK) {
		return FR_EXIST;
	}
	if (fres == FR_OK)
		return FR_EXIST;
	if (fres == FR_NO_FILE) {
		FileSystem *fs = pathInfo.getLastInfo()->fs;
		mstring work;
		pathInfo.getPathFromLastFS(work);
		fres = fs->dir_create(work.c_str());
		if (fres == FR_OK) {
			sendEventToObservers(eNodeAdded, pathInfo.getFullPath(work, -1), pathInfo.getFileName());
		}
		return fres;
	}
	return fres;
}

FRESULT FileManager :: create_dir(Path *path, const char *name)
{
	PathInfo pathInfo(rootfs);
	pathInfo.init(path, name);

	FRESULT fres = find_pathentry(pathInfo, false);
	if (fres == FR_OK) {
		return FR_EXIST;
	}
	if (fres == FR_OK)
		return FR_EXIST;
	if (fres == FR_NO_FILE) {
		FileSystem *fs = pathInfo.getLastInfo()->fs;
		mstring work;
		pathInfo.getPathFromLastFS(work);
		fres = fs->dir_create(work.c_str());
		if (fres == FR_OK) {
			sendEventToObservers(eNodeAdded, path->get_path(), name);
		}
		return fres;
	}
	return fres;
}

FRESULT FileManager :: fcopy(const char *path, const char *filename, const char *dest)
{
	printf("Copying %s to %s\n", filename, dest);
	FileInfo *info = new FileInfo(INFO_SIZE); // I do not use the stack here for the whole structure, because
	// we might recurse deeply. Our stack is maybe small.

	Path *sp = get_new_path("copy source");
	sp->cd(path);
	Path *dp = get_new_path("copy destination");
	dp->cd(dest);

	FRESULT ret = FR_OK;
	if ((ret = fstat(sp, filename, *info)) == FR_OK) {
		if (info->attrib & AM_DIR) {
			// create a new directory in our destination path
			FRESULT dir_create_result = create_dir(dp, filename);
			if (dir_create_result == FR_OK) {
				IndexedList<FileInfo *> *dirlist = new IndexedList<FileInfo *>(16, NULL);
				sp->cd(filename);
				FRESULT get_dir_result = get_directory(sp, *dirlist, NULL);
				if (get_dir_result == FR_OK) {
					dp->cd(filename);
					for (int i=0;i<dirlist->get_elements();i++) {
						FileInfo *el = (*dirlist)[i];
						ret = fcopy(sp->get_path(), el->lfname, dp->get_path());
					}
				} else {
					ret = get_dir_result;
				}
				delete dirlist;
			} else {
				ret = dir_create_result;
			}
			release_path(dp);
		} else if ((info->attrib & AM_VOL) == 0) { // it is a file!
			File *fi = 0;
			ret = fopen(sp, filename, FA_READ, &fi);
			if (fi) {
				File *fo = 0;
	            char dest_name[100];
	            strncpy(dest_name, filename, 100);
	            // This may look odd, but files inside a D64 for instance, do not have the extension in the filename anymore
	            // so we add it here.
	            set_extension(dest_name, info->extension, 100);
	            ret = fopen(dp, dest_name, FA_CREATE_NEW | FA_WRITE, &fo);
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
		printf("Could not stat %s (%s)\n", filename, FileSystem :: get_error_string(ret));
	}
	release_path(sp);
	delete info;
	return ret;
}


const char *FileManager :: eventStrings[] = {
    "eRefreshDirectory",  // Contents of directory have changed
    "eNodeAdded",         // New Node
    "eNodeRemoved",       // Node no longer exists (deleted)
    "eNodeMediaRemoved",  // Node lost all its children
    "eNodeUpdated",       // Node status changed (= redraw line)
    "eChangeDirectory",   // Request to change current directory within observer task
};
