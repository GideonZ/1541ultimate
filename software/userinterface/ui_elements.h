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
#include "event.h"

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
    virtual int  poll(int)
    {
        return -1;
    }
//    virtual int redraw(void);
};

class UIPopup : public UIObject
{
private:
    mstring   message;
    uint8_t buttons;

    uint8_t button_key[NUM_BUTTONS];
    int  btns_active;
    int  active_button;
    int  button_start_x;
    Window  *window;
    Keyboard *keyboard;

    void draw_buttons();

public:
    UIPopup(const char *msg, uint8_t flags);
    ~UIPopup() { }

    void init(Screen *screen, Keyboard *keyb);
    void deinit(void);
    int  poll(int);
};

class UIStringBox : public UIObject
{
private:
    mstring   message;
    Window   *window;
    Keyboard *keyboard;

    int   cur;
    int   len;

    // destination
    int   max_len;
    char *buffer;
public:
    UIStringBox(const char *msg, char *buf, int max);
    ~UIStringBox() { }

    void init(Screen *screen, Keyboard *keyb);
    void deinit(void);
    int  poll(int);
};

class UIStatusBox : public UIObject
{
private:
    mstring  message;
    int      total_steps;
    int      progress;
    Window  *window;
public:
    UIStatusBox(const char *msg, int steps);
    ~UIStatusBox() { }

    void init(Screen *screen);
    void deinit(void);
    void update(const char *msg, int steps);
};


#endif /* USERINTERFACE_UI_ELEMENTS_H_ */
