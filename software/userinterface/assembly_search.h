#ifndef ASSEMBLY_SEARCH_H
#define ASSEMBLY_SEARCH_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "context_menu.h"
#include "subsys.h"
#include "json.h"
#include "menu.h"
#include "action.h"
#include "network_interface.h"

class AssemblySearchForm: public TreeBrowserState
{
    void send_query(void);
public:
    AssemblySearchForm(Browsable *node, TreeBrowser *tb, int level);
    ~AssemblySearchForm();

    void into(void) { printf("Search Form Into\n"); }
    void level_up(void) { printf("Search Form Up\n"); };
    void change(void);
    void increase(void);
    void decrease(void);
    void clear_entry(void);
};

class AssemblyResultsView: public TreeBrowserState
{
    void get_entries(void);
public:
    AssemblyResultsView(Browsable *node, TreeBrowser *tb, int level);
    ~AssemblyResultsView();

    void into(void);
    bool into2(void) { into(); return true; }
//    void level_up(void) { printf("Results Up\n"); };
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
        return SSRET_OK;
    }
public:
    BrowsableQueryField(const char *field, JSON_List *presets) : field(field), presets(presets)
    {
        current_preset = -1;
    }

    ~BrowsableQueryField()
    {
    }

    const char *getName()
    {
        return field;
    }

    void reset()
    {
        current_preset = -1;
        value = "";
    }

    const char *getAqlString()
    {
        if (presets) {
            if (current_preset == -1) {
                return "";
            }
            JSON_Object *obj = (JSON_Object *)(*presets)[current_preset];
            JSON_String *aql = (JSON_String *)obj->get("aqlKey");
            if (aql) {
                return aql->get_string();
            }
        }
        return value.c_str();
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
            sprintf(buffer, "\er           \eR <<  Submit  >> \er");
            return;
        }

        sprintf(buffer, "%s:", field);
        buffer[strlen(buffer)] = ' ';
        if (width < 16) {
            return;
        }

        if (value.length()) {
            // position 10
            sprintf(buffer+10, "\er\eg%#s", width-11, value.c_str());
        } else {
            sprintf(buffer+10, "\er\ek%#s", width-11, "__________________");
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
        return SSRET_OK;
    }
public:
    BrowsableAssemblyRoot()
    {
        presets = NULL;
    }

    ~BrowsableAssemblyRoot()
    {
    }

    void fetchPresets(void);
    bool isInitialized(void)
    {
        return (presets != NULL);
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        // name, group, handle, event, date*, category*, subcat*, rating*, type*, repo*, latest, sort, order
        if (children.get_elements() == 0) {
            children.append(new BrowsableStatic("\em  Assembly 64 Query Form"));
            children.append(new BrowsableStatic(""));
            children.append(new BrowsableQueryField("name", NULL));
            children.append(new BrowsableQueryField("group", NULL));
            children.append(new BrowsableQueryField("handle", NULL));
            children.append(new BrowsableQueryField("event", NULL));
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
    Path path;
public:
    BrowsableQueryResult(JSON_Object *result) : path("/a64")
    {
        JSON *j;
        int year = 0;

        j = result->get("name");
        if (j && j->type() == eString) {
            summary = ((JSON_String *)j)->get_string();
        }
        j = result->get("year");
        if (j && j->type() == eInteger) {
            year = ((JSON_Integer *)j)->get_value();
        }
        j = result->get("group");
        if (j && j->type() == eString) {
            summary += " (";
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
        path.cd(id.c_str());
        char catstr[8];
        sprintf(catstr, "%d", category);
        path.cd(catstr);
    }

    ~BrowsableQueryResult()
    {
    }

    const char *getId() { return id.c_str(); }
    int getCategory() { return category; }

    const char *getName()
    {
        return summary.c_str();
    }

    Path *getPath()
    {
        return &path;
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
    IndexedList<Browsable *>items; // Override from Browsable
public:
    BrowsableQueryResults(JSON_List *results) : items(16, NULL)
    {
        for(int i=0; i<results->get_num_elements(); i++) {
            JSON *j = (*results)[i];
            if (j && j->type() == eObject) {
                JSON_Object *obj = (JSON_Object *)j;
                items.append(new BrowsableQueryResult(obj));
            }
        }
    }
    ~BrowsableQueryResults() { }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        return &items;
    }

};

class AssemblyInGui;
extern AssemblyInGui assembly_gui;

class AssemblyInGui : public ObjectWithMenu
{
    TaskCategory *taskItemCategory;
    BrowsableAssemblyRoot * root;

    static SubsysResultCode_e S_OpenSearch(SubsysCommand *cmd) {
        UserInterface *cmd_ui = cmd->user_interface;
        S_OpenSearch(cmd_ui);
        return SSRET_OK;
    }
   
public:
    AssemblyInGui() {
        root = NULL;
        taskItemCategory = TasksCollection :: getCategory("Assembly 64", SORT_ORDER_ASSEMBLY);
    }

    ~AssemblyInGui() {
        // unregister taskItemCategory
    }

    static void S_OpenSearch(UserInterface *cmd_ui) {

        if (!NetworkInterface :: DoWeHaveLink()) {
            if (cmd_ui)
                cmd_ui->popup("No Valid Network Link", BUTTON_OK);
            return;
        }
        Screen *scr = cmd_ui->screen;
        scr->set_status("Connecting...", cmd_ui->color_status);

        BrowsableAssemblyRoot *root = assembly_gui.getRoot();
        if (!root->isInitialized()) {
            if (cmd_ui)
                cmd_ui->popup("Could not connect.", BUTTON_OK);
            return;
        }

        AssemblySearch *search_window = new AssemblySearch(cmd_ui, assembly_gui.getRoot());
        search_window->init(cmd_ui->screen, cmd_ui->keyboard);
        search_window->setCleanup();
        cmd_ui->activate_uiobject(search_window); // now we have focus
    }

    BrowsableAssemblyRoot *getRoot()
    {
        if (!root) {
            root = new BrowsableAssemblyRoot();
        }
        if (!root->isInitialized()) {
            root->fetchPresets();
        }
        return root;
    }

    void create_task_items(void) {
        taskItemCategory->append(new Action("New Search", S_OpenSearch, 0, 0));
    }
};

#endif
