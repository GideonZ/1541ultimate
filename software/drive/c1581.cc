/*
 * c1581.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *    Scott Hutter <scott.hutter@gmail.com>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *  Copyright (C) 2011 Daniel Kahlin <daniel@kahlin.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>

#include "itu.h"
#include "dump_hex.h"
#include "c1581.h"
#include "c64.h"
#include "userinterface.h"
#include "filemanager.h"
#include "iec.h"

C1581 ::C1581(char letter) : SubSystem(SUBSYSID_DRIVE_C)
{
}
C1581 ::~C1581()
{
}

extern IecInterface iec_if;

int C1581 :: executeCommand(SubsysCommand *cmd)
{
	bool g64;
	bool protect;
	uint8_t flags = 0;
	File *newFile = 0;
	FRESULT res;
	FileInfo info(32);

	switch(cmd->functionID) {
		case D81FILE_MOUNT:
		{
			command_t command;
			char fullpath[200];
			strcpy(fullpath, (char *)cmd->path.c_str());
			strcat(fullpath, (char *)cmd->filename.c_str());
			strcpy(command.names[0].name,  fullpath);
			
			command.cmd = "CD";
			command.digits = -1;

			IecCommandChannel *ieccmdchnl;
			ieccmdchnl = iec_if.get_command_channel();
			ieccmdchnl->exec_command(command);
			break;
		}
		default:
			printf("Unhandled menu item for C1581.\n");
			return -1;
	}
	return 0;
}
