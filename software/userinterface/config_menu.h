#ifndef CONFIG_MENU_H
#define CONFIG_MENU_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "context_menu.h"
#include "config.h"
#include "subsys.h"

#define BR_EVENT_OUT   1
#define BR_EVENT_CLOSE 2
#define BR_EVENT_OPEN  3

class ConfigBrowserState: public TreeBrowserState
{
public:
    ConfigBrowserState(Browsable *node, TreeBrowser *tb, int level);
    ~ConfigBrowserState();

    void into(void);
    void level_up(void);
    void change(void);
    void increase(void);
    void decrease(void);
    void on_close(void);
};

class ConfigBrowser: public TreeBrowser
{
    void on_exit(void);
    int start_level;
public:
    ConfigBrowser(UserInterface *ui, Browsable *, int level = 0);
    virtual ~ConfigBrowser();

    virtual void init();
    virtual int handle_key(int);
    virtual void checkFileManagerEvent(void);

    static void start(UserInterface *ui);
};

class BrowsableConfigItem: public Browsable
{
    ConfigItem *item;

    static SubsysResultCode_e updateItem(SubsysCommand *cmd)
    {
        ConfigItem *item = (ConfigItem *)(cmd->functionID);
        item->execute(cmd->mode);
        return SSRET_OK;
    }

    static SubsysResultCode_e updateString(SubsysCommand *cmd)
    {
        ConfigItem *item = (ConfigItem *)(cmd->functionID);
        printf("Update String: %s\n", cmd->actionName.c_str());
        const char *actionString = cmd->actionName.c_str();
        item->setString(actionString);
        return SSRET_OK;
    }

    static SubsysResultCode_e requestString(SubsysCommand *cmd)
    {
        ConfigItem *item = (ConfigItem *)(cmd->functionID);
        char buffer[36];
        strncpy(buffer, item->getString(), 32);
        buffer[32] = 0;
        if (cmd->user_interface) {
            int ret = cmd->user_interface->string_box(item->definition->item_text, buffer, 32);
            if (ret > 0) {
                item->setString(buffer);
            }
        }
        return SSRET_OK;
    }
public:
    BrowsableConfigItem(ConfigItem *i)
    {
        item = i;
        selectable = (i->definition->type != CFG_TYPE_SEP) && (i->definition->type != CFG_TYPE_INFO);
    }
    ~BrowsableConfigItem()
    {
    }

    ConfigItem *getItem()
    {
        return item;
    }
    const char *getName()
    {
        return item->get_item_altname();
    }

    void getDisplayString(char *buffer, int width)
    {
        item->get_display_string(buffer, width);
    }

    void fetch_context_items(IndexedList<Action *>&actions)
    {
        static char buf[32];
        int i, ret = 0;
        t_cfg_strfunc func;
        IndexedList<char *> strings(8, NULL);

        switch (item->definition->type) {
            case CFG_TYPE_VALUE:
                for (i = item->definition->min; i <= item->definition->max; i++) {
                    sprintf(buf, item->definition->item_format, i);
                    actions.append(new Action(buf, updateItem, (int)item, i));
                }
                break;
            case CFG_TYPE_ENUM:
                for (i = item->definition->min; i <= item->definition->max; i++) {
                    sprintf(buf, item->definition->item_format, item->definition->items[i]);
                    actions.append(new Action(buf, updateItem, (int)item, i));
                }
                break;
            case CFG_TYPE_STRFUNC: // drop down type
                func = (t_cfg_strfunc)(item->definition->items);
                func(item, strings);
                for (int i = 0; i < strings.get_elements(); i++) {
                    actions.append(new Action(strings[i], updateString, (int)item, 0));
                    delete strings[i];
                }
                actions.append(new Action("Enter Manually", requestString, (int)item, 0));
                break;
            default:
                printf("Item not contextable\n");
        }
    }

};

class BrowsableConfigStore: public Browsable
{
    ConfigStore *store;
    IndexedList<Browsable *> children;
    public:
    BrowsableConfigStore(ConfigStore *s) :
            children(4, NULL)
    {
        store = s;
    }
    ~BrowsableConfigStore()
    {
        for (int i = 0; i < children.get_elements(); i++) {
            delete children[i];
        }
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        if (children.get_elements() == 0) {
            IndexedList<ConfigItem *> *itemList = store->getItems();
            store->at_open_config();
            for (int i = 0; i < itemList->get_elements(); i++) {
                children.append(new BrowsableConfigItem((*itemList)[i]));
            }
        }
        return &children;
    }

    ConfigStore *getStore()
    {
        return store;
    }

    const char *getName()
    {
        return store->get_alt_store_name();
    }

    void event(int code)
    {
        switch(code) {
        case BR_EVENT_OUT:
            store->at_close_config();
            break;
        case BR_EVENT_CLOSE:
            store->at_close_config();
            break;
        case BR_EVENT_OPEN:
            store->at_open_config();
            break;
        };
    }

    // BrowsableConfigItem *findConfigItem(uint8_t id)
    // {
    //     for (int i = 0; i < children.get_elements(); i++) {
    //         BrowsableConfigItem *bit = ((BrowsableConfigItem *)children[i]);
    //         if (bit->getItem()->definition->id == id) {
    //             return bit;
    //         }
    //     }
    //     return NULL;
    // }
};

class BrowsableConfigGroup: public Browsable
{
    ConfigGroup *group;
    IndexedList<Browsable *> children;
    public:
    BrowsableConfigGroup(ConfigGroup *g) :
            children(4, NULL)
    {
        group = g;
    }
    ~BrowsableConfigGroup()
    {
        for (int i = 0; i < children.get_elements(); i++) {
            delete children[i];
        }
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        if (children.get_elements() == 0) {
            IndexedList<ConfigItem *> *itemList = group->getConfigItems();
            // store->at_open_config(); FIXME
            for (int i = 0; i < itemList->get_elements(); i++) {
                children.append(new BrowsableConfigItem((*itemList)[i]));
            }
        }
        return &children;
    }

    const char *getName()
    {
        return group->getName();
    }

    void event(int code)
    {
        IndexedList<ConfigStore *> *stores;
        switch(code) {
        case BR_EVENT_OUT:
            printf("Leaving the config group '%s'\n", group->getName());
            stores = group->getStores();
            for(int i=0; i < stores->get_elements(); i++) {
                ConfigStore *s = (*stores)[i];
                s->effectuate();
            }
            printf("You left!!\n");
            break;
        case BR_EVENT_CLOSE:
            printf("Closing the config group '%s'\n", group->getName());
            stores = group->getStores();
            for(int i=0; i < stores->get_elements(); i++) {
                ConfigStore *s = (*stores)[i];
                s->at_close_config();
            }
            break;
        case BR_EVENT_OPEN:
            stores = group->getStores();
            for(int i=0; i < stores->get_elements(); i++) {
                ConfigStore *s = (*stores)[i];
                s->at_open_config();
            }
            break;
        };
    }

    // BrowsableConfigItem *findConfigItem(uint8_t id)
    // {
    //     for (int i = 0; i < children.get_elements(); i++) {
    //         BrowsableConfigItem *bit = ((BrowsableConfigItem *)children[i]);
    //         if (bit->getItem()->definition->id == id) {
    //             return bit;
    //         }
    //     }
    //     return NULL;
    // }
};


class BrowsableConfigRoot: public Browsable
{
    IndexedList<Browsable *> children;
public:
    BrowsableConfigRoot() :
            children(4, NULL)
    {

    }
    ~BrowsableConfigRoot()
    {
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        if (children.get_elements() == 0) {
            IndexedList<ConfigStore *> *storeList = ConfigManager::getConfigManager()->getStores();
            for (int i = 0; i < storeList->get_elements(); i++) {
                if (!(*storeList)[i]->isHidden())
                    children.append(new BrowsableConfigStore((*storeList)[i]));
            }

            // ConfigGroupCollection::getConfigGroupCollection()->sort();
            IndexedList<ConfigGroup *> *groupList = ConfigGroupCollection::getGroups();
            for (int i = 0; i < groupList->get_elements(); i++) {
                children.append(new BrowsableConfigGroup((*groupList)[i]));
            }

            children.sort(Browsable::compare_alphabetically);
        }
        return &children;
    }
    const char *getName()
    {
        return "Browsable Config Root";
    }
};

class BrowsableConfigRootPredefined : public Browsable
{
    const char **names;
    int count;
    IndexedList<Browsable *> children;
public:
    BrowsableConfigRootPredefined(int count, const char *names[]) :
            children(count, NULL), names(names), count(count)
    {

    }

    ~BrowsableConfigRootPredefined()
    {
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        if (children.get_elements() == 0) {
            for (int i=0; i<count; i++) {
                ConfigGroup *grp = ConfigGroupCollection::getGroup(names[i], -1); // do not create
                if (grp) {
                    children.append(new BrowsableConfigGroup(grp));
                    continue;
                }
                ConfigStore *store = ConfigManager::getConfigManager()->find_store(names[i]);
                if (store) {
                    children.append(new BrowsableConfigStore(store));
                }
            }
        }
        return &children;
    }
    const char *getName()
    {
        return "Browsable Config Predefined";
    }
};

#endif
