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
    e_idle, e_filename, e_file, e_dir, e_complete, e_error
    
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

typedef struct {
    int drive;
    char *name;
    char separator;
} name_t;

typedef struct {
    char *cmd;
    int digits;
    name_t names[5];
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

    char *CreateIecName(char *in, char *ext, bool dir)
    {
        char *out = new char[24];
        memset(out, 0, 24);

        char temp[64];
        strncpy(temp, in, 64);

        if (dir) {
            memcpy(out, "DIR", 3);
        } else if (strcmp(ext, "PRG") == 0) {
            memcpy(out, ext, 3);
            set_extension(temp, "", 64);
        } else if (strcmp(ext, "SEQ") == 0) {
            memcpy(out, ext, 3);
            set_extension(temp, "", 64);
        } else if (strcmp(ext, "REL") == 0) {
            memcpy(out, ext, 3);
            set_extension(temp, "", 64);
        } else if (strcmp(ext, "USR") == 0) {
            memcpy(out, ext, 3);
            set_extension(temp, "", 64);
        } else {
            memcpy(out, "SEQ", 3);
        }

        for(int i=0;i<16;i++) {
            char o;
            if(temp[i] == 0) {
                break;
            } else if(temp[i] == '_') {
                o = 164;
            } else if(temp[i] < 32) {
                o = 32;
            } else {
                o = toupper(in[i]);
            }
            out[i+3] = o;
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
        FRESULT res = path->get_directory(*dirlist);
        for(int i=0;i<dirlist->get_elements();i++) {
            FileInfo *inf = (*dirlist)[i];
            iecNames->append(CreateIecName(inf->lfname, inf->extension, inf->attrib & AM_DIR));
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
        if (!command.names[0].name) {
            return -1;
        }
        int f = 0;
        for(int i=0;i<5;i++) {
            if (!command.names[i].name) {
                break;
            }
            char *name = command.names[i].name;
            int idx = -1;
            for (int fl=0; fl < iecNames->get_elements(); fl++) {
                char *iecName = (*iecNames)[fl];
                if (pattern_match(name, &iecName[3], false)) {
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
/*
    int GetCurrentPartition(void) {
        return currentPartition;
    }
*/
};


class IecChannel
{
    IecInterface *interface;
    FileManager *fm;

	int  channel;
    int  write;

    int  size;
    int  append;
    uint8_t buffer[256];

    int  pointer;
    int  prefetch;
    int  prefetch_max;
    File *f;
    int  last_command;
    int  dir_index;
    int  dir_last;
    IecPartition *dirPartition;
    t_channel_state state;
    mstring dirpattern;

    int  last_byte;

    // temporaries
    uint32_t bytes;

public:
     uint8_t buffer[256];
    IecChannel(IecInterface *intf, int ch);
    virtual ~IecChannel();
    virtual void reset_prefetch(void);
    virtual int prefetch_data(uint8_t& data);
    virtual int pop_data(void);
    int read_dir_entry(void);
    int read_block(void);
    virtual int push_data(uint8_t b);
    virtual int push_command(uint8_t b);
    void parse_command(char *buffer, command_t *command);
    void dump_command(command_t& cmd);
    bool hasIllegalChars(const char *name);
    const char *GetExtension(char specifiedType, bool &explicitExt);
private:
    int open_file(void);  // name should be in buffer
    int close_file(void); // file should be open
    friend class IecCommandChannel;
};

class IecCommandChannel : public IecChannel
{
//    BYTE error_buf[40];
    int track_counter;
    void mem_read(void);
    void mem_write(void);
    void renam(command_t& command);
    void copy(command_t& command);
public:
    IecCommandChannel(IecInterface *intf, int ch);
    virtual ~IecCommandChannel();
    void get_last_error(int err = -1, int track = 0, int sector = 0);
    int pop_data(void);
    int push_data(uint8_t b);
    void exec_command(command_t &command);
    int push_command(uint8_t b);
};

#endif /* IEC_CHANNEL_H */
