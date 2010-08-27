#include "filetype_reu.h"
#include "filemanager.h"
#include "c64.h"
#include "menu.h"
#include "userinterface.h"

// tester instance
FileTypeREU tester_reu(file_type_factory);

/*********************************************************************/
/* REU File Browser Handling                                         */
/*********************************************************************/
#define REUFILE_LOAD      0x5201

FileTypeREU :: FileTypeREU(FileTypeFactory &fac) : FileDirEntry(NULL, NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypeREU :: FileTypeREU(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating REU type from info: %s\n", fi->lfname);
}

FileTypeREU :: ~FileTypeREU()
{
}

int FileTypeREU :: fetch_children(void)
{
  return -1;
}

int FileTypeREU :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Load into REU", REUFILE_LOAD));

    return 1 + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeREU :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "REU")==0)
        return new FileTypeREU(obj->parent, inf);
    return NULL;
}

void FileTypeREU :: execute(int selection)
{
	printf("REU Select: %4x\n", selection);
	File *file;
	UINT bytes_read;
	int res;
	static char buffer[36];

	switch(selection) {
	case REUFILE_LOAD:
		res = BUTTON_OK;
		if(info->size > 0x100000)
			res = user_interface->popup("This might take a while. Continue?", BUTTON_OK | BUTTON_CANCEL );
		if(res == BUTTON_OK) {
			printf("REU Load.. %s\n", get_name());
			file = root.fopen(this, FA_READ);
			if(file) {
				// load file in REU memory
				file->read((void *)(REU_MEMORY_BASE), (REU_MAX_SIZE), &bytes_read);
				sprintf(buffer, "Bytes loaded: %d ($%8x)", bytes_read, bytes_read);
				root.fclose(file);
				file = NULL;
				user_interface->popup(buffer, BUTTON_OK);
			} else {
				printf("Error opening file.\n");
			}
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}
