#ifndef TREEBROWSER_H
#define TREEBROWSER_H

#include "userinterface.h"
#include "browsable.h"
#include "ui_elements.h"
#include "observer.h"
#include "filemanager.h"

class TreeBrowserState;
class ContextMenu;
class ConfigBrowser;

#define MAX_SEARCH_LEN_TB 32

class TreeBrowser : public UIObject
{
public:
    char quick_seek_string[MAX_SEARCH_LEN_TB];
    int  quick_seek_length;

    FileManager *fm;
    UserInterface *user_interface;
    ObserverQueue *observerQueue;
    Browsable *root;
    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
    Path 	 *path;

    TreeBrowserState *state_root;
    TreeBrowserState *state;

    // link to temporary popup
    ContextMenu *contextMenu; // anchor for menu that pops up
    ConfigBrowser *configBrowser;

    // Member functions
    TreeBrowser(UserInterface *ui, Browsable *);
    virtual ~TreeBrowser();

    virtual void init(Screen *scr, Keyboard *k);
    void deinit(void);

    virtual int poll(int);
    virtual int handle_key(int);

    void reset_quick_seek(void);
    bool perform_quick_seek(void);
    
    void context(int);
    void task_menu(void);
    void config(void);
    void test_editor(void);
    
    void invalidate(const void *obj);
    const char *getPath();
};
#endif
