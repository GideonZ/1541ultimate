#include "file_utils.h"

FRESULT fcopy(const char *path, const char *filename, const char *dest, const char *dest_filename, bool overwrite)
{
    printf("Copying %s to %s\n", filename, dest);
    FileInfo *info = new FileInfo(INFO_SIZE); // I do not use the stack here for the whole structure, because
    // we might recurse deeply. Our stack is maybe small.

    Path *sp = new Path(path);
    Path *dp = new Path(dest);
    FRESULT ret = FR_OK;

    if ((ret = FileManager::fstat(sp, filename, *info, false)) == FR_OK) {
        // In the special case that one tries to copy a file (or dir) onto itself, we just report
        // that we're done.
        if ( sp->equals(dp) && (strcasecmp(info->lfname, dest_filename) == 0)) {
            return FR_OK;
        }
        if (info->attrib & AM_DIR) {
            // create a new directory in our destination path
            FRESULT dir_create_result = FileManager::create_dir(dp, dest_filename);
            if ((dir_create_result == FR_OK) || (dir_create_result == FR_EXIST)) {
                IndexedList<FileInfo *> *dirlist = new IndexedList<FileInfo *>(16, NULL);
                sp->cd(filename);
                FRESULT get_dir_result = get_directory(sp, *dirlist, NULL);
                if (get_dir_result == FR_OK) {
                    dp->cd(filename);
                    for (int i = 0; i < dirlist->get_elements(); i++) {
                        FileInfo *el = (*dirlist)[i];
                        ret = fcopy(sp->get_path(), el->lfname, dp->get_path(), el->lfname, overwrite);
                        if (ret != FR_OK) {
                            break;
                        }
                    }
                }
                else {
                    ret = get_dir_result;
                }
                delete dirlist;
            }
            else {
                ret = dir_create_result;
            }
            delete dp;
        }
        else if ((info->attrib & AM_VOL) == 0) { // it is a file!
            File *fi = 0;
            ret = FileManager::fopen(sp, filename, FA_READ, &fi);
            if (fi) {
                File *fo = 0;
                char dest_name[100];
                strncpy(dest_name, dest_filename, 100);
                // This may look odd, but files inside a D64 for instance, do not have the extension in the filename anymore
                // so we add it here.
                set_extension(dest_name, info->extension, 100);
                uint8_t writeflags = (overwrite)? (FA_WRITE|FA_CREATE_ALWAYS) : (FA_WRITE|FA_CREATE_NEW);
                ret = FileManager::fopen(dp, dest_name, writeflags, &fo);
                if (fo) {
                    uint8_t *buffer = new uint8_t[32768];
                    uint32_t transferred, written;
                    do {
                        FRESULT frd_result = FileManager::read(fi, buffer, 32768, &transferred);
                        if (frd_result == FR_OK) {
                            FRESULT fwr_result = FileManager::write(fo, buffer, transferred, &written);
                            if (fwr_result != FR_OK) {
                                ret = fwr_result;
                                break;
                            }
                        }
                        else {
                            ret = frd_result;
                            break;
                        }
                    } while (transferred > 0);

                    delete[] buffer;
                    FileManager::fclose(fo);
                    FileManager::fclose(fi);
                }
                else { // no output file
                    printf("Cannot open output file %s\n", filename);
                    FileManager::fclose(fi);
                }
            }
            else {
                printf("Cannot open input file %s\n", filename);
            }
        } // is file?
    }
    else {
        printf("Could not stat %s (%s)\n", filename, FileSystem::get_error_string(ret));
    }
    delete sp;
    delete info;
    return ret;
}

FRESULT load_file(const char *path, const char *filename, uint8_t *mem, uint32_t maxlen, uint32_t *transferred)
{
    FileManager *fm = FileManager::getFileManager();
    File *file = 0;
    FRESULT fres = FileManager::fopen(path, filename, FA_READ, &file);
    uint32_t tr = 0;
    if (fres == FR_OK) {
        fres = FileManager::read(file, mem, maxlen, &tr);
        FileManager::fclose(file);
    }
    if (transferred) {
        *transferred = tr;
    }
    return fres;
}

FRESULT save_file(bool overwrite, const char *path, const char *filename, uint8_t *mem, uint32_t len, uint32_t *transferred)
{
    FileManager *fm = FileManager::getFileManager();
    File *file = 0;
    uint8_t flag = FA_WRITE | (overwrite ? FA_CREATE_ALWAYS : FA_CREATE_NEW);
    FRESULT fres = FileManager::fopen(path, filename, flag, &file);
    uint32_t tr = 0;
    if (fres == FR_OK) {
        fres = FileManager::write(file, mem, len, &tr);
        FileManager::fclose(file);
    }
    if (transferred) {
        *transferred = tr;
    }
    return fres;
}

FRESULT get_directory(Path *p, IndexedList<FileInfo *> &target, const char *matchPattern)
{
    Directory *dir;
    FileInfo info(INFO_SIZE);

    FRESULT res = FileManager::fstat(p, NULL, info, true);
    if (res != FR_OK) {
        printf("Getting directory %s: Error: %s\n", p->get_path(), FileSystem::get_error_string(res));
        return res;
    }
    FileSystem *fs = info.fs;

    res = FileManager::open_dir(p->get_path(), &dir);
    if (res != FR_OK) {
        return res;
    }
    if (dir == NULL) {
        return FR_NO_PATH;
    }

    while (1) {
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

    if (fs->needs_sorting()) {
        printf("Sorting directory %s (%p:%s)\n", p->get_path(), fs, fs->identify());
        target.sort(FileInfo::compare);
    }
    FileManager::close_dir(dir);

    return FR_OK;
}

FRESULT delete_recursive(Path *path, const char *name)
{
    FileInfo *info = new FileInfo(INFO_SIZE); // I do not use the stack here for the whole structure, because
    // we might recurse deeply. Our stack is maybe small.

    FRESULT ret = FR_OK;
    if ((ret = FileManager::fstat(path, name, *info, false)) == FR_OK) {
        if (info->is_directory()) {
            path->cd(name);
            IndexedList<FileInfo *> *dirlist = new IndexedList<FileInfo *>(16, NULL);
            FRESULT get_dir_result = get_directory(path, *dirlist, NULL);
            if (get_dir_result == FR_OK) {
                for (int i = 0; i < dirlist->get_elements(); i++) {
                    FileInfo *el = (*dirlist)[i];
                    ret = delete_recursive(path, el->lfname);
                }
            } else {
                ret = get_dir_result;
            }
            delete dirlist;
            path->cd("..");
        }
        // After recursive deletion, the dir should be empty.
        ret = FileManager::delete_file(path, name);
    }
    printf("Deleting %s%s: %s\n", path->get_path(), name, FileSystem::get_error_string(ret));
    delete info;
    return ret;
}

FRESULT print_directory(const char *path)
{
    printf("*** %s ***\n", path);

    Directory *dir;
    FileInfo info(INFO_SIZE);

    FRESULT res = FileManager::fstat(path, info);
    if (res != FR_OK) {
        return res;
    }
    FileSystem *fs = info.fs;

    res = FileManager::open_dir(path, &dir);
    if (res != FR_OK) {
        return res;
    }
    if (dir == NULL) {
        return FR_NO_PATH;
    }

    while (1) {
        res = dir->get_entry(info);
        if (res != FR_OK)
            break;
        printf("%60s (%s [%02x]) %10d\n", info.lfname, (info.attrib & AM_DIR) ? "DIR " : "FILE", info.attrib, info.size);
    }
    FileManager::close_dir(dir);

    return FR_OK;
}
