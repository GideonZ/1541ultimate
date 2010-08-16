#include "filetype_prg.h"
#include "directory.h"
#include "filemanager.h"
#include "c1541.h"
#include "c64.h"
#include <ctype.h>

/* Drives to mount on */
extern C1541 *c1541;

/* other external references */
extern BYTE _binary_sidcrt_65_start;

// tester instance
FileTypePRG tester_prg(file_type_factory);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
#define PRGFILE_MOUNT_RUN 0x2203

cart_def dma_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM }; 

FileTypePRG :: FileTypePRG(FileTypeFactory &fac) : FileDirEntry(NULL, NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypePRG :: FileTypePRG(PathObject *par, FileInfo *fi, bool hdr) : FileDirEntry(par, fi)
{
    printf("Creating PRG type from info: %s\n", fi->lfname);
    has_header = hdr;
}

FileTypePRG :: ~FileTypePRG()
{
}

int FileTypePRG :: fetch_children(void)
{
  return -1;
}

int FileTypePRG :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Run",  PRGFILE_RUN));
    list.append(new MenuItem(this, "Load", PRGFILE_LOAD));
	int count = 2;

	// a little dirty
	FileInfo *inf = parent->get_file_info();
    if(strcmp(inf->extension, "D64")==0) {
    	list.append(new MenuItem(this, "Mount & Run", PRGFILE_MOUNT_RUN));
    	count++;
    }

    return count + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypePRG :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "PRG")==0)
        return new FileTypePRG(obj->parent, inf, false);
    if(inf->extension[0] == 'P') {
        if(isdigit(inf->extension[1]) && isdigit(inf->extension[2])) {
            return new FileTypePRG(obj->parent, inf, true);
        }
    }
    return NULL;
}

bool FileTypePRG :: check_header(File *f)
{
    static char p00_header[0x1A]; 

    if(!has_header)
        return true;
    UINT bytes_read;
    FRESULT res = f->read(p00_header, 0x1A, &bytes_read);
    if(res != FR_OK)
        return false;
    if(strncmp(p00_header, "C64File", 7))
        return false;
    return true;        
}

void FileTypePRG :: execute(int selection)
{
	printf("PRG Select: %4x\n", selection);
	File *file, *d64;
	FileInfo *inf;
	
	switch(selection) {
	case PRGFILE_RUN:
	case PRGFILE_LOAD:
	case PRGFILE_MOUNT_RUN:
		printf("DMA Load.. %s\n", get_name());
		file = root.fopen(this, FA_READ);
		if(file) {
            if(check_header(file)) {
    			C64_POKE(2, 0xAB); // magic key to enable DMA handshake
                dma_cart.custom_addr = (void *)&_binary_sidcrt_65_start;
    			push_event(e_unfreeze, (void *)&dma_cart, 1);
    			if(selection == PRGFILE_MOUNT_RUN) {
					inf = parent->get_file_info();
					d64 = root.fopen(parent, FA_READ);
    				if(d64) // mount read only only if file is read only
    					push_event(e_mount_drv1, d64, ((inf->attrib & AM_RDO) != 0));
    				else
    					printf("Can't open D64 file..\n");
    			}
    			push_event(e_dma_load, file, (selection == PRGFILE_LOAD)?0:1);
            } else {
                printf("Header of P00 file not correct.\n");
                root.fclose(file);
            }
		} else {
			printf("Error opening file.\n");
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}
