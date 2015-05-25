#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include "indexed_list.h"
#include "action.h"
#include "contextable.h"
#include "userinterface.h"

typedef enum _context_state {
    e_new = 0,
    e_active,
    e_finished
} t_context_state;

class ContextMenu : public UIObject
{
/* Quick Seek */
	char quick_seek_string[MAX_SEARCH_LEN];
    int  quick_seek_length;
    void reset_quick_seek(void);
    bool perform_quick_seek(void);

	// private functions:
    virtual int handle_key(int c);

    Contextable *contextable;

    Screen   *screen;
    Window   *window;
    Keyboard *keyb;

    
    // some things we like to keep track
    t_context_state context_state;
    IndexedList<Action *> actions;
    int item_index;
    int first;
    int y_offs;
    int corner;
public:
    ContextMenu(Contextable *node, int initial, int y);
    virtual ~ContextMenu(void);
    
    void executeAction();

    virtual void init(Window *pwin, Keyboard *keyb);
    virtual int poll(int, Event &e);
    virtual void draw();

    friend class TaskMenu;
};

#endif
