#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "menu.h"
#include "browsable.h"
#include "context_menu.h"

class TaskMenu : public ContextMenu
{
	Browsable *browsable;
public:    
    TaskMenu(Browsable *b);
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
};

extern IndexedList<ObjectWithMenu*> main_menu_static_items;

#endif
