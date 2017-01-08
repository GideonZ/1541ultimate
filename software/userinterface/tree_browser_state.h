#ifndef TREEBROWSER_STATE_H
#define TREEBROWSER_STATE_H

#include "userinterface.h"
#include "browsable.h"

class TreeBrowser;

class TreeBrowserState
{
public:
	int level;
	TreeBrowser *browser; // owner

	int cursor_pos;
	Browsable *under_cursor; // on cursor

	int first_item_on_screen;
    int selected_line; // y-cursor

    int initial_index;
    bool refresh;
    bool needs_reload;

    Browsable *node;
    TreeBrowserState *previous;
    TreeBrowserState *deeper;
    IndexedList<Browsable *> *children;

    // Member functions
    TreeBrowserState(Browsable *node, TreeBrowser *b, int lev);
    virtual ~TreeBrowserState();
    virtual void cleanup();

    virtual void do_refresh();
    virtual void update_selected();
    virtual void draw();
    virtual void draw_item(Browsable *t, int line, bool selected);

    //    virtual void reselect();
    virtual void reload(void);
    virtual void up(int);
    virtual void down(int);
    virtual void move_to_index(int);

    virtual void into(void);
	virtual bool into2(void);
    virtual void into3(const char* name);
    virtual void level_up(void);
    virtual void select(void);
    virtual void select_all(bool);

    // functions only used for config menu state
    virtual void change(void) { }
    virtual void increase(void) { }
    virtual void decrease(void) { }
};

#endif
