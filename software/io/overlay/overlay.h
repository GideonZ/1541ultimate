#ifndef OVERLAY_H
#define OVERLAY_H

extern "C" {
	#include "itu.h"
}

#include "integer.h"
#include "host.h"
#include "keyboard.h"

#define CHARGEN_LINE_CLOCKS_HI   *((volatile BYTE *)0x40E0000)
#define CHARGEN_LINE_CLOCKS_LO   *((volatile BYTE *)0x40E0001)
#define CHARGEN_CHAR_WIDTH       *((volatile BYTE *)0x40E0002)
#define CHARGEN_CHAR_HEIGHT      *((volatile BYTE *)0x40E0003)
#define CHARGEN_CHARS_PER_LINE   *((volatile BYTE *)0x40E0004)
#define CHARGEN_ACTIVE_LINES     *((volatile BYTE *)0x40E0005)
#define CHARGEN_X_ON_HI          *((volatile BYTE *)0x40E0006)
#define CHARGEN_X_ON_LO          *((volatile BYTE *)0x40E0007)
#define CHARGEN_Y_ON_HI          *((volatile BYTE *)0x40E0008)
#define CHARGEN_Y_ON_LO          *((volatile BYTE *)0x40E0009)
#define CHARGEN_POINTER_HI       *((volatile BYTE *)0x40E000A)
#define CHARGEN_POINTER_LO       *((volatile BYTE *)0x40E000B)
#define CHARGEN_PERFORM_SYNC     *((volatile BYTE *)0x40E000C)
#define CHARGEN_TRANSPARENCY     *((volatile BYTE *)0x40E000D)
#define CHARGEN_KEYB_ROW         *((volatile BYTE *)0x40E000E)
#define CHARGEN_KEYB_COL         *((volatile BYTE *)0x40E000F)


#define CHARGEN_SCREEN_RAM       (0x40E0800)
#define CHARGEN_COLOR_RAM        (0x40E1000)

// This is a temporary fix, until there is a better build-up
// of objects. In fact the overlay unit should be a variant of
// an output device, but at this point there is no such base.

class Overlay : public GenericHost
{
    Keyboard *keyb;
public:
    Overlay(bool active) {
        keyb = NULL;
        
        if(CAPABILITIES & CAPAB_OVERLAY) {
            CHARGEN_CHAR_WIDTH       = 8;
            CHARGEN_CHAR_HEIGHT      = 9;
            CHARGEN_CHARS_PER_LINE   = 40;
            CHARGEN_ACTIVE_LINES     = 25;
            CHARGEN_X_ON_HI          = 0;
            CHARGEN_X_ON_LO          = 100;
            CHARGEN_Y_ON_HI          = 0;
            CHARGEN_Y_ON_LO          = 150;
            CHARGEN_POINTER_HI       = 0;
            CHARGEN_POINTER_LO       = 0;
            CHARGEN_PERFORM_SYNC     = 0;
            CHARGEN_TRANSPARENCY     = (active)?0x84:0x04;

            keyb = new Keyboard(this, &CHARGEN_KEYB_ROW, &CHARGEN_KEYB_COL);
        }
    }
    ~Overlay() {
        if(keyb)
            delete keyb;
    }

    bool exists(void) {
        if(CAPABILITIES & CAPAB_OVERLAY) {
            return true;
        } else {
            return false;
        }
    }
    
    bool is_accessible(void) {
        return true;
    }
    
    void poll(Event &e) { }
    void reset(void) { }
    void freeze(void) {
        CHARGEN_TRANSPARENCY = 0x86;
    }
    void unfreeze(Event &e) {
        CHARGEN_TRANSPARENCY = 0x06;
    }

    char *get_screen(void) { return (char *)CHARGEN_SCREEN_RAM; }
    char *get_color_map(void) { return (char *)CHARGEN_COLOR_RAM; }

    /* We should actually just return an input device type */
    Keyboard *get_keyboard(void) {
        return keyb;
    }
};

#endif
