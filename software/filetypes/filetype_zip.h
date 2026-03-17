#ifndef FILETYPE_ZIP_H
#define FILETYPE_ZIP_H

#include "filetypes.h"
#include "browsable_root.h"
#include "mystring.h"

class BrowsableZipEntry : public Browsable
{
    mstring zip_dir;     // directory containing the ZIP (with trailing slash as from get_path())
    mstring zip_name;    // the ZIP filename
    int     entry_idx;
    mstring entry_path;
    uint32_t uncomp_size;
    bool    is_dir;
public:
    BrowsableZipEntry(const char *zip_dir, const char *zip_name, int idx,
                      const char *path, uint32_t size, bool dir);
    virtual ~BrowsableZipEntry() {}

    virtual const char *getName() { return entry_path.c_str(); }
    virtual void getDisplayString(char *buffer, int width);
    virtual bool isSelectable() { return !is_dir; }
    virtual void fetch_context_items(IndexedList<Action *> &items);

    static SubsysResultCode_e extract_one(SubsysCommand *cmd);
};

class FileTypeZIP : public FileType
{
    BrowsableDirEntry *node;

    static SubsysResultCode_e do_unzip_to_folder(SubsysCommand *cmd);
    static SubsysResultCode_e do_unzip_here(SubsysCommand *cmd);
    static SubsysResultCode_e do_extract(SubsysCommand *cmd, bool to_folder);

public:
    FileTypeZIP(BrowsableDirEntry *node);
    ~FileTypeZIP() {}

    int  fetch_context_items(IndexedList<Action *> &list) override;
    int  getCustomBrowsables(Browsable *parent, IndexedList<Browsable *> &list) override;
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
