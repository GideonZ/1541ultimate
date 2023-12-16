#ifndef ASSEMBLY_SEARCH_H
#define ASSEMBLY_SEARCH_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "context_menu.h"
#include "subsys.h"
#include "json.h"

class AssemblySearchForm: public TreeBrowserState
{
    void send_query(void);
public:
    AssemblySearchForm(Browsable *node, TreeBrowser *tb, int level);
    ~AssemblySearchForm();

    void into(void) { printf("Seach Form Into\n"); }
    void level_up(void) { printf("Search Form Up\n"); };
    void change(void);
    void increase(void);
    void decrease(void);
};

class AssemblyResultsView: public TreeBrowserState
{
    void get_entries(void) { }
public:
    AssemblyResultsView(Browsable *node, TreeBrowser *tb, int level);
    ~AssemblyResultsView();

    void into(void) { printf("Results Into\n"); }
    void level_up(void) { printf("Results Up\n"); };
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

        if (field[0] == '$') {
            sprintf(buffer, "           \eR <<  Search  >> \er");
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
            sprintf(buffer+10, "\ek%#s", width-11, "__________________");
        }
        if (buffer[0] > 0x60)
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
    BrowsableAssemblyRoot();

    ~BrowsableAssemblyRoot()
    {
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        // name, group, handle, event, date*, category*, subcat*, rating*, type*, repo*, latest, sort, order
        if (children.get_elements() == 0) {
            children.append(new BrowsableStatic("\em  Assembly 64 Query Form"));
            children.append(new BrowsableStatic(""));
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
            children.append(new BrowsableStatic(""));
            children.append(new BrowsableStatic(""));
            children.append(new BrowsableQueryField("$", NULL));
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

class BrowsableQueryResult: public Browsable
{
    mstring summary;
    //mstring name;
    //mstring group;
    //mstring year;
    mstring id;
    int category;
public:
    BrowsableQueryResult(JSON_Object *result)
    {
        JSON *j;
        const char *year = "";

        j = result->get("name");
        if (j && j->type() == eString) {
            summary = ((JSON_String *)j)->get_string();
        }
        j = result->get("year");
        if (j && j->type() == eString) {
            year = ((JSON_String *)j)->get_string();
        }
        j = result->get("group");
        if (j && j->type() == eString) {
            summary += "(";
            summary += ((JSON_String *)j)->get_string();
            if (year) {
                summary += ", ";
                summary += year;
            }
            summary += ")";
        }
        category = 0;
        j = result->get("category");
        if (j && j->type() == eInteger) {
            category = ((JSON_Integer *)j)->get_value();
        }
        j = result->get("id");
        if (j && j->type() == eString) {
            id = ((JSON_String *)j)->get_string();
        }
    }

    ~BrowsableQueryResult()
    {
    }

    const char *getName()
    {
        return summary.c_str();
    }

    void getDisplayString(char *buffer, int width)
    {
        memset(buffer, ' ', width+2);
        buffer[width+2] = '\0';

        strncpy(buffer, summary.c_str(), width);
    }

    IndexedList<Browsable *> *getSubItems(int &error);

};

class BrowsableQueryResults : public Browsable // Root of results screen
{
public:
    BrowsableQueryResults(JSON_List *results)
    {
        for(int i=0; i<results->get_num_elements(); i++) {
            JSON *j = (*results)[i];
            if (j && j->type() == eObject) {
                JSON_Object *obj = (JSON_Object *)j;
                children.append(new BrowsableQueryResult(obj));
            }
        }
    }
    ~BrowsableQueryResults() { }
};

#endif
