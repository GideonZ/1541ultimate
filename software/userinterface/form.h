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
                    children.append(new BrowsableQueryField((*keys)[i], val));
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
                if (name[0] != '$') {
                    json->set(name, value);
                }
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

#endif
