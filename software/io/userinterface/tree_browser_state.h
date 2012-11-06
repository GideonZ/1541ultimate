#ifndef TREEBROWSER_STATE_H
#define TREEBROWSER_STATE_H
#include "userinterface.h"

class TreeBrowser;

class TreeBrowserState
{
public:
	TreeBrowser *browser;
	int level;
	PathObject *selected;
    int first_item_on_screen;
    int selected_line; // y-cursor

    int initial_index;
    bool refresh;

    PathObject *node;
    TreeBrowserState *previous;
    TreeBrowserState *deeper;

    // Member functions
    TreeBrowserState(PathObject *node, TreeBrowser *b, int lev);
    virtual ~TreeBrowserState();

    void do_refresh();
    void update_selected();
    void draw();
    void reselect();
    void reload(void);
    virtual void highlight();
    virtual void unhighlight();
    void up(int);
    void down(int);
    void move_to_index(int);

    virtual void into(void);
    virtual void level_up(void);

    // functions only used for config menu state
    virtual void change(void) { }
    virtual void increase(void) { }
    virtual void decrease(void) { }

};

#endif
