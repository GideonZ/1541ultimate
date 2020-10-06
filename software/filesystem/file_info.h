/*
 * file_info.h
 *
 *  Created on: Apr 27, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FILE_INFO_H_
#define FILESYSTEM_FILE_INFO_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "indexed_list.h"
#include "pattern.h"
#include "cbmname.h"

/* File attribute bits for directory entry */

#define AM_RDO   0x01    /* Read only */
#define AM_HID   0x02    /* Hidden */
#define AM_SYS   0x04    /* System */
#define AM_VOL   0x08    /* Volume label */
#define AM_LFN   0x0F    /* LFN entry */
#define AM_DIR   0x10    /* Directory */
#define AM_ARC   0x20    /* Archive */
#define AM_MASK  0x3F    /* Mask of defined bits */
#define AM_HASCHILDREN 0x40 /* Is the kind of file that might have a directory listing */

#define NAME_FORMAT_DIRECT 0x01
#define NAME_FORMAT_CBM    0x02

class FileSystem;

class FileInfo
{
    FileInfo()
    {
    	init();
    }

	int compare_impl(FileInfo *other)
	{
		if ((attrib & AM_DIR) && !(other->attrib & AM_DIR))
			return -1;
		if (!(attrib & AM_DIR) && (other->attrib & AM_DIR))
			return 1;

	    int by_name = strcasecmp(lfname, other->lfname);
		return by_name;
	}

public:
	FileSystem *fs;  /* Reference to file system, to uniquely identify file */
    uint32_t  cluster; /* To compare if a file is the same even after rename */
    uint32_t  size;	 /* File size */
	uint16_t  date;	 /* Last modified date */
	uint16_t  time;	 /* Last modified time */
    uint16_t  lfsize;
    uint8_t   attrib;  /* Attribute */
    uint8_t   name_format;
    char   *lfname;
	char    extension[4];

    void init()
    {
    	memset(this, 0, sizeof(FileInfo));
    }

    FileInfo(int namesize)
	{
    	init();
		lfsize = namesize;
	    if (namesize) {
	        lfname = new char[namesize];
			lfname[0] = '\0';
		}
    }

    FileInfo(const char *name)
    {
    	init();
		lfsize = strlen(name)+1;
        lfname = new char[lfsize];
        strcpy(lfname, name);
    }

    FileInfo(FileInfo &i)
    {
    	lfsize = i.lfsize;
    	lfname = new char[lfsize];
    	copyfrom(&i);
    }

    FileInfo(FileInfo *i, const char *new_name)
    {
        fs = i->fs;
        cluster = i->cluster;
        size = i->size;
        date = i->date;
        time = i->time;
		lfsize = strlen(new_name)+1;
        lfname = new char[lfsize];
        strcpy(lfname, new_name);
        attrib = i->attrib;
        extension[0] = 0;
        name_format = i->name_format;
    }

	~FileInfo()
	{
		if(lfname)
	        delete[] lfname;
    }

	void copyfrom(FileInfo *i) {
    	fs = i->fs;
        cluster = i->cluster;
        size = i->size;
        date = i->date;
        time = i->time;
        strncpy(lfname, i->lfname, lfsize);
        strncpy(extension, i->extension, 4);
        attrib = i->attrib;
        extension[3] = 0;
        name_format = i->name_format;
	}

	bool is_directory(void) {
	    return (attrib & AM_DIR);
	}

	bool is_writable(void);

	void print_info(void) {
		printf("File System: %p\n", fs);
		printf("Cluster    : %d\n", cluster);
		printf("Size       : %d\n", size);
		printf("Date       : %d\n", date);
		printf("Time	   : %d\n", time);
		printf("LFSize     : %d\n", lfsize);
		printf("LFname     : %s\n", lfname);
		printf("Attrib:    : %b\n", attrib);
		printf("Extension  : %s\n", extension);
	}

	void generate_fat_name(char *buffer, int maxlen)
	{
	    if (name_format & NAME_FORMAT_CBM) {
	        petscii_to_fat(lfname, buffer);
	        add_extension(buffer, extension, maxlen);
	    } else {
	        buffer[maxlen-1] = 0;
	        strncpy(buffer, lfname, maxlen-1);
	    }
	}

	bool match_to_pattern(CbmFileName &cbm)
	{
        bool match_name = pattern_match_escaped(cbm.getName(), lfname);
        bool match_ext  = !cbm.hadExtension() || pattern_match(cbm.getExtension(), extension);
        return match_name && match_ext;
	}

	bool match_to_pattern(const char *pattern, CbmFileName &cbm)
	{
	    if (name_format & NAME_FORMAT_CBM) {
	        if (!cbm.isInitialized()) { // optimization, such that init is only done once for a whole bunch of files
	            cbm.init(pattern);
	        }
	        return match_to_pattern(cbm);
	    }
	    return pattern_match(pattern, lfname, false); // no escaping! Be aware!
	}

	static int compare(IndexedList<FileInfo *> *list, int a, int b)
	{
	//	printf("Compare %d and %d: ", a, b);
		FileInfo *obj_a = (*list)[a];
		FileInfo *obj_b = (*list)[b];

		if(!obj_b)
			return 1;
		if(!obj_a)
			return -1;

		return obj_a->compare_impl(obj_b);
	}
};


#endif /* FILESYSTEM_FILE_INFO_H_ */
