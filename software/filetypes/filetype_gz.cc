/*
 * filetype_gz.cc
 *
 * Gzip decompression file type handler.
 * Uses miniz's tinfl (raw DEFLATE) with manual gzip header/trailer parsing.
 */

#include "filetype_gz.h"
#include "filemanager.h"
#include "userinterface.h"
#include "miniz_tinfl.h"
#include <stdlib.h>
#include <string.h>

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_gz(FileType::getFileTypeFactory(), FileTypeGZ::test_type);

FileTypeGZ::FileTypeGZ(BrowsableDirEntry *n) : node(n)
{
}

// static
FileType *FileTypeGZ::test_type(BrowsableDirEntry *obj)
{
    FileInfo *inf = obj->getInfo();
    if (strcmp(inf->extension, "GZ") == 0)
        return new FileTypeGZ(obj);
    return NULL;
}

int FileTypeGZ::fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Decompress", FileTypeGZ::do_decompress, 0, (int)this));
    return 1;
}

// Parse gzip header (RFC 1952). Returns offset to compressed data, or -1 on error.
static int parse_gzip_header(const uint8_t *buf, uint32_t size, char *orig_name, int name_size)
{
    orig_name[0] = '\0';

    if (size < 10) return -1;
    if (buf[0] != 0x1F || buf[1] != 0x8B) return -1; // magic
    if (buf[2] != 8) return -1; // method must be deflate

    uint8_t flags = buf[3];
    // flags: bit 0=FTEXT, 1=FHCRC, 2=FEXTRA, 3=FNAME, 4=FCOMMENT
    int pos = 10;

    // FEXTRA
    if (flags & 0x04) {
        if (pos + 2 > (int)size) return -1;
        int xlen = buf[pos] | (buf[pos + 1] << 8);
        pos += 2 + xlen;
    }

    // FNAME — original filename (Latin-1, zero-terminated)
    if (flags & 0x08) {
        int start = pos;
        while (pos < (int)size && buf[pos] != 0) pos++;
        if (pos >= (int)size) return -1;
        int len = pos - start;
        if (len >= name_size) len = name_size - 1;
        memcpy(orig_name, buf + start, len);
        orig_name[len] = '\0';
        pos++; // skip null terminator
    }

    // FCOMMENT
    if (flags & 0x10) {
        while (pos < (int)size && buf[pos] != 0) pos++;
        if (pos >= (int)size) return -1;
        pos++;
    }

    // FHCRC
    if (flags & 0x02) {
        pos += 2;
    }

    if (pos >= (int)size) return -1;
    return pos;
}

// static
SubsysResultCode_e FileTypeGZ::do_decompress(SubsysCommand *cmd)
{
    FileTypeGZ *typ = (FileTypeGZ *)cmd->functionID;
    UserInterface *ui = cmd->user_interface;
    FileManager *fm = FileManager::getFileManager();

    const char *gz_path = cmd->path.c_str();
    const char *gz_name = cmd->filename.c_str();

    // Load compressed file into memory
    File *file = NULL;
    FRESULT fres = fm->fopen(gz_path, gz_name, FA_READ, &file);
    if (!file || fres != FR_OK) {
        if (ui) ui->popup("Cannot open .gz file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t gz_size = file->get_size();
    if (gz_size > 8 * 1024 * 1024) {
        fm->fclose(file);
        if (ui) ui->popup("File too large (>8MB).", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint8_t *gz_buf = (uint8_t *)malloc(gz_size);
    if (!gz_buf) {
        fm->fclose(file);
        if (ui) ui->popup("Not enough memory.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t bytes_read = 0;
    fres = file->read(gz_buf, gz_size, &bytes_read);
    fm->fclose(file);

    if (fres != FR_OK || bytes_read != gz_size) {
        free(gz_buf);
        if (ui) ui->popup("Error reading file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Parse gzip header
    char orig_name[128];
    int data_offset = parse_gzip_header(gz_buf, gz_size, orig_name, sizeof(orig_name));
    if (data_offset < 0) {
        free(gz_buf);
        if (ui) ui->popup("Invalid gzip header.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Read uncompressed size from trailer (last 4 bytes, little-endian)
    if (gz_size < (uint32_t)(data_offset + 8)) {
        free(gz_buf);
        if (ui) ui->popup("Truncated gzip file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t uncomp_size = gz_buf[gz_size - 4]
                         | (gz_buf[gz_size - 3] << 8)
                         | (gz_buf[gz_size - 2] << 16)
                         | (gz_buf[gz_size - 1] << 24);

    if (uncomp_size > 16 * 1024 * 1024) {
        free(gz_buf);
        if (ui) ui->popup("Uncompressed size too large.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Decompress using tinfl (raw DEFLATE, no zlib header)
    uint8_t *out_buf = (uint8_t *)malloc(uncomp_size);
    if (!out_buf) {
        free(gz_buf);
        if (ui) ui->popup("Not enough memory for output.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    size_t comp_size = gz_size - data_offset - 8; // exclude header and 8-byte trailer
    size_t out_len = tinfl_decompress_mem_to_mem(out_buf, uncomp_size,
                                                  gz_buf + data_offset, comp_size,
                                                  0); // flags=0 for raw deflate (no zlib header)
    free(gz_buf);

    if (out_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || out_len != uncomp_size) {
        free(out_buf);
        if (ui) ui->popup("Decompression failed.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Determine output filename
    char out_name[128];
    if (orig_name[0]) {
        // Use original name from gzip header
        strncpy(out_name, orig_name, sizeof(out_name) - 1);
        out_name[sizeof(out_name) - 1] = '\0';
    } else {
        // Strip .gz extension from input filename
        strncpy(out_name, gz_name, sizeof(out_name) - 1);
        out_name[sizeof(out_name) - 1] = '\0';
        int len = (int)strlen(out_name);
        if (len > 3 && out_name[len - 3] == '.' &&
            (out_name[len - 2] == 'g' || out_name[len - 2] == 'G') &&
            (out_name[len - 1] == 'z' || out_name[len - 1] == 'Z')) {
            out_name[len - 3] = '\0';
        }
    }

    // Save decompressed file
    uint32_t written = 0;
    char dest_path[512];
    strncpy(dest_path, gz_path, sizeof(dest_path) - 1);
    dest_path[sizeof(dest_path) - 1] = '\0';
    // Strip trailing slash
    int dlen = (int)strlen(dest_path);
    if (dlen > 1 && dest_path[dlen - 1] == '/') {
        dest_path[dlen - 1] = '\0';
    }

    fres = fm->save_file(true, dest_path, out_name, out_buf, uncomp_size, &written);
    free(out_buf);

    if (fres != FR_OK || written != uncomp_size) {
        if (ui) ui->popup("Error writing output file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    if (ui) {
        char msg[160];
        sprintf(msg, "Decompressed: %s (%u bytes)", out_name, uncomp_size);
        ui->popup(msg, BUTTON_OK);
    }
    return SSRET_OK;
}
