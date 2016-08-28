#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <stdlib.h>
#include <string.h>
#include "itu.h"
#include "integer.h"
#include "screen.h"
#include "host.h"
#include "keyboard.h"
#include "versions.h"
#include "config.h"
#include "ui_elements.h"
#include "editor.h"

#define MAX_UI_OBJECTS  8


typedef enum {
    ui_idle,
    ui_host_owned,
    ui_host_permanent
} t_ui_state;


class UserInterface : public ConfigurableObject, HostClient
{
private:
    /* Configurable Object */
    void effectuate_settings(void);

    bool initialized;
    mstring title;
    t_ui_state state;

    UIObject *ui_objects[MAX_UI_OBJECTS];
    
    void set_screen_title(void);
    bool pollFocussed(void);
    UIStatusBox *status_box;
public:
    int color_border, color_bg, color_fg, color_sel;

    GenericHost *host;
    Keyboard *keyboard;
    Screen *screen;
    int     focus;

    UserInterface(const char *title);
    virtual ~UserInterface();

    // from HostClient
    virtual void release_host();

    virtual bool is_available(void);
    virtual void run();
    virtual int  popup(const char *msg, uint8_t flags); // blocking
    virtual int  string_box(const char *msg, char *buffer, int maxlen); // blocking

    virtual void show_progress(const char *msg, int steps); // not blocking
    virtual void update_progress(const char *msg, int steps); // not blocking
    virtual void hide_progress(void); // not blocking (of course)

    virtual int enterSelection(void);

    void init(GenericHost *h);
    void appear(void);
    void set_screen(Screen *s); /* Only used in updater */
    int  activate_uiobject(UIObject *obj);
        
    void run_editor(const char *);

    UIObject *get_root_object(void) { return ui_objects[0]; }
};

#endif
