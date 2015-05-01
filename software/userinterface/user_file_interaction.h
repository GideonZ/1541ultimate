/*
 * user_file_interaction.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_USER_FILE_INTERACTION_H_
#define USERINTERFACE_USER_FILE_INTERACTION_H_

#include "userinterface.h"
#include "c1541.h"

class UserFileInteraction {
public:
	UserFileInteraction();
	virtual ~UserFileInteraction();
};

#define FILEDIR_RENAME   			0x2001
#define FILEDIR_DELETE   			0x2002
#define FILEDIR_ENTERDIR 			0x2003
#define FILEDIR_DELETE_CONTINUED   	0x2004
#define FILEDIR_VIEW                0x2005

#define MENU_CREATE_D64    0x3001
#define MENU_CREATE_DIR    0x3002
#define MENU_CREATE_G64    0x3003

/* Debug options */
#define MENU_DUMP_INFO     0x30FE
#define MENU_DUMP_OBJECT   0x30FF

#endif /* USERINTERFACE_USER_FILE_INTERACTION_H_ */
