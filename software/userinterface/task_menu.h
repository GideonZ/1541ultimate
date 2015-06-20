#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "menu.h"
#include "browsable.h"
#include "context_menu.h"
#include "path.h"

class TreeBrowserState;

class TaskMenu : public ContextMenu
{
	TreeBrowserState *state;
	Browsable *browsable;
	Path *path;
public:    
    TaskMenu(UserInterface *ui, TreeBrowserState *state, Path *p);
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
};

#endif
