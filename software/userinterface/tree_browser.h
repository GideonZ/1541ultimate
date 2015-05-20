#ifndef TREEBROWSER_H
#define TREEBROWSER_H

#include "userinterface.h"
#include "browsable.h"
#include "ui_elements.h"

class TreeBrowserState;
class ContextMenu;
class ConfigBrowser;

class TreeBrowser : public UIObject
{
public:
    char quick_seek_string[MAX_SEARCH_LEN];
    int  quick_seek_length;

    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
    //Path 	 *path;

    TreeBrowserState *state;

    // link to temporary popup
    ContextMenu *contextMenu; // anchor for menu that pops up
    ConfigBrowser *configBrowser;

    // Member functions
    TreeBrowser(Browsable *);
    virtual void initState(Browsable *);
    virtual ~TreeBrowser();

    virtual void init(Screen *scr, Keyboard *k);
    void deinit(void);

    virtual int poll(int, Event &e);
    virtual int handle_key(int);

    void reset_quick_seek(void);
    bool perform_quick_seek(void);
    
    void context(int);
    void task_menu(void);
    void config(void);
    void test_editor(void);
    
    //void invalidate(Browsable *obj);

};
#endif
