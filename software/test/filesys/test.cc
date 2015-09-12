/*
 * test.cc
 *
 *  Created on: Sep 1, 2015
 *      Author: Gideon
 */

#include <stdio.h>
#include "filemanager.h"
#include "blockdev_emul.h"
#include "file_device.h"
#include "dump_hex.h"
#include "node_directfs.h"
#include "filesystem_dummy.h"

int main()
{
	FileManager *fm = FileManager :: getFileManager();
	BlockDevice *blk = new BlockDevice_Emulated("image/black_stick.img", 512);
	FileDevice *dev = new FileDevice(blk, (char *)"img", (char *)"Image");
	dev->attach_disk(512);
	FileSystem *dummy_fs = new FileSystemDummy();
	Node_DirectFS *dummy = new Node_DirectFS(dummy_fs, "dummy");
	fm->add_root_entry(dev);
	fm->add_root_entry(dummy);


	fm->print_directory("/");
	fm->print_directory("/dummy");
	fm->print_directory("/dummy/gideon@1541ultimate.net:password/aapje.txt");

	fm->print_directory("/img");
	fm->print_directory("/img/iso");
	fm->print_directory("/img/iso/d*");
	fm->print_directory("/img/canon.fat/REU");
	fm->print_directory("/img/testdir");

	fm->print_directory("/img/Stuff");
	fm->print_directory("/img/Stuff/FLOWERPO.D64");
	fm->print_directory("/img/Stuff/FLOWERPO.D64/Aap");
	fm->print_directory("/img/Stuff/FLOWERPO.D64/*");
	fm->print_directory("/img/Stuff/bestaat_niet");


	File *f;
	uint8_t *buffer = new uint8_t[192 * 1024];
	uint32_t trans = 0;

	//	fm->fopen("/img/Stuff/FLOWERPO.D64/", "FLOWERPOWER", FA_READ, &f);
	// FRESULT fres = fm->fopen("/img/canon.fat/REU/BETACOPY.D64", "*", FA_READ, &f);
	FRESULT fres = fm->fopen("/img/iso/d*/IND*", FA_READ, &f);
//	FRESULT fres = fm->fopen("/img/testdir", "test.9?4", FA_READ, &f);
	if (fres == FR_OK) {
		fres = f->read(buffer, 192 * 1024, &trans);
		puts(FileSystem :: get_error_string(fres));
		printf("%06x\n", trans);
		dump_hex_relative(buffer, 128);
		FILE *fo = fopen("test2.dmp", "wb");
		fwrite(buffer, 1, trans, fo);
		fclose(fo);
		fm->fclose(f);
	} else {
		printf("Could not open file.\n");
	}

	const char *testdir = "/img/canon.fat/REU";

	Path *p = fm->get_new_path("test");
	p->cd(testdir);
	fres = fm->create_dir(testdir);
	printf("Creating %s: %s\n", testdir, FileSystem :: get_error_string(fres));

	char namebuf[10];
	for(int count=0; count < 10; count++) {
		sprintf(namebuf, "test.%03d", count);

		fres = fm->fopen(p, namebuf, FA_WRITE | FA_CREATE_NEW, &f);
		if (fres == FR_OK) {
			fres = f->write(buffer, trans, &trans);
			puts(FileSystem :: get_error_string(fres));
			printf("%06x\n", trans);
			fm->fclose(f);
		} else {
			printf("Could not open file for writing. ");
			puts(FileSystem :: get_error_string(fres));
		}
	}

	for(int count=0; count < 10; count+=1) {
		sprintf(namebuf, "test.%03d", count);
		fres = fm->delete_file(p, namebuf);
		printf("Deleting %s: %s\n", namebuf, FileSystem :: get_error_string(fres));
	}
	fres = fm->delete_file(testdir);
	printf("Deleting %s: %s\n", testdir, FileSystem :: get_error_string(fres));

	const char *testdir2 = "/img/fromcanon";

	fres = fm->create_dir(testdir2);
	printf("Creating %s: %s\n", testdir2, FileSystem :: get_error_string(fres));
	fm->fcopy("/img", "SIDs", testdir2);
	fres = fm->delete_file(testdir2);
	printf("Deleting %s: %s\n", testdir2, FileSystem :: get_error_string(fres));

	fm->remove_root_entry(dummy);
	fm->remove_root_entry(dev);
	delete buffer;
	delete dev;
	delete blk;
}
