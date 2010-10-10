#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <stdlib.h>
#include <string.h>
#include "integer.h"
#include "screen.h"
#include "c64.h"
#include "keyboard.h"
#include "path.h"
#include "poll.h"
#include "event.h"
#include "versions.h"


#define NUM_BUTTONS 5

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

#define MAX_SEARCH_LEN 32
#define MAX_UI_OBJECTS  8

//#define RES_BUSY    0
//#define RES_CANCEL -1

typedef enum {
    ui_idle,
    ui_host_owned
} t_ui_state;

class UIObject;

class UserInterface
{
private:
    bool initialized;
    t_ui_state state;

    UIObject *ui_objects[MAX_UI_OBJECTS];
    
    void set_screen_title(void);
    PathObject *current_path;
    
public:
    C64 *host;
    Keyboard *keyboard;
    Screen *screen;
    int     focus;

    UserInterface();
    virtual ~UserInterface();

    virtual void handle_event(Event &e);
    virtual int  popup(char *msg, BYTE flags); // blocking
    virtual int  string_box(char *msg, char *buffer, int maxlen); // blocking
    
    // interface to find the current path from any object, they can ask the user interface
    // This is intended for menu options that do not pass the path object.
    void set_path(PathObject *po) { current_path = po; /*printf("Set current path to %s\n", po->get_name());*/  }
    PathObject *get_path(void)    { return current_path; }
    // end workaround
    
    void init(C64 *h, Keyboard *k);
    void set_screen(Screen *s); /* Only used in updater */
    int  activate_uiobject(UIObject *obj);
        
//    UIObject *get_current_ui_object(void) { return ui_objects[focus]; }
    UIObject *get_root_object(void) { return ui_objects[0]; }
};

class UIObject
{
public:
    UIObject() { }
    virtual ~UIObject() { } 
    
    virtual void init(Screen *scr, Keyboard *key) { }
    virtual void deinit(void) { }
    virtual int  poll(int, Event &e)
    {
        return -1;
    }
//    virtual int redraw(void);
};

class UIPopup : public UIObject
{
private:
    string   message;
    BYTE buttons;

    int  button_len[NUM_BUTTONS];
    int  button_pos[NUM_BUTTONS];
    char button_key[NUM_BUTTONS];
    int  btns_active;
    int  active_button;
    Screen  *window;
    Keyboard *keyboard;
public:
    UIPopup(char *msg, BYTE flags);
    ~UIPopup() { }
    
    void init(Screen *screen, Keyboard *keyb);
    void deinit(void);
    int  poll(int, Event &e);
};
        
class UIStringBox : public UIObject
{
private:
    string    message;
    Screen   *window;
    Keyboard *keyboard;

    char *scr;
    int   cur;
    int   len;

    // destination
    int   max_len;
    char *buffer;
public:    
    UIStringBox(char *msg, char *buf, int max);
    ~UIStringBox() { }

    void init(Screen *screen, Keyboard *keyb);
    void deinit(void);
    int  poll(int, Event &e);
};


void poll_user_interface(Event &e);
extern UserInterface *user_interface;

#endif
