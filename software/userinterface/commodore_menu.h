#ifndef COMMODORE_MENU_H
#define COMMODORE_MENU_H

#include "context_menu.h"
#include "tree_browser.h"
#include "menu.h"
#include "subsys.h"

class CommodoreMenu: public ContextMenu, ObjectWithMenu
{
    static SubsysResultCode_e S_file_browser(Action *act, void *context);
    static SubsysResultCode_e S_cfg_page(Action *act, void *context);
    static SubsysResultCode_e S_advanced(Action *act, void *context);
    static SubsysResultCode_e S_assembly64(Action *act, void *context);

    static SubsysResultCode_e S_SendMenuKey(SubsysCommand *cmd) {
        cmd->user_interface->send_keystroke(KEY_MENU);
        return SSRET_OK;
    }

    TreeBrowser *root_tree_browser;

public:
    CommodoreMenu(UserInterface *ui);
    virtual ~CommodoreMenu();

    virtual void init(Screen *screen, Keyboard *k);
    void redraw();
    int poll(int);
    int select_item(void);
    
    void create_task_items(void) {
        // nasty
        TaskCategory *cat = TasksCollection :: getCategory("Return to Main Menu", -1);
        cat->append(new Action("Return", S_SendMenuKey, 0, 0));
    };

};

#endif
