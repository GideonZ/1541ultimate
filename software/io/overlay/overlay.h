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
#include "u64.h"

// This is a temporary fix, until there is a better build-up
// of objects. In fact the overlay unit should be a variant of
// an output device, but at this point there is no such base.
#define OVERLAY_CHARHEIGHT_8    0x08
#define OVERLAY_CHARHEIGHT_9    0x09
#define OVERLAY_CHARHEIGHT_16   0x90
#define OVERLAY_CHARHEIGHT_17   0x91
#define OVERLAY_CHARHEIGHT_24   0x5E

typedef struct
{
    int activeX;
    int activeY;
    int X_on;
    int Y_on;
    uint8_t charWidth;
    uint8_t charHeight;
} overlay_settings_t;

class Overlay : public GenericHost
{
    volatile t_chargen_registers *overlay_regs;
    char *charmap, *colormap;
    Keyboard *keyb;
    Screen *screen;
    bool enabled;
    bool buttonPushSeen;
    int activeX;
    int activeY;
    int X_on;
    int Y_on; //346

public:
    Overlay(bool active, int addrbits, uint32_t base, overlay_settings_t &initial_settings)
    {
        overlay_regs = (volatile t_chargen_registers *)base;
        charmap = (char *)(base + (1 << addrbits));
        colormap = (char *)(base + (2 << addrbits));
        keyb = NULL;
        enabled = active;
        buttonPushSeen = false;
        
        update_settings(initial_settings);
        screen = new Screen_MemMappedCharMatrix(charmap, colormap, activeX, activeY);

        if (enabled) {
            take_ownership(0);
        } else {
            release_ownership();
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
        return true;
    }
    
    bool is_accessible(void) {
        return enabled;
    }

    bool is_permanent(void) {
    	return true;
    }
    
    void take_ownership(HostClient *h) {
        overlay_regs->TRANSPARENCY = 0x80;
        enabled = true;
        //system_usb_keyboard.enableMatrix(false);
    }

    void release_ownership() {
        overlay_regs->TRANSPARENCY = 0x00;
        // THIS IS A HACK... This is not part of the overlay function, but it is the best place to perform this function
        // to make the 'gap' the smallest possible between a write to the CIA and one from the system CPU.
        *C64_PLD_PORTA = C64_PLD_STATE0;
        *C64_PLD_PORTB = C64_PLD_STATE1;
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


    void update_settings(overlay_settings_t &settings)
    {
        activeX = settings.activeX;
        activeY = settings.activeY;
        X_on    = settings.X_on;
        Y_on    = settings.Y_on;

        overlay_regs->CHARS_PER_LINE   = activeX;
        overlay_regs->ACTIVE_LINES     = activeY;
        overlay_regs->X_ON_HI          = X_on >> 8;
        overlay_regs->X_ON_LO          = X_on & 0xFF;
        overlay_regs->Y_ON_HI          = Y_on >> 8;
        overlay_regs->Y_ON_LO          = Y_on & 0xFF;
        overlay_regs->CHAR_WIDTH       = settings.charWidth;
        overlay_regs->CHAR_HEIGHT      = settings.charHeight;
        overlay_regs->POINTER_HI       = 0;
        overlay_regs->POINTER_LO       = 0;
        overlay_regs->PERFORM_SYNC     = 0;
        
    }
};

#endif
