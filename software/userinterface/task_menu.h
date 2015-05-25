#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "menu.h"
#include "context_menu.h"

class TaskMenu : public ContextMenu
{
    // private functions:
public:    
    TaskMenu();
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
};

extern IndexedList<ObjectWithMenu*> main_menu_static_items;

/*
class TaskMenuManager  just here because we need automatic clean up on exit
{
public:
    TaskMenuManager() {}

    ~TaskMenuManager() {
        CachedTreeNode *o;
        for(int i=0;i<main_menu_static_items.get_elements();i++) {
            o = (CachedTreeNode *)main_menu_static_items[i];
            delete o;
        }
    }
};
*/

#endif
