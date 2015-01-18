#ifndef OVERLAY_H
#define OVERLAY_H

extern "C" {
	#include "itu.h"
}

#include "integer.h"
#include "host.h"
#include "keyboard.h"
#include "iomap.h"

#define CHARGEN_LINE_CLOCKS_HI   *((volatile BYTE *)(OVERLAY_BASE + 0x00))
#define CHARGEN_LINE_CLOCKS_LO   *((volatile BYTE *)(OVERLAY_BASE + 0x01))
#define CHARGEN_CHAR_WIDTH       *((volatile BYTE *)(OVERLAY_BASE + 0x02))
#define CHARGEN_CHAR_HEIGHT      *((volatile BYTE *)(OVERLAY_BASE + 0x03))
#define CHARGEN_CHARS_PER_LINE   *((volatile BYTE *)(OVERLAY_BASE + 0x04))
#define CHARGEN_ACTIVE_LINES     *((volatile BYTE *)(OVERLAY_BASE + 0x05))
#define CHARGEN_X_ON_HI          *((volatile BYTE *)(OVERLAY_BASE + 0x06))
#define CHARGEN_X_ON_LO          *((volatile BYTE *)(OVERLAY_BASE + 0x07))
#define CHARGEN_Y_ON_HI          *((volatile BYTE *)(OVERLAY_BASE + 0x08))
#define CHARGEN_Y_ON_LO          *((volatile BYTE *)(OVERLAY_BASE + 0x09))
#define CHARGEN_POINTER_HI       *((volatile BYTE *)(OVERLAY_BASE + 0x0A))
#define CHARGEN_POINTER_LO       *((volatile BYTE *)(OVERLAY_BASE + 0x0B))
#define CHARGEN_PERFORM_SYNC     *((volatile BYTE *)(OVERLAY_BASE + 0x0C))
#define CHARGEN_TRANSPARENCY     *((volatile BYTE *)(OVERLAY_BASE + 0x0D))
#define CHARGEN_KEYB_ROW         *((volatile BYTE *)(OVERLAY_BASE + 0x0E))
#define CHARGEN_KEYB_COL         *((volatile BYTE *)(OVERLAY_BASE + 0x0F))


#define CHARGEN_SCREEN_RAM       (OVERLAY_BASE + 0x0800)
#define CHARGEN_COLOR_RAM        (OVERLAY_BASE + 0x1000)

// This is a temporary fix, until there is a better build-up
// of objects. In fact the overlay unit should be a variant of
// an output device, but at this point there is no such base.

class Overlay : public GenericHost
{
    Keyboard *keyb;
public:
    Overlay(bool active) {
        keyb = NULL;
        
        if(getFpgaCapabilities() & CAPAB_OVERLAY) {
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
        if(getFpgaCapabilities() & CAPAB_OVERLAY) {
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
