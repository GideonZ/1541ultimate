#ifndef ASSEMBLY_SEARCH_H
#define ASSEMBLY_SEARCH_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "context_menu.h"
#include "subsys.h"
#include "json.h"

class AssemblySearchState: public TreeBrowserState
{
public:
    AssemblySearchState(Browsable *node, TreeBrowser *tb, int level);
    ~AssemblySearchState();

    void into(void) { printf("Assembly Into"); }
    void level_up(void) { printf("Assembly Up"); };
    void change(void);
    void increase(void);
    void decrease(void);
};

class AssemblySearch: public TreeBrowser
{
public:
    AssemblySearch(UserInterface *ui, Browsable *);
    virtual ~AssemblySearch();

    void init(Screen *screen, Keyboard *k);
    int handle_key(int);
    void checkFileManagerEvent(void)
    {
    } // we are not listening to file manager events
};

class BrowsableQueryField: public Browsable
{
    const char *field;
    JSON_List *presets; // When NULL, use a string edit.     
    mstring value;
    int current_preset;

    static SubsysResultCode_e update(SubsysCommand *cmd)
    {
        BrowsableQueryField *field = (BrowsableQueryField *)cmd->functionID;
        // field->value = cmd->actionName;
        field->current_preset = cmd->mode;
        field->setPreset();
    }
public:
    BrowsableQueryField(const char *field, JSON_List *presets) : field(field), presets(presets)
    {
        current_preset = 0;
    }

    ~BrowsableQueryField()
    {
    }

    const char *getName()
    {
        return field;
    }

    const char *getStringValue()
    {
        return value.c_str();
    }

    void setStringValue(const char *s)
    {
        value = s;
    }

    bool isDropDown(void)
    {
        return (presets != NULL);
    }

    void updown(int offset)
    {
        if (!presets)
            return;

        current_preset += offset;
        if (current_preset < 0)
            current_preset = 0;
        if (current_preset >= presets->get_num_elements()) {
            current_preset = presets->get_num_elements() - 1;
        }
        setPreset();
    }

    void setPreset()
    {
        JSON *el = (*presets)[current_preset];
        if (el->type() != eObject) {
            return;
        }
        JSON_Object *obj = (JSON_Object *)el;
        JSON *value = obj->get("name");
        if (!value) {
            value = obj->get("aqlKey");
        }
        if (value->type() != eString) {
            return;
        }
        setStringValue(((JSON_String *)value)->get_string());
    }

    void getDisplayString(char *buffer, int width)
    {
        // minimum window size = 38
        // Field name: 8 chars max, colon, space = 10. Left: 28
        // Max field length = 26

        memset(buffer, ' ', width+2);
        buffer[width+2] = '\0';

        if (width < 16) {
            return;
        }

        sprintf(buffer, "%s:", field);
        buffer[strlen(buffer)] = ' ';
        if (width < 16) {
            return;
        }

        if (value.length()) {
            // position 10
            sprintf(buffer+10, "\eg%#s", width-11, value.c_str());
        } else {
            sprintf(buffer+10, "\ek%#s", width-11, "________________");
        }
        buffer[0] &= 0xDF; // Capitalize ;-)
    }

    void fetch_context_items(IndexedList<Action *>&actions)
    {
        if (!presets) {
            return;
        }
        for (int i = 0; i < presets->get_num_elements(); i++) {
            JSON *el = (*presets)[i];
            if (el->type() != eObject) {
                continue;
            }
            JSON_Object *obj = (JSON_Object *)el;
            JSON *value = obj->get("name");
            if (!value) {
                value = obj->get("aqlKey");
            }
            if (value->type() != eString) {
                continue;
            }
            actions.append(new Action(((JSON_String *)value)->get_string(), update, (int)this, i));          
        }
    }

};

class Assembly
{
    char *assembly_presets =
        "[{\"type\":\"date\",\"values\":[{\"aqlKey\":\"1980\"},{\"aqlKey\":\"1981\"},{\"aqlKey\":\"1982\"},{\"aqlKey\":"
        "\"1983\"},{\"aqlKey\":\"1984\"},{\"aqlKey\":\"1985\"},{\"aqlKey\":\"1986\"},{\"aqlKey\":\"1987\"},{\"aqlKey\":"
        "\"1988\"},{\"aqlKey\":\"1989\"},{\"aqlKey\":\"1990\"},{\"aqlKey\":\"1991\"},{\"aqlKey\":\"1992\"},{\"aqlKey\":"
        "\"1993\"},{\"aqlKey\":\"1994\"},{\"aqlKey\":\"1995\"},{\"aqlKey\":\"1996\"},{\"aqlKey\":\"1997\"},{\"aqlKey\":"
        "\"1998\"},{\"aqlKey\":\"1999\"},{\"aqlKey\":\"2000\"},{\"aqlKey\":\"2001\"},{\"aqlKey\":\"2002\"},{\"aqlKey\":"
        "\"2003\"},{\"aqlKey\":\"2004\"},{\"aqlKey\":\"2005\"},{\"aqlKey\":\"2006\"},{\"aqlKey\":\"2007\"},{\"aqlKey\":"
        "\"2008\"},{\"aqlKey\":\"2009\"},{\"aqlKey\":\"2010\"},{\"aqlKey\":\"2011\"},{\"aqlKey\":\"2012\"},{\"aqlKey\":"
        "\"2013\"},{\"aqlKey\":\"2014\"},{\"aqlKey\":\"2015\"},{\"aqlKey\":\"2016\"},{\"aqlKey\":\"2017\"},{\"aqlKey\":"
        "\"2018\"},{\"aqlKey\":\"2019\"},{\"aqlKey\":\"2020\"},{\"aqlKey\":\"2021\"},{\"aqlKey\":\"2022\"},{\"aqlKey\":"
        "\"2023\"}]},{\"type\":\"rating\",\"values\":[{\"aqlKey\":\"1\"},{\"aqlKey\":\"2\"},{\"aqlKey\":\"3\"},{"
        "\"aqlKey\":\"4\"},{\"aqlKey\":\"5\"},{\"aqlKey\":\"6\"},{\"aqlKey\":\"7\"},{\"aqlKey\":\"8\"},{\"aqlKey\":"
        "\"9\"},{\"aqlKey\":\"10\"}]},{\"type\":\"type\",\"values\":[{\"aqlKey\":\"bin\"},{\"aqlKey\":\"crt\"},{"
        "\"aqlKey\":\"d64\"},{\"aqlKey\":\"d71\"},{\"aqlKey\":\"d81\"},{\"aqlKey\":\"g64\"},{\"aqlKey\":\"prg\"},{"
        "\"aqlKey\":\"sid\"},{\"aqlKey\":\"t64\"},{\"aqlKey\":\"tap\"}]},{\"type\":\"repo\",\"values\":[{\"aqlKey\":"
        "\"csdb\",\"name\":\"CSDB games\"},{\"aqlKey\":\"tapes\",\"name\":\"c64tapes.org "
        "tapes\"},{\"aqlKey\":\"tapesno\",\"name\":\"c64.no tapes\"},{\"aqlKey\":\"c64comgames\",\"name\":\"c64.com "
        "games\"},{\"aqlKey\":\"gamebase\",\"name\":\"Gamebase64\"},{\"aqlKey\":\"seuck\",\"name\":\"Seuck "
        "games\"},{\"aqlKey\":\"mayhem\",\"name\":\"Mayhem crt\"},{\"aqlKey\":\"pres\",\"name\":\"Preservers "
        "discs\"},{\"aqlKey\":\"guybrush\",\"name\":\"Guybrush games\"},{\"aqlKey\":\"c64comdemos\",\"name\":\"c64.com "
        "demos\"},{\"aqlKey\":\"hvsc\",\"name\":\"HVSC Music\"},{\"aqlKey\":\"c64orgintro\",\"name\":\"c64.org "
        "intros\"}]},{\"type\":\"category\",\"values\":[{\"aqlKey\":\"games\",\"name\":\"Games\"},{\"aqlKey\":"
        "\"demos\",\"name\":\"Demos\"},{\"aqlKey\":\"music\",\"name\":\"Music\"},{\"aqlKey\":\"graphics\",\"name\":"
        "\"Graphics\"},{\"aqlKey\":\"mags\",\"name\":\"Mags\"},{\"aqlKey\":\"tools\",\"name\":\"Tools\"},{\"aqlKey\":"
        "\"c128\",\"name\":\"C128\"},{\"aqlKey\":\"misc\",\"name\":\"Misc\"},{\"aqlKey\":\"intros\",\"name\":"
        "\"Intros\"},{\"aqlKey\":\"charts\",\"name\":\"Charts\"},{\"aqlKey\":\"reu\",\"name\":\"Reu\"},{\"aqlKey\":"
        "\"easyflash\",\"name\":\"Easyflash\"},{\"aqlKey\":\"bbs\",\"name\":\"BBS\"}]},{\"type\":\"subcat\",\"values\":"
        "[{\"id\":0,\"aqlKey\":\"games\",\"name\":\"CSDB "
        "games\"},{\"id\":12,\"aqlKey\":\"tapes\",\"name\":\"C64tapes.org "
        "tapes\"},{\"id\":13,\"aqlKey\":\"tapesno\",\"name\":\"C64.no "
        "tapes\"},{\"id\":15,\"aqlKey\":\"c64comgames\",\"name\":\"C64.com "
        "games\"},{\"id\":16,\"aqlKey\":\"gamebase\",\"name\":\"Gamebase64\"},{\"id\":17,\"aqlKey\":\"seuck\",\"name\":"
        "\"Seuck games\"},{\"id\":22,\"aqlKey\":\"mayhem\",\"name\":\"Mayhem "
        "crt\"},{\"id\":23,\"aqlKey\":\"presdisk\",\"name\":\"Preservers "
        "discs\"},{\"id\":24,\"aqlKey\":\"prestap\",\"name\":\"Preservers "
        "taps\"},{\"id\":26,\"aqlKey\":\"guybrushgames\",\"name\":\"Guybrush "
        "games\"},{\"id\":1,\"aqlKey\":\"demos\",\"name\":\"CSDB "
        "demos\"},{\"id\":14,\"aqlKey\":\"c64comdemos\",\"name\":\"C64.com "
        "demos\"},{\"id\":31,\"aqlKey\":\"guybrushdemos\",\"name\":\"Guybrush "
        "Demos\"},{\"id\":32,\"aqlKey\":\"mibridemos\",\"name\":\"Mibri "
        "demoshow\"},{\"id\":4,\"aqlKey\":\"music\",\"name\":\"CSDB "
        "music\"},{\"id\":18,\"aqlKey\":\"hvscmusic\",\"name\":\"HVSC "
        "Music\"},{\"id\":19,\"aqlKey\":\"hvscgames\",\"name\":\"HVSC "
        "games\"},{\"id\":20,\"aqlKey\":\"hvscdemos\",\"name\":\"HVSC "
        "demos\"},{\"id\":3,\"aqlKey\":\"graphics\",\"name\":\"CSDB "
        "graphics\"},{\"id\":5,\"aqlKey\":\"discmags\",\"name\":\"CSDB "
        "discmags\"},{\"id\":8,\"aqlKey\":\"tools\",\"name\":\"CSDB "
        "tools\"},{\"id\":27,\"aqlKey\":\"guybrushutils\",\"name\":\"Guybrush "
        "Utils\"},{\"id\":28,\"aqlKey\":\"guybrushutilsgerman\",\"name\":\"Guybrush Utils "
        "German\"},{\"id\":2,\"aqlKey\":\"c128stuff\",\"name\":\"CSDB "
        "128\"},{\"id\":7,\"aqlKey\":\"c64misc\",\"name\":\"CSDB "
        "misc\"},{\"id\":29,\"aqlKey\":\"guybrushgamesgerman\",\"name\":\"Guybrush Games "
        "German\"},{\"id\":30,\"aqlKey\":\"guybrushmisc\",\"name\":\"Guybrush "
        "Misc\"},{\"id\":11,\"aqlKey\":\"intros\",\"name\":\"C64.org "
        "intros\"},{\"id\":9,\"aqlKey\":\"charts\",\"name\":\"CSDB "
        "charts\"},{\"id\":25,\"aqlKey\":\"reu\",\"name\":\"CSDB "
        "reu\"},{\"id\":10,\"aqlKey\":\"easyflash\",\"name\":\"CSDB "
        "easyflash\"},{\"id\":6,\"aqlKey\":\"bbs\",\"name\":\"CSDB bbs\"}]}]";

    JSON *presets;
public:
    Assembly() {
        presets = NULL;
        convert_text_to_json_objects(assembly_presets, strlen(assembly_presets), 1000, &presets);
    }
    JSON *get_presets(void) { return presets; }
};

extern Assembly assembly;

class BrowsableAssemblyRoot: public Browsable
{
    JSON *presets;

    static SubsysResultCode_e new_search(SubsysCommand *cmd)
    {
        printf("Creating search menu...\n");
            
        Browsable *root = (Browsable *)cmd->functionID;
        AssemblySearch *searchBrowser = new AssemblySearch(cmd->user_interface, root);
        searchBrowser->init(cmd->user_interface->screen, cmd->user_interface->keyboard);
        cmd->user_interface->activate_uiobject(searchBrowser);

        // from this moment on, we loose focus.. polls will go directly to config menu!
    }
public:
    BrowsableAssemblyRoot()
    {
        presets = assembly.get_presets();
    }

    ~BrowsableAssemblyRoot()
    {
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        // name, group, handle, event, date*, category*, subcat*, rating*, type*, repo*, latest, sort, order
        if (children.get_elements() == 0) {
            children.append(new BrowsableQueryField("Name", NULL));
            children.append(new BrowsableQueryField("Group", NULL));
            children.append(new BrowsableQueryField("Handle", NULL));
            children.append(new BrowsableQueryField("Event", NULL));
            if (presets && (presets->type() == eList)) {
                JSON_List *list = (JSON_List *)presets;
                for (int i = 0; i < list->get_num_elements(); i++) {
                    JSON_Object *obj = (JSON_Object *)(*list)[i];
                    JSON *type = obj->get("type");
                    JSON *values = obj->get("values");
                    if (type && type->type() == eString && values && values->type() == eList) {
                        children.append(new BrowsableQueryField(((JSON_String *)type)->get_string(), (JSON_List *)values));
                    }
                }
            }
        }
        return &children;
    }

    void getDisplayString(char *buffer, int width) {
        sprintf(buffer, "A64     Assembly 64 Database");
    }

	void fetch_context_items(IndexedList<Action *>&items) {
        items.append(new Action("New Search..", new_search, (int)this, 0));
    }

    const char *getName()
    {
        return "Assembly 64 Search";
    }
};

#endif
