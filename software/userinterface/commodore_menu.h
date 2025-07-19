#ifndef COMMODORE_MENU_H
#define COMMODORE_MENU_H

#include "context_menu.h"
#include "tree_browser.h"

class CommodoreMenu: public ContextMenu
{
    static SubsysResultCode_e S_file_browser(Action *act, void *context);
    static SubsysResultCode_e S_cfg_page(Action *act, void *context);
    static SubsysResultCode_e S_advanced(Action *act, void *context);

    TreeBrowser *root_tree_browser;

public:
    CommodoreMenu(UserInterface *ui);
    virtual ~CommodoreMenu();

    virtual void init(Screen *screen, Keyboard *k);
    void redraw();
    int select_item(void);
};

#endif
