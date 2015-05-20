#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <stdlib.h>
#include <string.h>
#include "itu.h"
#include "integer.h"
#include "screen.h"
#include "host.h"
#include "keyboard.h"
#include "path.h"
#include "poll.h"
#include "event.h"
#include "versions.h"
#include "config.h"
#include "ui_elements.h"
#include "editor.h"

#define MAX_SEARCH_LEN 32
#define MAX_UI_OBJECTS  8


typedef enum {
    ui_idle,
    ui_host_owned,
    ui_host_permanent
} t_ui_state;


class UserInterface : public ConfigurableObject
{
private:
    /* Configurable Object */
    void effectuate_settings(void);

    bool initialized;
    t_ui_state state;

    UIObject *ui_objects[MAX_UI_OBJECTS];
    
    void set_screen_title(void);
    CachedTreeNode *current_path;
    UIStatusBox *status_box;
public:
    int color_border, color_bg, color_fg, color_sel;

    GenericHost *host;
    Keyboard *keyboard;
    Screen *screen;
    int     focus;

    UserInterface();
    virtual ~UserInterface();

    virtual bool is_available(void);
    virtual void handle_event(Event &e);
    virtual int  popup(char *msg, uint8_t flags); // blocking
    virtual int  string_box(char *msg, char *buffer, int maxlen); // blocking

    virtual void show_progress(char *msg, int steps); // not blocking
    virtual void update_progress(char *msg, int steps); // not blocking
    virtual void hide_progress(void); // not blocking (of course)

    void init(GenericHost *h);
    void set_screen(Screen *s); /* Only used in updater */
    int  activate_uiobject(UIObject *obj);
        
    void run_editor(char *);

//    UIObject *get_current_ui_object(void) { return ui_objects[focus]; }
    UIObject *get_root_object(void) { return ui_objects[0]; }
};

void poll_user_interface(Event &e);
extern UserInterface *user_interface;

#endif
