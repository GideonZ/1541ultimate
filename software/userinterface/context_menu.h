#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include "indexed_list.h"
#include "action.h"
#include "browsable.h"
#include "userinterface.h"

class TreeBrowserState;

#define MAX_SEARCH_LEN 20

typedef enum _context_state {
    e_new = 0,
    e_active,
    e_finished
} t_context_state;

class ContextMenu;

class ContextMenu : public UIObject
{
/* Quick Seek */
	char quick_seek_string[MAX_SEARCH_LEN];
    int  quick_seek_length;
    void reset_quick_seek(void);
    bool perform_quick_seek(void);

	// private functions:
    virtual int handle_key(int c);

    UserInterface *user_interface;
    TreeBrowserState *state;
    Action *selectedAction;
    Browsable *contextable;
    ContextMenu *subContext;

    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
    // some things we like to keep track
    t_context_state context_state;
    IndexedList<Action *> actions;
    int item_index;
    int first;
    int y_offs;
    int hook_y;
    int corner;
    int when_done; // what to do when done with this menu (MENU_CLOSE, MENU_EXIT, MENU_HIDE)
    int indent;

    void page_up(void);
    void page_down(void);
    void seek_char(int c);
    void down(void);
    void up(void);
    void help(void);
public:
    ContextMenu(UserInterface *ui, TreeBrowserState *state, int initial, int y, int when_done = MENU_CLOSE, int indent=0);
    virtual ~ContextMenu(void);
    
    int     executeSelected(const char *p);
    Action *getSelectedAction(void) { return selectedAction; }
    Browsable *getContextable(void) { return contextable; }

    void appendAction(Action *a) { actions.append(a); }
    virtual int get_items();
    virtual void init(Window *pwin, Keyboard *keyb);
    virtual void deinit(void);
    virtual int poll(int);
    virtual void draw();
    virtual void redraw(void);
    virtual int select_item(void);
    friend class TaskMenu;
    friend class CommodoreMenu;
};

#endif
