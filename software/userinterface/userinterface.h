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

                        // Values > 0 are valid choices from the user and need to be passed to the
                        // underlying object.
#define MENU_NOP     0  // Stay in current window
#define MENU_CLOSE  -1  // Window operation is complete and can be closed
#define MENU_HIDE   -2  // The selected action requests the menu to hide. (No effect for remote connection)
#define MENU_EXIT   -3  // The selected action requests the menu to exit and be destroyed.
                        // For hosted menus this means back to level 0 and hidden
                        // For the remote connection it means close connection

#define MAX_UI_OBJECTS  8

//#define CFG_USERIF_BACKGROUND  0x01
//#define CFG_USERIF_BORDER      0x02
//#define CFG_USERIF_FOREGROUND  0x03
//#define CFG_USERIF_SELECTED    0x04

#define CFG_USERIF_WORDWRAP    0x05
#define CFG_USERIF_START_HOME  0x06
#define CFG_USERIF_HOME_DIR    0x07
#define CFG_USERIF_ITYPE       0x08
#define CFG_USERIF_SELECTED_BG 0x09
#define CFG_USERIF_CFG_SAVE    0x0A
#define CFG_USERIF_ULTICOPY_NAME 0x0B
#define CFG_USERIF_FILENAME_OVERFLOW_SQUEEZE 0x0C
#define CFG_USERIF_NAVIGATION  0x0D
#define CFG_USERIF_COLORSCHEME 0x0E

class UserInterface : public ConfigurableObject, public HostClient
{
private:
    /* Configurable Object */
    void effectuate_settings(void);

    bool initialized;
    bool doBreak;
    bool available;
    mstring title;
    UIObject *ui_objects[MAX_UI_OBJECTS];
    UIStatusBox *status_box;
    
    void set_screen_title(void);
    int  pollFocussed(void);
    void peel_off(void);
    bool buttonDownFor(uint32_t ms);
public:
    int color_border, color_bg, color_fg, color_sel, color_sel_bg, config_save, filename_overflow_squeeze, navmode;
    bool logo;
    GenericHost *host;
    Keyboard *keyboard;
    Screen *screen;
    int     focus;
    int     menu_response_to_action;

    UserInterface(const char *title, bool logo);
    virtual ~UserInterface();

    // from HostClient
    virtual void release_host();

    virtual bool is_available(void);
    virtual void run();
    virtual void run_once();
    virtual void run_remote();
    virtual int  pollInactive();
    virtual int  popup(const char *msg, uint8_t flags); // blocking
    virtual int  popup(const char *msg, int count, const char **names, const char *keys); // blocking, custom
    virtual int  choice(const char *msg, const char **choices, int count);
    virtual int  string_box(const char *msg, char *buffer, int maxlen); // blocking
    virtual int  string_edit(char *buffer, int maxlen, Window *w, int x, int y);
    virtual void show_progress(const char *msg, int steps); // not blocking
    virtual void update_progress(const char *msg, int steps); // not blocking
    virtual void hide_progress(void); // not blocking (of course)

    void init(GenericHost *h);
    void appear(void);
    void set_screen(Screen *s); /* Only used in updater */
    Screen *get_screen() { return screen; }
    Keyboard *get_keyboard() { return keyboard; }
    
    int  activate_uiobject(UIObject *obj);
    bool has_focus(UIObject *obj);
    int  getPreferredType(void);
    void run_editor(const char *, int);
    void swapDisk(void);
    void send_keystroke(int key);

    UIObject *get_root_object(void) { return ui_objects[0]; }

    static void postMessage(const char *msg);
    mstring *getMessage();

    friend class HomeDirectory;
};

#endif
