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
	static void S_enter(void *obj, void *param);
	static void S_rename(void *obj, void *param);
	static void S_delete(void *obj, void *param);
	static void S_view(void *obj, void *param);
	static void S_createD64(void *obj, void *param);
	static void S_createDir(void *obj, void *param);

public:
	static int fetch_context_items(CachedTreeNode *node, IndexedList<Action *> &list);
	static int fetch_task_items(CachedTreeNode *node, IndexedList<Action *> &list);
};

#endif /* USERINTERFACE_USER_FILE_INTERACTION_H_ */
