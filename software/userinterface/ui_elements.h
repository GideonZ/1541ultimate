/*
 * ui_elements.h
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_UI_ELEMENTS_H_
#define USERINTERFACE_UI_ELEMENTS_H_

#include "screen.h"
#include "keyboard.h"
#include "mystring.h"

#define NUM_BUTTONS 5

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

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

    BYTE button_key[NUM_BUTTONS];
    int  btns_active;
    int  active_button;
    int  button_start_x;
    Window  *window;
    Keyboard *keyboard;

    void draw_buttons();

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
    Window   *window;
    Keyboard *keyboard;

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

class UIStatusBox : public UIObject
{
private:
    string   message;
    int      total_steps;
    int      progress;
    Window  *window;
public:
    UIStatusBox(char *msg, int steps);
    ~UIStatusBox() { }

    void init(Screen *screen);
    void deinit(void);
    void update(char *msg, int steps);
};


#endif /* USERINTERFACE_UI_ELEMENTS_H_ */
