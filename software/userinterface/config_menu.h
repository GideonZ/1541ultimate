#ifndef CONFIG_MENU_H
#define CONFIG_MENU_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "context_menu.h"
#include "config.h"
#include "subsys.h"


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
};

class ConfigBrowser : public TreeBrowser
{
public:
	ConfigBrowser(UserInterface *ui, Browsable *);
	virtual ~ConfigBrowser();

    virtual void init(Screen *screen, Keyboard *k);
    virtual int handle_key(int);
};

class BrowsableConfigItem : public Browsable
{
	ConfigItem *item;

	static int updateItem(SubsysCommand *cmd) {
		ConfigItem *item = (ConfigItem *)(cmd->functionID);
		item->execute(cmd->mode);
		return 0;
	}

public:
	BrowsableConfigItem(ConfigItem *i) {
		item = i;
	}
	~BrowsableConfigItem() {}

	ConfigItem *getItem() { return item; }
	const char *getName() { return item->get_item_name(); }

	void getDisplayString(char *buffer, int width) {
		item->get_display_string(buffer, width);
	}

	static int contextSelect(SubsysCommand *cmd) {
		ConfigItem *it = (ConfigItem *)cmd->functionID;
		printf("ContextSelect of item %s, set value to %d.\n", it->get_item_name(), cmd->mode);
		it->value = cmd->mode;
		return 0;
	}

	void fetch_context_items(IndexedList<Action *>&items) {
		static char buf[32];
		int i,ret = 0;

		switch(item->definition->type) {
			case CFG_TYPE_VALUE:
				printf("CFG_TYPE_VALUE\n");
				for(i=item->definition->min;i<=item->definition->max;i++) {
					sprintf(buf, item->definition->item_format, i);
					items.append(new Action(buf, BrowsableConfigItem :: contextSelect, (int)item, i));
				}
				break;
			case CFG_TYPE_ENUM:
				for(i=item->definition->min;i<=item->definition->max;i++) {
					sprintf(buf, item->definition->item_format, item->definition->items[i]);
					items.append(new Action(buf, updateItem, (int)item, i));
				}
				break;
			default:
				printf("Item not contextable\n");
		}
	}

};

class BrowsableConfigStore : public Browsable
{
	ConfigStore *store;
	IndexedList<Browsable *>children;
public:
	BrowsableConfigStore(ConfigStore *s) : children(4, NULL) {
		store = s;
	}
	~BrowsableConfigStore() {
		for (int i=0; i < children.get_elements(); i++) {
			delete children[i];
		}
	}

	IndexedList<Browsable *> *getSubItems(int &error) {
		if (children.get_elements() == 0) {
			IndexedList<ConfigItem *> *itemList = store->getItems();
			store->read();
			for (int i=0; i < itemList->get_elements(); i++) {
				children.append(new BrowsableConfigItem((*itemList)[i]));
			}
		}
		return &children;
	}

	ConfigStore *getStore() { return store; }
	const char *getName() { return store->get_store_name(); }
};

class BrowsableConfigRoot : public Browsable
{
	IndexedList<Browsable *>children;
public:
	BrowsableConfigRoot()  : children(4, NULL) {

	}
	~BrowsableConfigRoot() {}

	IndexedList<Browsable *> *getSubItems(int &error) {
		if (children.get_elements() == 0) {
			IndexedList<ConfigStore *> *storeList = ConfigManager :: getConfigManager()->getStores();
			for (int i=0; i < storeList->get_elements(); i++) {
				children.append(new BrowsableConfigStore((*storeList)[i]));
			}
		}
		return &children;
	}
	const char *getName() { return "Browsable Config Root"; }
};


#endif
