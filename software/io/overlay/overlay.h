#ifndef OVERLAY_H
#define OVERLAY_H

extern "C" {
	#include "itu.h"
}

#include "integer.h"
#include "host.h"
#include "keyboard_c64.h"
#include "keyboard_usb.h"
#include "iomap.h"
#include "chargen.h"

#define OVERLAY_REGS              ((volatile t_chargen_registers *)OVERLAY_BASE)

#define CHARGEN_SCREEN_RAM       (OVERLAY_BASE + 0x0800)
#define CHARGEN_COLOR_RAM        (OVERLAY_BASE + 0x1000)

// This is a temporary fix, until there is a better build-up
// of objects. In fact the overlay unit should be a variant of
// an output device, but at this point there is no such base.

class Overlay : public GenericHost
{
    Keyboard *keyb;
    Screen *screen;
    bool enabled;
    bool buttonPushSeen;
//    uint8_t backgroundColor;

public:
    Overlay(bool active) {
        keyb = NULL;
        enabled = active;
        buttonPushSeen = false;
        
        if(getFpgaCapabilities() & CAPAB_OVERLAY) {
            OVERLAY_REGS->CHAR_WIDTH       = 8;
            OVERLAY_REGS->CHAR_HEIGHT      = 9;
            OVERLAY_REGS->CHARS_PER_LINE   = 40;
            OVERLAY_REGS->ACTIVE_LINES     = 25;
            OVERLAY_REGS->X_ON_HI          = 1;
            OVERLAY_REGS->X_ON_LO          = 110;
            OVERLAY_REGS->Y_ON_HI          = 1;
            OVERLAY_REGS->Y_ON_LO          = 90;
            OVERLAY_REGS->POINTER_HI       = 0;
            OVERLAY_REGS->POINTER_LO       = 0;
            OVERLAY_REGS->PERFORM_SYNC     = 0;

            screen = new Screen_MemMappedCharMatrix((char *)CHARGEN_SCREEN_RAM, (char *)CHARGEN_COLOR_RAM, 40, 25);

            if (enabled) {
            	take_ownership(0);
            } else {
            	release_ownership();
            }
        }
    }
    ~Overlay() {
        if(screen)
        	delete screen;
    }

/*
    void set_colors(int background, int border) {
        this->backgroundColor = background;
    }
*/

    bool exists(void) {
        if(getFpgaCapabilities() & CAPAB_OVERLAY) {
            return true;
        } else {
            return false;
        }
    }
    
    bool is_accessible(void) {
        return enabled;
    }

    bool is_permanent(void) {
    	return true;
    }
    
    void take_ownership(HostClient *h) {
        OVERLAY_REGS->TRANSPARENCY = 0x80;
        enabled = true;
        //system_usb_keyboard.enableMatrix(false);
    }

    void release_ownership() {
        OVERLAY_REGS->TRANSPARENCY = 0x00;
        enabled = false;
        //system_usb_keyboard.enableMatrix(true);
    }

    bool hasButton(void) {
    	return true;
    }

    void checkButton(void) {
    	static uint8_t button_prev;

    	uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    	if((buttons & ~button_prev) & ITU_BUTTON1) {
    		buttonPushSeen = true;
    	}
    	button_prev = buttons;
    }

    bool buttonPush(void)
    {
    	bool ret = buttonPushSeen;
    	buttonPushSeen = false;
    	return ret;
    }

    void setButtonPushed(void)
    {
    	buttonPushSeen = true;
    }

    Screen *getScreen(void) {
    	return screen;
    }

    /* We should actually just return an input device type */
    Keyboard *getKeyboard(void) {
        return keyb;
    }

    void setKeyboard(Keyboard *kb) {
        keyb = kb;
    }
};

#endif
