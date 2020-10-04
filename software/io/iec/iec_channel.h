#ifndef IEC_CHANNEL_H
#define IEC_CHANNEL_H

#include "integer.h"
#include "iec.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "filemanager.h"
#include "mystring.h"

typedef enum _t_channel_state {
    e_idle, e_filename, e_file, e_dir, e_record, e_complete, e_error, e_status
    
} t_channel_state;

enum t_channel_retval {
    IEC_OK=0, IEC_LAST=1, IEC_NO_DATA=-1, IEC_FILE_NOT_FOUND=-2, IEC_NO_FILE=-3,
    IEC_READ_ERROR=-4, IEC_WRITE_ERROR=-5, IEC_BYTE_LOST=-6, IEC_BUFFER_END=-7
};

static uint8_t c_header[32] = { 1,  1,  4,  1,  0,  0, 18, 34,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            34, 32, 48, 48, 32, 50, 65,  0 };

class IecCommandChannel;

typedef enum _access_mode_t {
    e_undefined = 0,
    e_read,
    e_write,
    e_replace,
    e_append,
    e_relative,
} access_mode_t;

typedef struct _name_t {
    int drive;
    char *name;
    bool directory;
    bool explicitExt;
    const char *extension;
    access_mode_t mode;
    uint8_t recordSize;
} name_t;

typedef struct _command_t {
    char cmd[16];
    int digits;
    char *remaining;
} command_t;

#define MAX_PARTITIONS 256

class IecFileSystem;

class IecPartition
{
    IndexedList<FileInfo *> *dirlist;
    IndexedList<char *> *iecNames;
    Path *path;
    FileManager *fm;
    IecFileSystem *vfs;
    int partitionNumber;

public:
    IecPartition(IecFileSystem *vfs, int nr) {
        fm = FileManager :: getFileManager();
        this->vfs = vfs;
        partitionNumber = nr;
        path = fm->get_new_path("IEC Partition");
        dirlist = NULL;
        iecNames = NULL;
        dirlist = new IndexedList<FileInfo *>(8, NULL);
        iecNames = new IndexedList<char *>(8, NULL);

        SetInitialPath(); // constructs root path string
    }

    int GetPartitionNumber(void)
    {
        return partitionNumber;
    }

    void SetInitialPath(void);
    bool IsValid(); // implemented in iec_channel.cc, to resolve an illegal forward reference

    char *CreateIecName(FileInfo *inf)
    {
        bool dir  = inf->attrib & AM_DIR;
        char *ext = inf->extension;
        char *out = new char[24];
        memset(out, 0, 24);

        if (inf->name_format & NAME_FORMAT_CBM) {
            // Format is already CBM; simply copy the parts together
            memcpy(out, ext, 3);
            strncpy(out+3, inf->lfname, 16);
        } else {
            bool cutExtension = false;
            if (dir) {
                memcpy(out, "DIR", 3);
            } else if (strcmp(ext, "PRG") == 0) {
                memcpy(out, ext, 3);
                cutExtension = true;
            } else if (strcmp(ext, "SEQ") == 0) {
                memcpy(out, ext, 3);
                cutExtension = true;
            } else if (strcmp(ext, "REL") == 0) {
                memcpy(out, ext, 3);
                cutExtension = true;
            } else if (strcmp(ext, "USR") == 0) {
                memcpy(out, ext, 3);
                cutExtension = true;
            } else {
                memcpy(out, "SEQ", 3);
            }
            fat_to_petscii(inf->lfname, cutExtension, out+3, 16, true);
        }
        return out;
    }

    int FindIecName(const char *name, const char *ext, bool allowDir)
    {
        char temp[32];
        if (ext[0]=='.')
            ext++;
        temp[0] = toupper(ext[0]);
        temp[1] = toupper(ext[1]);
        temp[2] = toupper(ext[2]);

        strncpy(temp+3, name, 28);

        for(int i=0;i<iecNames->get_elements();i++) {
            if (pattern_match(temp, (*iecNames)[i], false)) {
                if (allowDir || !((*dirlist)[i]->attrib & AM_DIR)) {
                    return i;
                }
            }
        }
        return -1;
    }

    void CleanupDir() {
        if (!dirlist)
            return;
        for(int i=0;i < dirlist->get_elements();i++) {
            delete (*dirlist)[i];
            delete (*iecNames)[i];

        }
        dirlist->clear_list();
        iecNames->clear_list();
    }

    FRESULT ReadDirectory()
    {
        CleanupDir();
        FRESULT res = fm->get_directory(path, *dirlist, NULL);
        for(int i=0;i<dirlist->get_elements();i++) {
            FileInfo *inf = (*dirlist)[i];
            iecNames->append(CreateIecName(inf));
        }
        return res;
    }

    int GetDirItemCount(void)
    {
        return dirlist->get_elements();
    }

    const char *GetIecName(int index)
    {
        return (*iecNames)[index];
    }

    FileInfo *GetSystemFile(int index)
    {
        return (*dirlist)[index];
    }

    Path *GetPath()
    {
        return path;
    }

    const char *GetFullPath()
    {
        return path->get_path();
    }

    FRESULT MakeDirectory(const char *name)
    {
        FRESULT res = fm->create_dir(path, name);
        return res;
    }

    FRESULT RemoveFile(const char *name)
    {
        FRESULT res = fm->delete_file(path, name);
        return res;
    }

    bool cd(const char *name)
    {
        if (name[0] == '_') { // left arrow
            // return to root of partition
            SetInitialPath();
            if (!fm->is_path_valid(path)) {
                CleanupDir();
                return false;
            }
            FRESULT res = ReadDirectory(); // just try!
            if (res == FR_OK) {
                return true;
            }
            return false;
        }

        // Not the <- case
        int index = FindIecName(name, "DIR", true);
        if (index >= 0) {
            FileInfo *inf = (*dirlist)[index];
            name = inf->lfname;
        }
        mstring previous_path(path->get_path());
        path->cd(name);  // just try!
        if (!fm->is_path_valid(path)) {
            path->cd(previous_path.c_str()); // revert
            return false;
        }
        FRESULT res = ReadDirectory(); // just try!
        if (res == FR_OK) {
            return true;
        }
        path->cd(previous_path.c_str()); // revert
        ReadDirectory();
        return false;
    }

    int Remove(command_t& command, bool dir) {
        if (!command.remaining) {
            return -1;
        }
        int f = 0;
        char *filenames[5] = { 0, 0, 0, 0, 0 };
        split_string(',', command.remaining, filenames, 5);
        ReadDirectory();
        for(int i=0;i<5;i++) {
            if (!filenames[i]) {
                break;
            }
            //name_t name;
            //parse_filename(filenames[i], &name, false);
            int idx = -1;
            for (int fl=0; fl < iecNames->get_elements(); fl++) {
                char *iecName = (*iecNames)[fl];
                if (pattern_match(filenames[i], &iecName[3], false)) {
                    FileInfo *inf = (*dirlist)[fl];
                    if ( ((inf->attrib & AM_DIR) && !dir) || (!(inf->attrib & AM_DIR) && dir) ) {
                        continue; // skip entries that are not of the right type
                    }
                    FRESULT res = RemoveFile(inf->lfname);
                    if (res == FR_OK) {
                        f++;
                        dirlist->remove(inf);
                        iecNames->remove(iecName);
                        fl--;
                    }
                }
            }

        }
        return f;
    }
};

class IecFileSystem
{
    FileManager *fm;
    int currentPartition;
    IecPartition *partitions[MAX_PARTITIONS];
    IecInterface *interface;
public:
    IecFileSystem(IecInterface *intf)
    {
        interface = intf;
        fm = FileManager :: getFileManager();
        for (int i=0; i<MAX_PARTITIONS; i++) {
            partitions[i] = 0;
        }
        currentPartition = 0;
        partitions[0] = new IecPartition(this, 0);
    }

    const char *GetRootPath() {
        return interface->get_root_path();
    }

    IecPartition *GetPartition(int index) {
        if (index < 0) {
            index = currentPartition;
        }
        if (!partitions[index]) {
            partitions[index] = new IecPartition(this, index);
        }
        return partitions[index];
    }

    void SetCurrentPartition(int pn) {
        currentPartition = pn;
    }
};


class IecChannel
{
    IecInterface *interface;
    FileManager *fm;

	int  channel;
    uint8_t buffer[256];
    int  size;
    int  pointer;
    int  prefetch;
    int  prefetch_max;
    File *f;
    int  dir_index;
    int  dir_last;
    IecPartition *dirPartition;
    t_channel_state state;
    mstring dirpattern;
    int  last_byte;
    uint32_t recordOffset;
    uint8_t recordSize;
    bool recordDirty;

    // temporaries
    uint32_t bytes;
    name_t name;
    uint8_t flags;
    IecPartition *partition;
    char fs_filename[64];

private:
    bool parse_filename(char *buffer, name_t *name, int default_drive, bool doFlags);
    int  setup_directory_read(name_t& name);
    int  setup_file_access(name_t &name);
    int  init_iec_transfer(void);

    int open_file(void);  // name should be in buffer
    int close_file(void); // file should be open
    int read_dir_entry(void);
    int read_block(void);
    int read_record(int offset);
    int write_record(void);

    void dump_command(command_t& cmd);
    void dump_name(name_t& name, const char *id);
    bool hasIllegalChars(const char *name);

public:
    IecChannel(IecInterface *intf, int ch);
    virtual ~IecChannel();
    virtual void reset(void);
    virtual void talk(void) { }
    virtual void reset_prefetch(void);
    virtual int prefetch_data(uint8_t& data);
    virtual int pop_data(void);
    virtual int push_data(uint8_t b);
    virtual int push_command(uint8_t b);

    virtual int ext_open_file(const char *name);
    virtual int ext_close_file(void);

    void seek_record(const uint8_t *);
    friend class IecCommandChannel;
};

class IecCommandChannel : public IecChannel
{
    uint8_t wr_buffer[64];
    int  wr_pointer;

    void mem_read(void);
    void mem_write(void);
    void renam(command_t& command);
    void copy(command_t& command);
    void exec_command(command_t &command);
    void get_error_string(void);
    bool parse_command(char *buffer, int length, command_t *command);
public:
    IecCommandChannel(IecInterface *intf, int ch);
    virtual ~IecCommandChannel();
    void set_error(int err = -1, int track = 0, int sector = 0);
    void reset(void);
    void talk(void);
    int pop_data(void);
    int push_data(uint8_t b);
    int push_command(uint8_t b);
    int ext_open_file(const char *name);
    int ext_close_file(void);
};

#endif /* IEC_CHANNEL_H */
