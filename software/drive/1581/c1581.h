#ifndef C1581_H_
#define C1581_H_

#include <stdint.h>
#include <stdio.h>
#include "integer.h"
#include "filemanager.h"
#include "file_system.h"
#include "disk_image.h"
#include "config.h"
#include "subsys.h"
#include "menu.h" // to add menu items
#include "flash.h"
#include "iomap.h"
#include "browsable_root.h"
#include "iec.h"
#include "iec_channel.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "c1581_channel.h"

#define DISK_SIZE			819200
#define BLOCK_SIZE			256
#define D81FILE_MOUNT      	0x2102

#define MENU_1581_RESET     0x1581
#define MENU_1581_REMOVE    0x1582
#define MENU_1581_SAVED81   0x1583
#define MENU_1581_BLANK     0x1584

typedef enum {
	e_d81_disabled,
	e_no_disk81,
    e_disk81_file_closed,
    e_d81_disk
} t_disk_state_81;

typedef enum {
	file_prg,
	file_seq,
	file_usr,
	file_del,
	file_cbm
} t_file_type;

struct BamAllocationType {
	uint8_t track;
	uint8_t sector;
	bool allocated;
};

typedef BamAllocationType BamAllocation;

struct DirectoryEntryType {
	uint8_t file_type;
	uint8_t first_data_track;
	uint8_t first_data_sector;
	uint8_t filename[16];
	uint8_t first_track_ssb;	// side sector block (REL file only)
	uint8_t first_sector_ssb;
	uint8_t rel_file_length;
	uint8_t unused[6];
	uint8_t size_lo;
	uint8_t size_hi;
};

typedef DirectoryEntryType DirectoryEntry;

class C1581: public SubSystem, ConfigurableObject, ObjectWithMenu
{
		uint8_t mount_file[DISK_SIZE];
		bool bamisdirty;
		uint8_t BAMCache1[256];
		uint8_t BAMCache2[256];
		char mounted_path[256];
		char mounted_filename[256];

    public:
		C1581_Channel *channels[16];
		FileManager *fm;
		//volatile uint8_t *registers;
		int iec_address;
		char drive_letter;
		mstring drive_name;
		int last_command;
		t_disk_state_81 disk_state;
		uint8_t curtrack;
		uint8_t cursector;
		uint8_t nxttrack;
		uint8_t nxtsector;
		uint8_t prevtrack;
		uint8_t prevsector;
		uint8_t *sectorBuffer;
		uint8_t curbamtrack;
		uint8_t curbamsector;
		int curDirTotalBlocks;
		uint8_t startingDirTrack;
		uint8_t startingDirSector;
		int last_error;
		uint8_t buffers[7][256];
		
		C1581(char letter);
        ~C1581();

        void drive_reset(uint8_t doit);
        int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
		int  executeCommand(SubsysCommand *cmd);
		void init();
        void unlink(void);
        int mount_d81(bool protect, File *);
        void mount_blank();
        void remove_disk(void);
        void save_disk_to_file(SubsysCommand *cmd);
        
		uint8_t goTrackSector(uint8_t track, uint8_t sector);
		void writeSector(void);
		void write_d81(void);
		int readBAMtocache(void);
		int writeBAMfromcache(void);
		int resetBAM(uint8_t* id);
		int findFreeSector(bool file,uint8_t *track, uint8_t *sector);
		
		int readFile(uint8_t* filename, uint8_t* buffer, int *size);
		int getFileTrackSector(char *filename, uint8_t *track, uint8_t *sector, bool deleted);
		bool getTrackSectorAllocation(uint8_t track, uint8_t sector);
		int setTrackSectorAllocation(uint8_t track, uint8_t sector, bool allocate);
		int getNextFileSector(uint8_t* filename);

		int prefetch_data(uint8_t channel, uint8_t& data);

		int get_directory(uint8_t *buffer);
		int getNextDirectoryEntry(bool *firstcall, int *dirctr, bool *lastdirsector, DirectoryEntry *dirEntry);
		int createDirectoryEntry(DirectoryEntry *newDirEntry);
		int findDirectoryEntry(char *filename, char* extension, DirectoryEntry *dirEntry);
		int updateDirectoryEntry(char *filename, char* extension, DirectoryEntry *dirEntry);
		int getBlocksFree(void);
		int updateFileInfo(char *filename, uint16_t blocks);

		int get_last_error(char *buffer, int track, int sector);
};

extern C1581 *c1581_C;

#endif
