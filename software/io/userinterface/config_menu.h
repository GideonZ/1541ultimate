#ifndef CONFIG_MENU_H
#define CONFIG_MENU_H

#include "tree_browser.h"
#include "tree_browser_state.h"

class ConfigBrowserState : public TreeBrowserState
{
public:
	ConfigBrowserState(PathObject *node, TreeBrowser *tb, int level);
	~ConfigBrowserState();

	void into(void);
	void level_up(void);
    void change(void);
    void increase(void);
    void decrease(void);
    void unhighlight(void);
    void highlight(void);
};

class ConfigBrowser : public TreeBrowser
{
public:
	ConfigBrowser();
    virtual ~ConfigBrowser();

    virtual void init(Screen *screen, Keyboard *k);
    virtual int handle_key(char);
};


#endif
