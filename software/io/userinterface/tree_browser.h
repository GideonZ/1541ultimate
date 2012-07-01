#ifndef TREEBROWSER_H
#define TREEBROWSER_H
#include "userinterface.h"

class TreeBrowserState;

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
    void test_editor(void);
    
    void invalidate(PathObject *obj);
};
#endif
