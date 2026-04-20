/*
 * filetype_zip.cc
 *
 * This file is part of the 1541 Ultimate-II application.
 * Copyright (C) 2008-2024 Gideon Zweijtzer <info@1541ultimate.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filetype_zip.h"
#include "filemanager.h"
#include "userinterface.h"
#include "ui_elements.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_zip(FileType::getFileTypeFactory(), FileTypeZIP::test_type);

// ---- Internal helpers ----

// Remove trailing slash from buf (in-place). Returns new length.
static int strip_trailing_slash(char *buf)
{
    int len = (int)strlen(buf);
    if (len > 1 && buf[len - 1] == '/') {
        buf[--len] = '\0';
    }
    return len;
}

// Create every directory component of subpath under base.
// e.g. base="/USB0/Games/new", subpath="games/action/file.d64"
// creates "/USB0/Games/new/games" and "/USB0/Games/new/games/action"
static void ensure_zip_path(FileManager *fm, const char *base, const char *subpath)
{
    char current[512];
    strncpy(current, base, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    int base_len = strip_trailing_slash(current);

    const char *p = subpath;
    while (*p) {
        const char *slash = strchr(p, '/');
        if (!slash) break; // last segment has no slash → filename, not a dir

        int seg_len = (int)(slash - p);
        if (seg_len > 0 && (base_len + 1 + seg_len) < (int)(sizeof(current) - 1)) {
            current[base_len] = '/';
            memcpy(current + base_len + 1, p, seg_len);
            base_len += 1 + seg_len;
            current[base_len] = '\0';
            fm->create_dir(current); // FR_EXIST is fine
        }
        p = slash + 1;
    }
}

// Read the entire ZIP file into a malloc'd buffer. Returns NULL on error.
static uint8_t *load_zip_to_mem(UserInterface *ui, const char *zip_path, const char *zip_name, uint32_t &out_size)
{
    FileManager *fm = FileManager::getFileManager();
    File *file = NULL;

    FRESULT fres = fm->fopen(zip_path, zip_name, FA_READ, &file);
    if (!file || fres != FR_OK) {
        if (ui) ui->popup("Cannot open ZIP file.", BUTTON_OK);
        return NULL;
    }

    uint32_t zip_size = file->get_size();
    if (zip_size > 8 * 1024 * 1024) {
        fm->fclose(file);
        if (ui) ui->popup("ZIP too large (>8MB).", BUTTON_OK);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc(zip_size);
    if (!buf) {
        fm->fclose(file);
        if (ui) ui->popup("Not enough memory for ZIP.", BUTTON_OK);
        return NULL;
    }

    uint32_t bytes_read = 0;
    fres = file->read(buf, zip_size, &bytes_read);
    fm->fclose(file);

    if (fres != FR_OK || bytes_read != zip_size) {
        free(buf);
        if (ui) ui->popup("Error reading ZIP file.", BUTTON_OK);
        return NULL;
    }

    out_size = zip_size;
    return buf;
}

// ---- BrowsableZipEntry ----

BrowsableZipEntry::BrowsableZipEntry(const char *zip_dir_, const char *zip_name_, int idx,
                                     const char *path, uint32_t size, bool dir)
    : Browsable(!dir),
      zip_dir(zip_dir_),
      zip_name(zip_name_),
      entry_idx(idx),
      entry_path(path),
      uncomp_size(size),
      is_dir(dir)
{
}

void BrowsableZipEntry::getDisplayString(char *buffer, int width)
{
    if (is_dir) {
        sprintf(buffer, "\eR%s\er", entry_path.c_str());
    } else {
        sprintf(buffer, "%s  %u", entry_path.c_str(), (unsigned)uncomp_size);
    }
}

void BrowsableZipEntry::fetch_context_items(IndexedList<Action *> &items)
{
    if (!is_dir) {
        items.append(new Action("Extract here", BrowsableZipEntry::extract_one, 0, (int)this));
    }
}

SubsysResultCode_e BrowsableZipEntry::extract_one(SubsysCommand *cmd)
{
    BrowsableZipEntry *entry = (BrowsableZipEntry *)cmd->mode;
    UserInterface *ui = cmd->user_interface;
    FileManager *fm = FileManager::getFileManager();

    uint32_t zip_size = 0;
    uint8_t *buf = load_zip_to_mem(ui, entry->zip_dir.c_str(), entry->zip_name.c_str(), zip_size);
    if (!buf) return SSRET_DISK_ERROR;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, buf, zip_size, 0)) {
        free(buf);
        if (ui) ui->popup("Invalid ZIP file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, (mz_uint)entry->entry_idx, &stat)) {
        mz_zip_reader_end(&zip);
        free(buf);
        if (ui) ui->popup("Entry not found in ZIP.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Destination: the directory containing the ZIP
    char dest_dir[512];
    strncpy(dest_dir, entry->zip_dir.c_str(), sizeof(dest_dir) - 1);
    dest_dir[sizeof(dest_dir) - 1] = '\0';
    int dlen = strip_trailing_slash(dest_dir);

    // If entry has subdirectory components, create them and extend dest_dir
    const char *epath = entry->entry_path.c_str();
    const char *last_slash = strrchr(epath, '/');
    const char *basename = last_slash ? last_slash + 1 : epath;

    if (last_slash) {
        int sub_len = (int)(last_slash - epath);
        ensure_zip_path(fm, dest_dir, epath);
        if (dlen + 1 + sub_len < (int)sizeof(dest_dir) - 1) {
            dest_dir[dlen] = '/';
            memcpy(dest_dir + dlen + 1, epath, sub_len);
            dest_dir[dlen + 1 + sub_len] = '\0';
        }
    }

    size_t out_size = 0;
    void *out_data = mz_zip_reader_extract_to_heap(&zip, (mz_uint)entry->entry_idx, &out_size, 0);
    mz_zip_reader_end(&zip);
    free(buf);

    if (!out_data) {
        if (ui) ui->popup("Failed to extract entry.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t written = 0;
    FRESULT fres = fm->save_file(true, dest_dir, basename,
                                 (uint8_t *)out_data, (uint32_t)out_size, &written);
    mz_free(out_data);

    if (fres != FR_OK || written != (uint32_t)out_size) {
        if (ui) ui->popup("Error writing extracted file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    if (ui) {
        char msg[128];
        sprintf(msg, "Extracted: %s", basename);
        ui->popup(msg, BUTTON_OK);
    }
    return SSRET_OK;
}

// ---- FileTypeZIP ----

FileTypeZIP::FileTypeZIP(BrowsableDirEntry *n) : node(n)
{
    printf("Creating ZIP type from: %s\n", n->getName());
}

// static
FileType *FileTypeZIP::test_type(BrowsableDirEntry *obj)
{
    FileInfo *inf = obj->getInfo();
    if (strcmp(inf->extension, "ZIP") == 0)
        return new FileTypeZIP(obj);
    return NULL;
}

int FileTypeZIP::fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Unzip to folder", FileTypeZIP::do_unzip_to_folder, 0, (int)this));
    list.append(new Action("Unzip here",      FileTypeZIP::do_unzip_here,      0, (int)this));
    return 2;
}

int FileTypeZIP::getCustomBrowsables(Browsable *parent, IndexedList<Browsable *> &list)
{
    BrowsableDirEntry *dir_entry = (BrowsableDirEntry *)parent;
    const char *zip_path = dir_entry->getPath()->get_path(); // includes trailing slash
    const char *zip_name = dir_entry->getName();

    uint32_t zip_size = 0;
    uint8_t *buf = load_zip_to_mem(NULL, zip_path, zip_name, zip_size);
    if (!buf) return -1;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, buf, zip_size, 0)) {
        free(buf);
        return -1;
    }

    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        list.append(new BrowsableZipEntry(zip_path, zip_name, (int)i,
                                          stat.m_filename,
                                          (uint32_t)stat.m_uncomp_size,
                                          stat.m_is_directory != 0));
    }

    mz_zip_reader_end(&zip);
    free(buf);
    return (int)num_files;
}

// static
SubsysResultCode_e FileTypeZIP::do_unzip_to_folder(SubsysCommand *cmd)
{
    return do_extract(cmd, true);
}

// static
SubsysResultCode_e FileTypeZIP::do_unzip_here(SubsysCommand *cmd)
{
    return do_extract(cmd, false);
}

// static
SubsysResultCode_e FileTypeZIP::do_extract(SubsysCommand *cmd, bool to_folder)
{
    UserInterface *ui = cmd->user_interface;
    FileManager *fm = FileManager::getFileManager();

    uint32_t zip_size = 0;
    uint8_t *buf = load_zip_to_mem(ui, cmd->path.c_str(), cmd->filename.c_str(), zip_size);
    if (!buf) return SSRET_DISK_ERROR;

    char dest_path[512];

    if (to_folder) {
        // Strip .zip extension from filename to get folder name
        char dir_name[128];
        strncpy(dir_name, cmd->filename.c_str(), sizeof(dir_name) - 1);
        dir_name[sizeof(dir_name) - 1] = '\0';
        int len = (int)strlen(dir_name);
        // Case-insensitive strip of ".zip" suffix
        if (len > 4 && dir_name[len-4] == '.' &&
            (dir_name[len-3] == 'z' || dir_name[len-3] == 'Z') &&
            (dir_name[len-2] == 'i' || dir_name[len-2] == 'I') &&
            (dir_name[len-1] == 'p' || dir_name[len-1] == 'P')) {
            dir_name[len - 4] = '\0';
        }
        if (strlen(dir_name) > 60) dir_name[60] = '\0';

        // Build base path without trailing slash
        char base[512];
        strncpy(base, cmd->path.c_str(), sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        strip_trailing_slash(base);

        // Try creating the folder, handle name collisions
        char try_name[128];
        strncpy(try_name, dir_name, sizeof(try_name) - 1);
        try_name[sizeof(try_name) - 1] = '\0';

        int attempt = 0;
        FRESULT fres;
        while (true) {
            sprintf(dest_path, "%s/%s", base, try_name);
            fres = fm->create_dir(dest_path);
            if (fres == FR_OK) break;
            if (fres == FR_EXIST) {
                attempt++;
                if (attempt > 99) {
                    free(buf);
                    if (ui) ui->popup("Cannot create destination folder.", BUTTON_OK);
                    return SSRET_DISK_ERROR;
                }
                sprintf(try_name, "%s_%d", dir_name, attempt);
                continue;
            }
            free(buf);
            if (ui) ui->popup(FileSystem::get_error_string(fres), BUTTON_OK);
            return SSRET_DISK_ERROR;
        }
    } else {
        strncpy(dest_path, cmd->path.c_str(), sizeof(dest_path) - 1);
        dest_path[sizeof(dest_path) - 1] = '\0';
        strip_trailing_slash(dest_path);
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, buf, zip_size, 0)) {
        free(buf);
        if (ui) ui->popup("Invalid ZIP file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    if (ui) ui->show_progress("Extracting...", (int)num_files);

    int extracted = 0;
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            if (ui) ui->update_progress(NULL, 1);
            continue;
        }

        // Create all required parent directories
        ensure_zip_path(fm, dest_path, stat.m_filename);

        if (stat.m_is_directory) {
            // Also create the directory entry itself (ensure_zip_path handles parents,
            // but for a trailing-slash entry we need to create the final component too)
            char dir_entry_name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
            strncpy(dir_entry_name, stat.m_filename, sizeof(dir_entry_name) - 1);
            dir_entry_name[sizeof(dir_entry_name) - 1] = '\0';
            strip_trailing_slash(dir_entry_name);
            if (strlen(dir_entry_name) > 0) {
                char full[512];
                sprintf(full, "%s/%s", dest_path, dir_entry_name);
                fm->create_dir(full);
            }
            if (ui) ui->update_progress(NULL, 1);
            continue;
        }

        // Determine dest directory and basename for this file
        const char *entry_name = stat.m_filename;
        const char *last_slash = strrchr(entry_name, '/');
        const char *basename = last_slash ? last_slash + 1 : entry_name;

        if (!basename || strlen(basename) == 0) {
            if (ui) ui->update_progress(NULL, 1);
            continue;
        }

        char entry_dest_dir[512];
        if (last_slash) {
            int dir_len = (int)(last_slash - entry_name);
            // Build: dest_path + "/" + dir_part
            strncpy(entry_dest_dir, dest_path, sizeof(entry_dest_dir) - 1);
            entry_dest_dir[sizeof(entry_dest_dir) - 1] = '\0';
            int cur_len = (int)strlen(entry_dest_dir);
            if (cur_len + 1 + dir_len < (int)sizeof(entry_dest_dir) - 1) {
                entry_dest_dir[cur_len] = '/';
                memcpy(entry_dest_dir + cur_len + 1, entry_name, dir_len);
                entry_dest_dir[cur_len + 1 + dir_len] = '\0';
            }
        } else {
            strncpy(entry_dest_dir, dest_path, sizeof(entry_dest_dir) - 1);
            entry_dest_dir[sizeof(entry_dest_dir) - 1] = '\0';
        }

        size_t out_size = 0;
        void *out_data = mz_zip_reader_extract_to_heap(&zip, i, &out_size, 0);
        if (out_data) {
            uint32_t written = 0;
            fm->save_file(true, entry_dest_dir, basename,
                          (uint8_t *)out_data, (uint32_t)out_size, &written);
            mz_free(out_data);
            extracted++;
        }

        if (ui) ui->update_progress(NULL, 1);
    }

    mz_zip_reader_end(&zip);
    free(buf);

    if (ui) {
        ui->hide_progress();
        char msg[128];
        const char *dest_name = strrchr(dest_path, '/');
        dest_name = dest_name ? dest_name + 1 : dest_path;
        sprintf(msg, "Extracted %d file(s) to %s/", extracted, dest_name);
        ui->popup(msg, BUTTON_OK);
    }

    return SSRET_OK;
}
