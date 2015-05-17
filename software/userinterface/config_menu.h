#ifndef CONFIG_MENU_H
#define CONFIG_MENU_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "config.h"

class ConfigBrowserState : public TreeBrowserState
{
public:
	ConfigBrowserState(Browsable *node, TreeBrowser *tb, int level);
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
	ConfigBrowser(Browsable *);
	virtual void initState(Browsable *);
	virtual ~ConfigBrowser();

    virtual void init(Screen *screen, Keyboard *k);
    virtual int handle_key(BYTE);
};

class BrowsableConfigItem : public Browsable
{
	ConfigItem *item;
public:
	BrowsableConfigItem(ConfigItem *i) {
		item = i;
	}
	~BrowsableConfigItem() {}

	char *getName() { return item->get_item_name(); }
	char *getDisplayString() { return item->get_display_string(); };
};

class BrowsableConfigStore : public Browsable
{
	ConfigStore *store;
public:
	BrowsableConfigStore(ConfigStore *s) {
		store = s;
	}
	~BrowsableConfigStore() {}

	int getSubItems(IndexedList<Browsable *>&list) {
		IndexedList<ConfigItem *> *itemList = store->getItems();
		for (int i=0; i < itemList->get_elements(); i++) {
			list.append(new BrowsableConfigItem((*itemList)[i]));
		}
		return itemList->get_elements();
	}
	char *getName() { return store->get_store_name(); }
};

class BrowsableConfigRoot : public Browsable
{
public:
	BrowsableConfigRoot() {}
	~BrowsableConfigRoot() {}

	int getSubItems(IndexedList<Browsable *>&list) {
		IndexedList<ConfigStore *> *storeList = config_manager.getStores();
		for (int i=0; i < storeList->get_elements(); i++) {
			list.append(new BrowsableConfigStore((*storeList)[i]));
		}
		return storeList->get_elements();
	}
	char *getName() { return "Browsable Config Root"; }
};




#endif
