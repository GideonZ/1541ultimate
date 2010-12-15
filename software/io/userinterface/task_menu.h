#ifndef TASK_MENU_H
#define TASK_MENU_H

#include "context_menu.h"

class TaskMenu : public ContextMenu
{
    // private functions:
public:    
    TaskMenu(PathObject *node, PathObject *obj);
    ~TaskMenu(void);
    
    void init(Screen *pwin, Keyboard *keyb);
};

extern IndexedList<PathObject*> main_menu_static_items;

class TaskMenuManager /* just here because we need automatic clean up on exit */
{
public:
    TaskMenuManager() {}

    ~TaskMenuManager() {
        PathObject *o;
        for(int i=0;i<main_menu_static_items.get_elements();i++) {
            o = (PathObject *)main_menu_static_items[i];
            delete o;
        }
    }
};

#endif
