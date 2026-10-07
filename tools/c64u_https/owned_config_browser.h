#ifndef OWNED_CONFIG_BROWSER_H
#define OWNED_CONFIG_BROWSER_H

#include "config_menu.h"

// Retail menu actions create a new page for each opening. Destroy its state
// chain before the page; states still refer to their nodes during cleanup.
// Static/cached roots continue to use the ordinary borrowing ConfigBrowser.
class OwnedConfigBrowser : public ConfigBrowser
{
public:
    OwnedConfigBrowser(UserInterface *ui, Browsable *page, int level = 0)
        : ConfigBrowser(ui, page, level) {}

    ~OwnedConfigBrowser()
    {
        replace_root_state(NULL);
        delete root;
        root = NULL;
    }
};

#endif
