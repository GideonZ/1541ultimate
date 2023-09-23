/*
 * user_file_interaction.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_USER_FILE_INTERACTION_H_
#define USERINTERFACE_USER_FILE_INTERACTION_H_

#include "indexed_list.h"
#include "menu.h"
#include "subsys.h"
#include "filemanager.h"

enum EditorType { TEXT_EDITOR, HEX_EDITOR};

class Path;
class Action;
class BrowsableDirEntry;

class UserFileInteraction : public SubSystem, ObjectWithMenu {
	UserFileInteraction() : SubSystem(SUBSYSID_USER_STANDARD) { }
	Action *mkdir;

public:
	static int S_enter(SubsysCommand *cmd);
	static int S_rename(SubsysCommand *cmd);
	static int S_delete(SubsysCommand *cmd);
	static int S_view(SubsysCommand *cmd);
	static int S_hex_view(SubsysCommand *cmd);
	static int S_createDir(SubsysCommand *cmd);
	static int S_runApp(SubsysCommand *cmd);

	static UserFileInteraction *getUserFileInteractionObject(void) {
		static UserFileInteraction u;
		return &u;
	}

	// SubSystem
	const char *identify() { return "User File Interaction Module"; }

	// object with menu
	void create_task_items(void);
	void update_task_items(bool writablePath, Path *path);

	int fetch_context_items(BrowsableDirEntry *br, IndexedList<Action *> &list);
};

FRESULT create_file_ask_if_exists(FileManager *fm, UserInterface *ui, const char *path, const char *filename, File **f);
FRESULT create_user_file(UserInterface *ui, const char *message, const char *ext, const char *path, File **f, char *name_buffer);
FRESULT write_zeros(File *f, int size, uint32_t &written);


#endif /* USERINTERFACE_USER_FILE_INTERACTION_H_ */
