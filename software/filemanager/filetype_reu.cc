#include "filetype_reu.h"
#include "filemanager.h"
#include "c64.h"
#include "audio_select.h"
#include "menu.h"
#include "userinterface.h"

// tester instance
FileTypeREU tester_reu(file_type_factory);

// cart definition
extern BYTE _binary_module_bin_start;
cart_def mod_cart  = { 0x00, (void *)0, 0x4000, 0x02 | CART_REU | CART_RAM };

/*********************************************************************/
/* REU File Browser Handling                                         */
/*********************************************************************/
#define REUFILE_LOAD      0x5201
#define REUFILE_PLAYMOD   0x5202

#define REU_TYPE_REU 0
#define REU_TYPE_MOD 1

FileTypeREU :: FileTypeREU(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    info = NULL;
    type = 0;
}

FileTypeREU :: FileTypeREU(CachedTreeNode *par, FileInfo *fi, int type) : FileDirEntry(par, fi)
{
    printf("Creating REU type from info: %s\n", fi->lfname);
    this->type = type;
}

FileTypeREU :: ~FileTypeREU()
{
}

int FileTypeREU :: fetch_children(void)
{
  return -1;
}

int FileTypeREU :: fetch_context_items(IndexedList<CachedTreeNode *> &list)
{
    int count = 1;
    list.append(new MenuItem(this, "Load into REU", REUFILE_LOAD));
    DWORD capabilities = getFpgaCapabilities();
    if ((type == REU_TYPE_MOD) && (capabilities & CAPAB_SAMPLER)) {
        list.append(new MenuItem(this, "Play MOD", REUFILE_PLAYMOD));
        count++;
    }
    return count + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeREU :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "REU")==0)
        return new FileTypeREU(obj->parent, inf, REU_TYPE_REU);
    if(strcmp(inf->extension, "MOD")==0)
        return new FileTypeREU(obj->parent, inf, REU_TYPE_MOD);
    return NULL;
}

void FileTypeREU :: execute(int selection)
{
	printf("REU Select: %4x\n", selection);
	File *file;
	UINT bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    int remain;
    
	static char buffer[36];
    BYTE *dest;
    
	switch(selection) {
    case REUFILE_PLAYMOD:
        audio_configurator.clear_sampler_registers();
        // fallthrough
	case REUFILE_LOAD:
//		if(info->size > 0x100000)
//			res = user_interface->popup("This might take a while. Continue?", BUTTON_OK | BUTTON_CANCEL );
        sectors = (info->size >> 9);
        if(sectors >= 128)
            progress = true;
        secs_per_step = (sectors + 31) >> 5;
        bytes_per_step = secs_per_step << 9;
        remain = info->size;
        
		printf("REU Load.. %s\n", get_name());
		file = root.fopen(this, FA_READ);
		if(file) {
            total_bytes_read = 0;
			// load file in REU memory
            if(progress) {
                user_interface->show_status("Loading REU file..", 32);
                dest = (BYTE *)(REU_MEMORY_BASE);
                while(remain >= 0) {
        			file->read(dest, bytes_per_step, &bytes_read);
                    total_bytes_read += bytes_read;
                    user_interface->update_status(NULL, 1);
        			remain -= bytes_per_step;
        			dest += bytes_per_step;
        	    }
        	    user_interface->hide_status();
        	} else {
    			file->read((void *)(REU_MEMORY_BASE), (REU_MAX_SIZE), &bytes_read);
                total_bytes_read += bytes_read;
    	    }
			root.fclose(file);
			file = NULL;
            if (selection == REUFILE_LOAD) {
    			sprintf(buffer, "Bytes loaded: %d ($%8x)", total_bytes_read, total_bytes_read);
    			user_interface->popup(buffer, BUTTON_OK);
            } else {
                mod_cart.custom_addr = (void *)&_binary_module_bin_start;
                push_event(e_unfreeze, (void *)&mod_cart, 1);
                push_event(e_object_private_cmd, c64, C64_EVENT_MAX_REU);
                push_event(e_object_private_cmd, c64, C64_EVENT_AUDIO_ON);
                AUDIO_SELECT_LEFT   = 6;
                AUDIO_SELECT_RIGHT  = 7;
            }
		} else {
			printf("Error opening file.\n");
		}
        break;
	default:
		FileDirEntry :: execute(selection);
    }
}
