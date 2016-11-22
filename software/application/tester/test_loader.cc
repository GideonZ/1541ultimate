/*
 * test_loader.cc
 *
 *  Created on: Nov 20, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "altera_avalon_pio_regs.h"

#include "FreeRTOS.h"
#include "task.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "u2p.h"
#include "usb_base.h"
#include "filemanager.h"

typedef struct {
	const char *fileName;
	const char *romName;
	const uint32_t flashAddress;
	uint32_t *buffer;
	uint32_t  size;
} BinaryImage_t;

BinaryImage_t testerApplication = { "/Usb?/tester/tester.app", "Tester Application", 0, 0, 0 };

void jump_run(uint32_t a)
{
	void (*function)();
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

int load_file(BinaryImage_t *flashFile)
{
	FRESULT fres;
	File *file;
	uint32_t transferred;

	FileManager *fm = FileManager :: getFileManager();
	fres = fm->fopen(flashFile->fileName, FA_READ, &file);
	if (fres == FR_OK) {
		flashFile->size = file->get_size();
		flashFile->buffer = (uint32_t *)malloc(flashFile->size + 8);
		fres = file->read(flashFile->buffer, flashFile->size, &transferred);
		if (transferred != flashFile->size) {
			printf("Expected to read %d bytes, but got %d bytes.\n", flashFile->size, transferred);
			return -1;
		}
	} else {
		printf("Warning: Could not open file '%s'! %s\n", flashFile->fileName, FileSystem :: get_error_string(fres));
		return -2;
	}
	printf("Successfully read %-35s. Size = %8d. Stored at %p.\n", flashFile->fileName, flashFile->size, flashFile->buffer);
	return 0;
}

extern "C" {
	void main_task(void *context)
	{
		// Power off DUT/Slot
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, 0xFF);
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, 0x0F); // turn on all LEDs, indicating loader state

		printf("Ultimate-II+ automated test system - LOADER...\n");

		usb2.initHardware();
		FileManager *fm = FileManager :: getFileManager();

		printf("Waiting for USB storage device to become available.\n");
		FileInfo info(32);
		FRESULT res;
		do {
			vTaskDelay(100);
			res = fm->fstat("/Usb?", info);
			//printf("%s\n", FileSystem :: get_error_string(res));
		} while (res != FR_OK);

		load_file(&testerApplication);

		if (!testerApplication.buffer) {
			printf("Tester application was not successfully loaded. Cannot start.\n");
		} else {
			uint32_t *app = testerApplication.buffer;
			int remain = testerApplication.size;
			portDISABLE_INTERRUPTS();
			ioWrite8(ITU_IRQ_GLOBAL, 0);
			ioWrite8(ITU_IRQ_DISABLE, 0xFF);
			printf("Interrupts are now disabled.\n");

			uint32_t runaddr = 0;
//			while(remain > 0) {
				int length = (int)app[1];
				uint32_t *dest = (uint32_t *)app[0];
				if (!runaddr)
					runaddr = app[2];
				uint32_t *src = &app[3];
				app += 3;
				app += (length >> 2);
				remain -= length;
				remain -= 12;
				printf("Copying %d bytes from %p to %p (remain = %d)\n", length, src, dest, remain);
				memcpy(dest, src, length);
//			}
			printf("Jumping to %p\n", runaddr);
			jump_run(runaddr);
		}
		printf("EXIT.\n");
		while(1)
			;
	}
}
