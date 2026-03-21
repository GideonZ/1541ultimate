/*
 * ui_elements.h
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_UI_ELEMENTS_H_
#define USERINTERFACE_UI_ELEMENTS_H_

#include <stdint.h>
#include "screen.h"
#include "keyboard.h"
#include "mystring.h"

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

class UserInterface;

class UIObject
{
    bool autoCleanup;
    UserInterface *ui;
public:
    UIObject(UserInterface *ui) { autoCleanup = false; this->ui = ui; }
    void setCleanup() { autoCleanup = true; }
    bool needCleanup() { return autoCleanup; }
    UserInterface *get_ui() { return ui; }
    
    virtual ~UIObject() { }

    virtual void init() { }
    virtual void redraw(void) { }
    virtual void deinit(void) { }
    virtual int  poll(int)
    {
        return -1;
    }
    virtual int  poll_inactive(void)
    {
        return 0;
    }
    virtual void send_keystroke(int key) { }
};

class UIPopup : public UIObject
{
private:
    mstring   message;
    uint8_t buttons;

    uint8_t button_key[8];
    int  btns_active;
    int  active_button;
    int  button_start_x;
    Window  *window;
    Keyboard *keyboard;
    const char **button_names;
    const char *button_keys;
    const int button_count; 

    void draw_buttons();

public:
    UIPopup(UserInterface *ui, const char *msg, uint8_t flags, int count, const char **names, const char *keys);
    ~UIPopup() { }

    void init();
    void deinit(void);
    int  poll(int);
};

class UIStringEdit
{
private:
    Window   *window;
    Keyboard *keyboard;
    int   win_xoffs, win_yoffs;

    int   cur;
    int   len;
    int   max_chars; // maximum number of chars shown at once
    int   edit_offs; // offset in string being edited (scrolling)

    // destination
    int   max_len;
    char *buffer;
public:
    UIStringEdit(char *buf, int max);
    ~UIStringEdit() { }

    void init(Window *win, Keyboard *keyb, int x_offs, int y_offs, int max_chars);
    int  poll(int);
    int  get_max_len() { return max_len; }
};

class UIStringBox : public UIObject
{
private:
    mstring message;
    UIStringEdit edit;
    Window *window;
public:
    UIStringBox(UserInterface *ui, const char *msg, char *buf, int max);
    ~UIStringBox() { }

    void init();
    void deinit(void);
    int  poll(int a) { return edit.poll(a); }
};

class UIStatusBox : public UIObject
{
private:
    mstring  message;
    int      total_steps;
    int      progress;
    Window  *window;
public:
    UIStatusBox(UserInterface *ui, const char *msg, int steps);
    ~UIStatusBox() { }

    void init();
    void deinit(void);
    void update(const char *msg, int steps);
};

class UIChoiceBox : public UIObject
{
private:
    mstring  message;
    const char **choices;
    int      count;
    int      current;
    Window  *window;
    Keyboard *keyboard;
public:
    UIChoiceBox(UserInterface *ui, const char *msg, const char **choices, int count);
    ~UIChoiceBox() { }

    void init();
    void deinit(void);
    int  poll(int);
    void redraw(void);
};


#endif /* USERINTERFACE_UI_ELEMENTS_H_ */
