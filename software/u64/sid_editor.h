/*
 * sid_editor.h
 *
 *  Created on: Sep 1, 2019
 *      Author: gideon
 */

#ifndef SID_EDITOR_H_
#define SID_EDITOR_H_

#include "userinterface.h"
#include "ui_elements.h"
#include "config.h"

class SidEditor: public UIObject {
    ConfigStore *cfg;
    ConfigItem *address_item[4];
    ConfigItem *split_item[4];
    ConfigItem *mirror_item;

    UserInterface *user_interface;
    Screen   *screen;
    Window   *window;
    Keyboard *keyb;

    int edit; // which SID we are editing

    // private functions:
    virtual int handle_key(int c);
    void draw_edit(void);

public:
    SidEditor(UserInterface *intf, ConfigStore *cfg);
    virtual ~SidEditor();

    virtual void init();
    virtual void deinit(void);
    virtual int poll(int);
    virtual void draw();
    void redraw(void);
};

#endif /* SID_EDITOR_H_ */
