#ifndef FILETYPE_CRT_H
#define FILETYPE_CRT_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeCRT : public FileType
{
	BrowsableDirEntry *node;
	uint8_t  crt_header[0x20];
    uint8_t  chip_header[0x10];
    uint16_t  type_select;
    uint16_t  max_bank;
    uint32_t total_read;
    bool  load_at_a000;    
    char *name;
    bool  check_header(File *f);
    void  configure_cart(void);
    bool  read_chip_packet(File *file);

    static int execute_st(SubsysCommand *cmd);
    int execute(SubsysCommand *cmd);
public:
    FileTypeCRT(BrowsableDirEntry *node);
    ~FileTypeCRT();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};


#endif
