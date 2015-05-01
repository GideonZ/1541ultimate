/*
 * fileio.cc
 *
 *  Created on: Apr 27, 2015
 *      Author: Gideon
 */

#include "filemanager.h"

extern "C" void test_from_cpp() {

	IndexedList<FileInfo *>dir_list(0, NULL);

	Path *path = file_manager.get_new_path();
	printf("New path: '%s'\n", path->get_path());

	file_manager.dump();

	printf("CD to sd: %d, '%s'\n", path->cd("SD/DCIM"), path->get_path());
	path->get_directory(dir_list);
	for(int i=0;i<dir_list.get_elements();i++) {
		printf("%9d: %s\n", dir_list[i]->size, dir_list[i]->lfname);
	}

//	File *f = file_manager.fopen(path, "checksum.txt", FA_READ);
//	File *f = file_manager.fopen(path, "../checksum.txt", FA_READ);
	File *f = file_manager.fopen(path, "/SD/checksum.txt", FA_READ);
	if (f) {
		UINT t;
		char buf[256];
		//f->print_info();
		f->read(buf, 256, &t);
		puts(buf);
		f->close();
	} else {
		printf("File not found.\n");
	}
}
