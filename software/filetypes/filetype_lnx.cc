/*
 * filetype_lnx.cc
 *
 * Lynx archive (.lnx) to D64 converter.
 * Parses the Lynx container format and writes each file into
 * a freshly formatted D64 disk image.
 */

#include "filetype_lnx.h"
#include "filemanager.h"
#include "userinterface.h"
#include "disk_image.h"
#include "blockdev_ram.h"
#include "filesystem_d64.h"
#include <stdlib.h>
#include <string.h>

// Workaround: httpd/server.h defines "close" as macro for lwip_close
#undef close

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_lnx(FileType::getFileTypeFactory(), FileTypeLNX::test_type);

FileTypeLNX::FileTypeLNX(BrowsableDirEntry *n) : node(n)
{
}

// static
FileType *FileTypeLNX::test_type(BrowsableDirEntry *obj)
{
    FileInfo *inf = obj->getInfo();
    if (strcmp(inf->extension, "LNX") == 0)
        return new FileTypeLNX(obj);
    return NULL;
}

int FileTypeLNX::fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Convert to D64", FileTypeLNX::do_convert_d64, 0, (int)this));
    return 1;
}

// Read a CR-terminated string from buffer. Returns number of bytes consumed (including CR).
static int read_cr_string(const uint8_t *buf, int max, char *out, int out_size)
{
    int i = 0;
    int j = 0;
    while (i < max && buf[i] != 0x0D) {
        if (j < out_size - 1) {
            out[j++] = buf[i];
        }
        i++;
    }
    out[j] = '\0';
    if (i < max && buf[i] == 0x0D) i++; // skip CR
    return i;
}

// Read a CR-terminated ASCII decimal number from buffer.
static int read_cr_int(const uint8_t *buf, int max, int *value)
{
    char tmp[16];
    int consumed = read_cr_string(buf, max, tmp, sizeof(tmp));
    *value = (int)strtol(tmp, NULL, 10);
    return consumed;
}

// Map Lynx file type char to CBM file type byte
static uint8_t lnx_type_to_cbm(char type_char)
{
    switch (type_char) {
    case 'P': return 0x82; // PRG
    case 'S': return 0x81; // SEQ
    case 'U': return 0x83; // USR
    case 'R': return 0x84; // REL
    case 'D': return 0x80; // DEL
    default:  return 0x82; // default PRG
    }
}

struct LnxEntry {
    char name[20];
    int  blocks;
    char type_char;
    int  last_block_size;
    int  byte_size;     // computed: (blocks-1)*254 + last_block_size - 1
};

// static
SubsysResultCode_e FileTypeLNX::do_convert_d64(SubsysCommand *cmd)
{
    FileTypeLNX *typ = (FileTypeLNX *)cmd->functionID;
    UserInterface *ui = cmd->user_interface;
    FileManager *fm = FileManager::getFileManager();

    const char *lnx_path = cmd->path.c_str();
    const char *lnx_name = cmd->filename.c_str();

    // Load LNX file into memory
    File *file = NULL;
    FRESULT fres = fm->fopen(lnx_path, lnx_name, FA_READ, &file);
    if (!file || fres != FR_OK) {
        if (ui) ui->popup("Cannot open .lnx file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t lnx_size = file->get_size();
    if (lnx_size > 4 * 1024 * 1024) {
        fm->fclose(file);
        if (ui) ui->popup("LNX file too large.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint8_t *lnx_buf = (uint8_t *)malloc(lnx_size);
    if (!lnx_buf) {
        fm->fclose(file);
        if (ui) ui->popup("Not enough memory.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    uint32_t bytes_read = 0;
    fres = file->read(lnx_buf, lnx_size, &bytes_read);
    fm->fclose(file);

    if (fres != FR_OK || bytes_read != lnx_size) {
        free(lnx_buf);
        if (ui) ui->popup("Error reading file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Validate: must start with $01 $08 (BASIC load address)
    if (lnx_size < 100 || lnx_buf[0] != 0x01 || lnx_buf[1] != 0x08) {
        free(lnx_buf);
        if (ui) ui->popup("Not a valid LNX file.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Find end of BASIC stub: three consecutive zero bytes
    int pos = 2;
    while (pos < (int)lnx_size - 2) {
        if (lnx_buf[pos] == 0 && lnx_buf[pos + 1] == 0 && lnx_buf[pos + 2] == 0) {
            pos += 3;
            break;
        }
        pos++;
    }

    if (pos >= (int)lnx_size) {
        free(lnx_buf);
        if (ui) ui->popup("Cannot find LNX header.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Skip CR after BASIC stub
    if (pos < (int)lnx_size && lnx_buf[pos] == 0x0D) pos++;

    // Read directory block count and file count
    int dir_blocks = 0;
    int file_count = 0;
    int remaining = (int)lnx_size - pos;

    pos += read_cr_int(lnx_buf + pos, remaining, &dir_blocks);
    remaining = (int)lnx_size - pos;
    pos += read_cr_int(lnx_buf + pos, remaining, &file_count);

    printf("[LNX] dir_blocks=%d, file_count=%d\n", dir_blocks, file_count);

    if (file_count <= 0 || file_count > 144 || dir_blocks <= 0) {
        free(lnx_buf);
        if (ui) ui->popup("Invalid LNX directory.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Parse directory entries
    LnxEntry *entries = new LnxEntry[file_count];
    for (int i = 0; i < file_count; i++) {
        remaining = (int)lnx_size - pos;
        if (remaining <= 0) {
            file_count = i; // truncate
            break;
        }

        // Filename (CR-terminated)
        pos += read_cr_string(lnx_buf + pos, remaining, entries[i].name, sizeof(entries[i].name));
        remaining = (int)lnx_size - pos;

        // Block count (CR-terminated)
        pos += read_cr_int(lnx_buf + pos, remaining, &entries[i].blocks);
        remaining = (int)lnx_size - pos;

        // File type char + CR
        char type_str[8];
        pos += read_cr_string(lnx_buf + pos, remaining, type_str, sizeof(type_str));
        entries[i].type_char = type_str[0];
        remaining = (int)lnx_size - pos;

        // Last block size (CR-terminated)
        pos += read_cr_int(lnx_buf + pos, remaining, &entries[i].last_block_size);

        // Compute byte size
        if (entries[i].blocks > 0 && entries[i].last_block_size > 0) {
            entries[i].byte_size = (entries[i].blocks - 1) * 254 + entries[i].last_block_size - 1;
        } else {
            entries[i].byte_size = 0;
        }

        printf("[LNX] %d: '%s' %c %d blocks (%d bytes)\n",
               i, entries[i].name, entries[i].type_char, entries[i].blocks, entries[i].byte_size);
    }

    // Data starts at: 2 (load addr) + dir_blocks * 254
    int data_offset = 2 + dir_blocks * 254;
    if (data_offset >= (int)lnx_size) {
        delete[] entries;
        free(lnx_buf);
        if (ui) ui->popup("LNX data offset out of range.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Create D64 in memory using BinImage
    BinImage *bin = new BinImage("LNX Convert", 35);
    if (!bin) {
        delete[] entries;
        free(lnx_buf);
        if (ui) ui->popup("Not enough memory for D64.", BUTTON_OK);
        return SSRET_DISK_ERROR;
    }

    // Use LNX filename (without extension) as disk name
    char disk_name[24];
    strncpy(disk_name, lnx_name, sizeof(disk_name) - 1);
    disk_name[sizeof(disk_name) - 1] = '\0';
    int nlen = (int)strlen(disk_name);
    if (nlen > 4 && disk_name[nlen - 4] == '.') {
        disk_name[nlen - 4] = '\0';
    }
    bin->format(disk_name);

    // Open D64 filesystem on the BinImage
    BlockDevice_Ram *blk = new BlockDevice_Ram(bin->bin_data, 256, 683);
    Partition *prt = new Partition(blk, 0, 683, 0);
    FileSystemD64 *d64fs = new FileSystemD64(prt, false);

    if (ui) ui->show_progress("Converting LNX...", file_count);

    int data_pos = data_offset;
    int written_count = 0;
    for (int i = 0; i < file_count; i++) {
        if (data_pos + entries[i].blocks * 254 > (int)lnx_size + 254) {
            printf("[LNX] File %d overruns buffer, stopping\n", i);
            break;
        }

        if (entries[i].byte_size > 0) {
            File *d64file = NULL;
            fres = d64fs->file_open(entries[i].name, FA_WRITE | FA_CREATE_NEW, &d64file);
            if (d64file) {
                uint32_t wr = 0;
                d64file->write(lnx_buf + data_pos, entries[i].byte_size, &wr);
                d64file->close();
                written_count++;
            } else {
                printf("[LNX] Cannot create '%s' in D64: %d\n", entries[i].name, fres);
            }
        }

        data_pos += entries[i].blocks * 254;
        if (ui) ui->update_progress(NULL, 1);
    }

    // Sync filesystem
    d64fs->sync();

    delete d64fs;
    delete prt;
    delete blk;
    delete[] entries;
    free(lnx_buf);

    // Save D64 to disk
    char d64_name[128];
    strncpy(d64_name, disk_name, sizeof(d64_name) - 5);
    d64_name[sizeof(d64_name) - 5] = '\0';
    fix_filename(d64_name);
    strcat(d64_name, ".d64");

    // Strip trailing slash from path
    char dest_path[512];
    strncpy(dest_path, lnx_path, sizeof(dest_path) - 1);
    dest_path[sizeof(dest_path) - 1] = '\0';
    int dlen = (int)strlen(dest_path);
    if (dlen > 1 && dest_path[dlen - 1] == '/') {
        dest_path[dlen - 1] = '\0';
    }

    File *d64file = NULL;
    fres = fm->fopen(dest_path, d64_name, FA_WRITE | FA_CREATE_NEW, &d64file);
    if (d64file) {
        bin->save(d64file, ui);
        fm->fclose(d64file);
    } else {
        delete bin;
        if (ui) {
            ui->hide_progress();
            ui->popup("Cannot create D64 file.", BUTTON_OK);
        }
        return SSRET_DISK_ERROR;
    }

    delete bin;

    if (ui) {
        ui->hide_progress();
        char msg[160];
        sprintf(msg, "Converted %d file(s) to %s", written_count, d64_name);
        ui->popup(msg, BUTTON_OK);
    }
    return SSRET_OK;
}
