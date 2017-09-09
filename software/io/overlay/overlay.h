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

#define CHARGEN_LINE_CLOCKS_HI   *((volatile uint8_t *)(OVERLAY_BASE + 0x00))
#define CHARGEN_LINE_CLOCKS_LO   *((volatile uint8_t *)(OVERLAY_BASE + 0x01))
#define CHARGEN_CHAR_WIDTH       *((volatile uint8_t *)(OVERLAY_BASE + 0x02))
#define CHARGEN_CHAR_HEIGHT      *((volatile uint8_t *)(OVERLAY_BASE + 0x03))
#define CHARGEN_CHARS_PER_LINE   *((volatile uint8_t *)(OVERLAY_BASE + 0x04))
#define CHARGEN_ACTIVE_LINES     *((volatile uint8_t *)(OVERLAY_BASE + 0x05))
#define CHARGEN_X_ON_HI          *((volatile uint8_t *)(OVERLAY_BASE + 0x06))
#define CHARGEN_X_ON_LO          *((volatile uint8_t *)(OVERLAY_BASE + 0x07))
#define CHARGEN_Y_ON_HI          *((volatile uint8_t *)(OVERLAY_BASE + 0x08))
#define CHARGEN_Y_ON_LO          *((volatile uint8_t *)(OVERLAY_BASE + 0x09))
#define CHARGEN_POINTER_HI       *((volatile uint8_t *)(OVERLAY_BASE + 0x0A))
#define CHARGEN_POINTER_LO       *((volatile uint8_t *)(OVERLAY_BASE + 0x0B))
#define CHARGEN_PERFORM_SYNC     *((volatile uint8_t *)(OVERLAY_BASE + 0x0C))
#define CHARGEN_TRANSPARENCY     *((volatile uint8_t *)(OVERLAY_BASE + 0x0D))
#define CHARGEN_KEYB_ROW         *((volatile uint8_t *)(OVERLAY_BASE + 0x0E))
#define CHARGEN_KEYB_COL         *((volatile uint8_t *)(OVERLAY_BASE + 0x0F))


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
public:
    Overlay(bool active) {
        keyb = NULL;
        enabled = active;
        buttonPushSeen = false;
        
        if(getFpgaCapabilities() & CAPAB_OVERLAY) {
            CHARGEN_CHAR_WIDTH       = 8;
            CHARGEN_CHAR_HEIGHT      = 9;
            CHARGEN_CHARS_PER_LINE   = 40;
            CHARGEN_ACTIVE_LINES     = 25;
            CHARGEN_X_ON_HI          = 1;
            CHARGEN_X_ON_LO          = 120;
            CHARGEN_Y_ON_HI          = 1;
            CHARGEN_Y_ON_LO          = 90;
            CHARGEN_POINTER_HI       = 0;
            CHARGEN_POINTER_LO       = 0;
            CHARGEN_PERFORM_SYNC     = 0;

            keyb = new Keyboard_C64(this, &CHARGEN_KEYB_ROW, &CHARGEN_KEYB_COL);
            screen = new Screen_MemMappedCharMatrix((char *)CHARGEN_SCREEN_RAM, (char *)CHARGEN_COLOR_RAM, 40, 25);

            if (enabled) {
            	take_ownership(0);
            } else {
            	release_ownership();
            }
        }
    }
    ~Overlay() {
        if(keyb)
            delete keyb;
        if(screen)
        	delete screen;
    }

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
        CHARGEN_TRANSPARENCY = 0x80;
        enabled = true;
        system_usb_keyboard.enableMatrix(false);
    }

    void release_ownership() {
        CHARGEN_TRANSPARENCY = 0x01;
        enabled = false;
        system_usb_keyboard.enableMatrix(true);
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
};

#endif
