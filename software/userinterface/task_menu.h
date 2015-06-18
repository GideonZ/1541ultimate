#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "menu.h"
#include "browsable.h"
#include "context_menu.h"

class TreeBrowserState;

class TaskMenu : public ContextMenu
{
	TreeBrowserState *state;
	Browsable *browsable;
public:    
    TaskMenu(UserInterface *ui, TreeBrowserState *state);
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
};

#endif
