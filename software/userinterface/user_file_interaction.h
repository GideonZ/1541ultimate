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

class Path;
class Action;
class BrowsableDirEntry;

class UserFileInteraction : public SubSystem, ObjectWithMenu {
	UserFileInteraction() : SubSystem(SUBSYSID_USER_STANDARD) { }

public:
	static int S_enter(SubsysCommand *cmd);
	static int S_rename(SubsysCommand *cmd);
	static int S_delete(SubsysCommand *cmd);
	static int S_view(SubsysCommand *cmd);
	static int S_createD64(SubsysCommand *cmd);
	static int S_createDir(SubsysCommand *cmd);

	static UserFileInteraction *getUserFileInteractionObject(void) {
		static UserFileInteraction u;
		return &u;
	}

	// SubSystem
	const char *identify() { return "User File Interaction Module"; }

	// object with menu
	int fetch_task_items(Path *path, IndexedList<Action *> &list);

	int fetch_context_items(BrowsableDirEntry *br, IndexedList<Action *> &list);
};

#endif /* USERINTERFACE_USER_FILE_INTERACTION_H_ */
