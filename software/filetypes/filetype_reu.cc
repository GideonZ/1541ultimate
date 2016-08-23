#include "filetype_reu.h"
#include "filemanager.h"
#include "c64.h"
#include "audio_select.h"
#include "menu.h"
#include "userinterface.h"
#include "subsys.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_reu(FileType :: getFileTypeFactory(), FileTypeREU :: test_type);

// cart definition
extern uint8_t _module_bin_start;
cart_def mod_cart  = { 0x00, (void *)0, 0x4000, 0x02 | CART_REU | CART_RAM };

/*********************************************************************/
/* REU File Browser Handling                                         */
/*********************************************************************/
#define REUFILE_LOAD      0x5201
#define REUFILE_PLAYMOD   0x5202

#define REU_TYPE_REU 0
#define REU_TYPE_MOD 1

FileTypeREU :: FileTypeREU(BrowsableDirEntry *node, int type)
{
    printf("Creating REU type from: %s\n", node->getName());
    this->node = node;
    this->type = type;
}

FileTypeREU :: ~FileTypeREU()
{
}

int FileTypeREU :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 1;

    list.append(new Action("Load into REU", FileTypeREU :: execute_st, REUFILE_LOAD));
    uint32_t capabilities = getFpgaCapabilities();
    if ((type == REU_TYPE_MOD) && (capabilities & CAPAB_SAMPLER)) {
        list.append(new Action("Play MOD", FileTypeREU :: execute_st, REUFILE_PLAYMOD));
        count++;
    }
    return count;
}

FileType *FileTypeREU :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "REU")==0)
        return new FileTypeREU(obj, REU_TYPE_REU);
    if(strcmp(inf->extension, "MOD")==0)
        return new FileTypeREU(obj, REU_TYPE_MOD);
    return NULL;
}

int FileTypeREU :: execute_st(SubsysCommand *cmd)
{
	printf("REU Select: %4x\n", cmd->functionID);
	File *file = 0;
	uint32_t bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    int remain;
    
	static char buffer[48];
    uint8_t *dest;
    FileManager *fm = FileManager :: getFileManager();
    FileInfo info(32);
    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), info);

    if (cmd->functionID == REUFILE_PLAYMOD) {
        audio_configurator.clear_sampler_registers();
    }

    sectors = (info.size >> 9);
	if(sectors >= 128)
		progress = true;
	secs_per_step = (sectors + 31) >> 5;
	bytes_per_step = secs_per_step << 9;
	remain = info.size;

	printf("REU Load.. %s\nUI = %p", cmd->filename.c_str(), cmd->user_interface);
	if (!cmd->user_interface)
		progress = false;

	FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
	if(file) {
		total_bytes_read = 0;
		// load file in REU memory
		if(progress) {
			cmd->user_interface->show_progress("Loading REU file..", 32);
			dest = (uint8_t *)(REU_MEMORY_BASE);
			while(remain > 0) {
				file->read(dest, bytes_per_step, &bytes_read);
				total_bytes_read += bytes_read;
				cmd->user_interface->update_progress(NULL, 1);
				remain -= bytes_per_step;
				dest += bytes_per_step;
			}
			cmd->user_interface->hide_progress();
		} else {
			file->read((void *)(REU_MEMORY_BASE), (REU_MAX_SIZE), &bytes_read);
			total_bytes_read += bytes_read;
		}
		printf("\nClosing file. ");
		fm->fclose(file);
		file = NULL;
		printf("done.\n");

		if (cmd->functionID == REUFILE_LOAD) {
			sprintf(buffer, "Bytes loaded: %d ($%8x)", total_bytes_read, total_bytes_read);
			cmd->user_interface->popup(buffer, BUTTON_OK);
		} else {
			mod_cart.custom_addr = (void *)&_module_bin_start;

			C64_REU_SIZE = 7;
			C64_REU_ENABLE = 1;
			C64_SAMPLER_ENABLE = 1;
			ioWrite8(AUDIO_SELECT_LEFT, 6);
			ioWrite8(AUDIO_SELECT_RIGHT, 7);

			SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&mod_cart, "", "");
			c64_command->execute();
		}
	} else {
		printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
		return -2;
	}
	return 0;
}
