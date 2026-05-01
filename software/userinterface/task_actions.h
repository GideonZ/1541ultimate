#ifndef TASK_ACTIONS_H_
#define TASK_ACTIONS_H_

#include "action.h"

void ensure_task_actions_created(bool writablePath);
Action *find_task_action(int subsysId, const char *actionName);

#endif
