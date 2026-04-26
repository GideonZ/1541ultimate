#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "menu.h"
#include "browsable.h"
#include "context_menu.h"
#include "path.h"

class TreeBrowserState;

class TaskMenu : public ContextMenu
{
    static bool actionsCreated;
    TreeBrowserState *state;
	Browsable *browsable;
	Path *path;
public:    
    TaskMenu(UserInterface *ui, TreeBrowserState *state, Path *p);
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
    int select_item(void);
};

class TaskSubMenu : public ContextMenu
{
    TaskCategory *category;
public:
    TaskSubMenu(UserInterface *ui, TreeBrowserState *state, TaskCategory *cat, int first, int item);
    ~TaskSubMenu() { }
    int get_items(void);
};

#endif
