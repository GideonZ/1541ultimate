/*
 * fileio.cc
 *
 *  Created on: Apr 27, 2015
 *      Author: Gideon
 */

#include "filemanager.h"
#include "dump_hex.h"

void show_first_file(Path *path)
{
	FileManager *fm = FileManager :: getFileManager();
	File *f = fm->fopen(path, "*", FA_READ);

	if (f) {
		printf("File successfully opened. Path of file = '%s'.\nFilename = '%s'\n", f->get_path(), f->getFileInfo()->lfname);
		uint32_t t;
		char buf[256];
		//f->print_info();
		f->read(buf, 256, &t);
		dump_hex_relative(buf, t);
		fm->fclose(f);
	} else {
		printf("File not found.\n");
	}

}

void test_emb(Path *path, char *filename)
{
	IndexedList<FileInfo *>dir_list(0, NULL);
	path->cd(filename);

	FRESULT res = path->get_directory(dir_list);
	if (res == FR_OK) {
		printf("Directory of %s:\n", path->get_path());
		for(int i=0;i<dir_list.get_elements();i++) {
			printf("%9d: %s\n", dir_list[i]->size, dir_list[i]->lfname);
		}
	} else {
		printf("Result: %s\n", FileSystem :: get_error_string(res));
	}
	show_first_file(path);
	path->cd("..");
}

extern "C" void test_from_cpp() {

	IndexedList<FileInfo *>dir_list(0, NULL);

	FileManager *fm = FileManager :: getFileManager();
	Path *path = fm->get_new_path("test");

	path->cd("SD");

	FRESULT res = path->get_directory(dir_list);

	if (res == FR_OK) {
		for(int i=0;i<dir_list.get_elements();i++) {
			printf("%9d: %s\n", dir_list[i]->size, dir_list[i]->lfname);

			if(strcmp(dir_list[i]->extension, "D64")==0) {
				test_emb(path, dir_list[i]->lfname);
			}
		}
	} else {
		printf("Result: %s\n", FileSystem :: get_error_string(res));
	}
	fm->dump();

	printf("Now doing it a second time..\n");
	for(int i=0;i<dir_list.get_elements();i++) {
		if(strcmp(dir_list[i]->extension, "D64")==0) {
			test_emb(path, dir_list[i]->lfname);
		}
	}

	fm->dump();
}
