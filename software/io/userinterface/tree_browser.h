#ifndef TREEBROWSER_H
#define TREEBROWSER_H
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

    int default_color;
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
    void highlight();
    void unhighlight();
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

class TreeBrowser : public UIObject
{
public:
    char quick_seek_string[MAX_SEARCH_LEN];
    int  quick_seek_length;

    Screen   *window;
    Keyboard *keyb;
    Path 	 *path;

    TreeBrowserState *state;

    // link to temporary popup
    PathObject  *menu_node;    // dummy root to browse children
    TreeBrowser *menu_browser; // anchor for menu that pops up

    // Member functions
    TreeBrowser();
    virtual ~TreeBrowser();

    virtual void init(Screen *win, Keyboard *k);
    void deinit(void);

    virtual int poll(int, Event &e);
    virtual int handle_key(char);

    void reset_quick_seek(void);
    bool perform_quick_seek(void);
    
    void context(int);
    void task_menu(void);
    void config(void);

    void invalidate(PathObject *obj);
};
#endif
