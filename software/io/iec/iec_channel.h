#ifndef IEC_CHANNEL_H
#define IEC_CHANNEL_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "iec_drive.h"
#include "filemanager.h"
#include "mystring.h"
#include "cbmdos_parser.h"

typedef enum _t_channel_state {
    e_idle, e_filename, e_file, e_dir, e_partlist, e_record, e_buffer, e_complete, e_error, e_status
    
} t_channel_state;

static uint8_t c_header[32] = { 1,  1,  4,  1,  0,  0, 18, 34,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            32, 32, 32, 32, 32, 32, 32, 32,
                            34, 32, 48, 48, 32, 50, 65,  0 };

class IecCommandChannel;

#define MAX_PARTITIONS 256

class IecFileSystem;

class IecPartition {
    Path *path;
    FileManager *fm;
    IecFileSystem *vfs;
    int partitionNumber;
    mstring root;
    mstring full_path;
public:
    IecPartition(IecFileSystem *vfs, int nr, const char *rt)
    {
        fm = FileManager::getFileManager();
        this->vfs = vfs;
        partitionNumber = nr;
        path = fm->get_new_path("IEC Partition");
        root = rt;
        full_path = rt;
        if(root[-1] != '/') {
            root += "/";
            full_path += "/";
        }
    }

    ~IecPartition()
    {
        fm->release_path(path);
    }

    int GetPartitionNumber(void)
    {
        return partitionNumber;
    }

    bool IsValid(); // implemented in iec_channel.cc, to resolve an illegal forward reference

    bool cd(const char *p)
    {
        // zero pointers or zero strings don't need to be considered
        if (!p || !*p) {
            return true;
        }
        // make a new temporary path in string form
        Path temp(path);
        temp.cd(p);
        const char *rp = temp.get_path();        
        mstring full = root;
        full += (rp+1); // strip off the leading slash

        //printf("Now we need to check if the resulting path '%s' is valid\n", full.c_str());
        // now test the result, if correct copy
        if (fm->is_path_valid(full.c_str())) {
            path->cd(p);
            full_path = root + (rp+1); // strip off the leading slash
            return true;
        } 
        return false;
    }

    static char *CreateIecName(FileInfo *inf, char *out, filetype_t& type)
    {
        bool dir = inf->attrib & AM_DIR;
        char *ext = inf->extension;
        memset(out, 0, 24);

        bool cutExtension = false;
        if (dir) {
            type = e_folder;
        } else if (strcmp(ext, "PRG") == 0) {
            type = e_prg;
            cutExtension = true;
        } else if (strcmp(ext, "SEQ") == 0) {
            type = e_seq;
            cutExtension = true;
        } else if (strcmp(ext, "REL") == 0) {
            type = e_rel;
            cutExtension = true;
        } else if (strcmp(ext, "USR") == 0) {
            type = e_usr;
            cutExtension = true;
        } else {
            type = e_any;
        }
        if (inf->name_format & NAME_FORMAT_CBM) {
            strncpy(out, inf->lfname, 16);
        } else {
            fat_to_petscii(inf->lfname, cutExtension, out, 16, true);
        }
        return out;
    }

    const char *GetRelativePath()
    {
        return path->get_path();
    }

    const char *GetFullPath()
    {
        return full_path.c_str();
    }

    const char *GetRootPath()
    {
        return root.c_str();
    }

};

class IecFileSystem {
    FileManager *fm;
    int currentPartition;
    IecPartition *partitions[MAX_PARTITIONS];
    IecDrive *drive;
public:
    IecFileSystem(IecDrive *dr)
    {
        drive = dr;
        fm = FileManager::getFileManager();
        for (int i = 0; i < MAX_PARTITIONS; i++) {
            partitions[i] = 0;
        }
        currentPartition = 0;
        partitions[0] = new IecPartition(this, 0, "/"); // drive->get_root_path());
    }

    ~IecFileSystem() 
    {
        for (int i = 0; i < MAX_PARTITIONS; i++) {
            if (partitions[i]) {
                delete partitions[i];
            }
        }
    }

    void add_partition(int p, const char *path)
    {
        if (partitions[p]) {
            delete partitions[p];
        }
        partitions[p] = new IecPartition(this, p, path);
    }

    const char *GetRootPath()
    {
        return drive->get_root_path();
    }

    const char *GetPartitionPath(int index, bool root)
    {
        if (partitions[index]) {
            if (root) {
                return partitions[index]->GetRootPath();
            } else {
                return partitions[index]->GetFullPath();
            }
        }
        return NULL;
    }

    int GetTargetPartitionNumber(int index)
    {
        if (index < 0) {
            index = currentPartition;
        }
        return index;
    }

    IecPartition *GetPartition(int index)
    {
        index = GetTargetPartitionNumber(index);
        return partitions[index];
    }

    void SetCurrentPartition(int pn)
    {
        currentPartition = pn;
    }

    bool construct_path(open_t &fn, char *pathbuf, int path_size, char *namebuf, int name_size)
    {
        // replace current partition
        if (fn.file.partition < 1) {
            fn.file.partition = currentPartition;
        }

        // check for double slash path
        bool root = ((fn.file.path[0] == '/') && (fn.file.path[1] == '/'));

        const char *pp = GetPartitionPath(fn.file.partition, root);
        if (!pp) {
            return false;
        }
        // 
        strncpy(pathbuf, pp, path_size);

        // now, lets add the parts of the path and the filename
        char temp[32];            
        const char *parts[8] = { NULL };
        int n = fn.file.path.split('/', parts, 8);
        for (int i=(root)?2:1; i<n; i++) {
            petscii_to_fat(parts[i], temp, 32);
            strncat(pathbuf, temp, path_size);
            strncat(pathbuf, "/", path_size);
        }
        petscii_to_fat(fn.file.filename.c_str(), namebuf, name_size);
        return true;
    }
};

typedef struct {
    uint8_t bufdata[512];
    uint32_t valid_bytes;
} bufblk_t;

class IecChannel {
    IecDrive *drive;
    FileManager *fm;

    int channel;
    bufblk_t bufblk[2];
    bool pingpong;
    bufblk_t *curblk;
    bufblk_t *nxtblk;
    uint8_t *buffer; // for legacy (to be refactored)

    t_channel_state state;
    int pointer;
    int prefetch;
    int prefetch_max;
    int last_byte;

    open_t name_to_open;
    File *f;
    Directory *dir;
    fileaccess_t filemode;
    uint32_t dir_free;
    int part_idx;

    uint32_t recordOffset;
    uint8_t recordSize;
    bool recordDirty;

    // temporaries
    uint8_t flags;
    IecPartition *partition;
    char fs_filename[64];

private:
    int setup_partition_read();
    int setup_directory_read();
    int setup_file_access();
    int setup_buffer_access(void);
    int init_iec_transfer(void);

    int open_file(void);  // name should be in buffer
    int close_file(void); // file should be open
    int read_dir_entry(void);
    void swap_buffers(void);
    int read_block(void);
    t_channel_retval read_record(int offset);
    t_channel_retval write_record(void);

    const char *ConstructPath(mstring& work, filename_t& name, filetype_t ftype, fileaccess_t acc);

public:
    IecChannel(IecDrive *dr, int ch);
    virtual ~IecChannel();
    virtual void reset(void);
    virtual void talk(void)
    {
    }

    void reset_prefetch(void);

    t_channel_retval prefetch_data(uint8_t& data);
    t_channel_retval prefetch_more(int, uint8_t*&, int &);
    virtual t_channel_retval pop_data(void);
    virtual t_channel_retval pop_more(int);
    virtual t_channel_retval push_data(uint8_t b);
    virtual t_channel_retval push_command(uint8_t b);

    virtual int ext_open_file(const char *name);
    virtual int ext_close_file(void);

    void seek_record(const uint8_t *);
    friend class IecCommandChannel;
};

class IecCommandChannel: public IecChannel, public IecCommandExecuter {
    IecParser *parser;
    uint8_t wr_buffer[64];
    int wr_pointer;

    void mem_read(void);
    void mem_write(void);
    void get_error_string(void);

    int do_block_read(int chan, int part, int track, int sector);
    int do_block_write(int chan, int part, int track, int sector);
    int do_buffer_position(int chan, int pos);
    int do_set_current_partition(int part);
    int do_change_dir(filename_t& dest);
    int do_make_dir(filename_t& dest);
    int do_remove_dir(filename_t& dest);
    int do_copy(filename_t& dest, filename_t sources[], int n);
    int do_initialize();
    int do_format(uint8_t *name, uint8_t id1, uint8_t id2);
    int do_rename(filename_t &src, filename_t &dest);
    int do_scratch(filename_t filenames[], int n);
    int do_cmd_response(uint8_t *data, int len);
    int do_set_position(int chan, uint32_t pos);

public:
    IecCommandChannel(IecDrive *dr, int ch);
    virtual ~IecCommandChannel();
    void set_error(int err = -1, int track = 0, int sector = 0);
    void reset(void);
    void talk(void);
    t_channel_retval pop_data(void);
    t_channel_retval pop_more(int);
    t_channel_retval push_data(uint8_t b);
    t_channel_retval push_command(uint8_t b);
    int ext_open_file(const char *name);
    int ext_close_file(void);
};

#endif /* IEC_CHANNEL_H */
