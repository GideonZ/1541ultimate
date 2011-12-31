#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include "tree_browser.h"
#include "tree_browser_state.h"

typedef enum _context_state {
    e_new = 0,
    e_active,
    e_finished
} t_context_state;

class ContextMenu : public TreeBrowser
{
    // private functions:
    virtual int handle_key(char c);

public:
	PathObject *object;

    Screen *parent_win;
    
    // some things we like to keep track
    t_context_state context_state;
    int items;
    int rows, y_offs, size_y;
    int len, max_len;
    int corner;

    ContextMenu(PathObject *node, PathObject *obj, int initial, int y);
    virtual ~ContextMenu(void);
    
    virtual void init(Screen *pwin, Keyboard *keyb);
    virtual int poll(int, Event &e);
};

#endif
