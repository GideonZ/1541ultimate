#ifndef FILETYPE_CRT_H
#define FILETYPE_CRT_H

#include "file_direntry.h"

class FileTypeCRT : public FileDirEntry
{
    BYTE  crt_header[0x20]; 
    BYTE  chip_header[0x10];
    WORD  type_select;
    WORD  max_bank;
    DWORD total_read;
    bool  load_at_a000;    
    char *name;
    bool  check_header(File *f);
    void  configure_cart(void);
    bool  read_chip_packet(File *file);

public:
    FileTypeCRT(FileTypeFactory &fac);
    FileTypeCRT(PathObject *par, FileInfo *fi);
    ~FileTypeCRT();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};


#endif
