#ifndef FORM_UI_H
#define FORM_UI_H

#include "tree_browser.h"
#include "tree_browser_state.h"
#include "browsable_field.h"
#include "context_menu.h"
#include "subsys.h"
#include "json.h"
#include "action.h"

class BrowsableForm : public Browsable
{
    const char *title;
    JSON_Object *json;
public:
    BrowsableForm(const char *title, JSON_Object *json) : title(title), json(json) {}
    virtual ~BrowsableForm() {}

	IndexedList<Browsable *> *getSubItems(int &error) {
        if (children.get_elements() == 0) {
            children.append(new BrowsableHeader(title, 0));
            children.append(new BrowsableStatic(""));
            if (json) {
                IndexedList<const char *> *keys = json->get_keys();
                IndexedList<JSON *> *values = json->get_values();
                for(int i=0;i < keys->get_elements(); i++) {
                    JSON *val = (*values)[i];
                    BrowsableQueryField *field;
                    if (val->type() == eList) {
                        field = new BrowsableQueryField((*keys)[i], (JSON_List *)val);
                    } else if(val->type() == eString) {
                        field = new BrowsableQueryField((*keys)[i], NULL);
                        field->setStringValue(((JSON_String *)val)->get_string());
                    }
                    children.append(field);
                }
            }            
            children.append(new BrowsableStatic(""));
            children.append(new BrowsableQueryField("$", NULL));
        }
        return &children;
    }

    void read_fields(void) {
        for(int i=0;i<children.get_elements();i++) {
            Browsable *b = children[i];
            if (b->isSelectable()) {
                BrowsableQueryField *field = (BrowsableQueryField *)b;
                const char *name = field->getName();
                const char *value = field->getStringValue();
                json->set(name, value);
            }
        }
    }
};

class FormUIState: public TreeBrowserState
{
    void submit(void) {
        printf("Klaar!\n");
    }
public:
    FormUIState(Browsable *node, TreeBrowser *tb, int level);
    ~FormUIState();

    void into(void) { printf("Search Form Into\n"); }
    void level_up(void) { printf("Search Form Up\n"); };
    //void change(void);
    void increase(void);
    void decrease(void);
    void clear_entry(void);
    int  edit(void);
};

class FormUI: public TreeBrowser
{
    int size_x, size_y;
    BrowsableForm form;
public:
    FormUI(UserInterface *ui, int, int, const char *title, JSON_Object *fields);
    virtual ~FormUI();

    void init(Screen *screen, Keyboard *k);
    int handle_key(int);
    void checkFileManagerEvent(void)
    {
    } // we are not listening to file manager events

    friend class FormUIState;
};


    // static SubsysResultCode_e new_search(SubsysCommand *cmd)
    // {
    //     Browsable *root = (Browsable *)cmd->functionID;
    //     FormUI *searchBrowser = new FormUI(cmd->user_interface, root);
    //     searchBrowser->init(cmd->user_interface->screen, cmd->user_interface->keyboard);
    //     cmd->user_interface->activate_uiobject(searchBrowser);
    //     // from this moment on, we loose focus.. polls will go directly to config menu!
    //     return SSRET_OK;
    // }

    //     FormUI *search_window = new FormUI(cmd_ui, assembly_gui.getRoot());
    //     search_window->init(cmd_ui->screen, cmd_ui->keyboard);
    //     search_window->setCleanup();
    //     cmd_ui->activate_uiobject(search_window); // now we have focus
    //     return SSRET_OK;



#endif
