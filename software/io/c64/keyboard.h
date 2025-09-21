#ifndef KEYBOARD_H
#define KEYBOARD_H

class Keyboard
{

public:
    Keyboard() { }
    virtual ~Keyboard() { }
    
    virtual int  getch(void) { return -1; }
    virtual void push_head(int) { }
    virtual void wait_free(void) { }
    virtual void clear_buffer(void) { }
};

#define KEY_BACK   0x08
#define KEY_TAB    0x09
#define KEY_RETURN 0x0D
#define KEY_BREAK  0x11
#define KEY_DOWN   0x12
#define KEY_HOME   0x13
#define KEY_ESCAPE 0x1B
#define KEY_RIGHT  0x1D
#define KEY_SPACE  0x20
#define KEY_SHIFT_SP 0xA0

#define KEY_CTRL_HOME 0x14

#define KEY_CTRL_A 0x01
#define KEY_CTRL_C 0x03
#define KEY_CTRL_N 0x0E
#define KEY_CTRL_V 0x16
#define KEY_CTRL_J 0x0A
#define KEY_CTRL_L 0x0C

#define KEY_A      0x61
#define KEY_S      0x73
#define KEY_D      0x64
#define KEY_W      0x77

#define KEY_F1     0x85
#define KEY_F3     0x86
#define KEY_F5     0x87
#define KEY_F7     0x88
#define KEY_F2     0x89
#define KEY_F4     0x8A
#define KEY_F6     0x8B
#define KEY_F8     0x8C
#define KEY_F9     0x8D
#define KEY_F10    0x8E
#define KEY_F11	   0x8F
#define KEY_F12    0x90

#define KEY_UP       0x91
#define KEY_CLEAR	 0x93
#define KEY_INSERT   0x94
#define KEY_LEFT     0x9D

#define KEY_PAGEUP   0x92
#define KEY_PAGEDOWN 0x95
#define KEY_END		 0x96
#define KEY_DELETE   0x7F

#define KEY_ERR      0xFF
#define KEY_CAPS     0xA1
#define KEY_PRSCR    0xA2
#define KEY_SCRLOCK  0xA3
#define KEY_NUMLOCK  0xA4

// Specials
#define KEY_MENU     0x1FE
#define KEY_SEARCH   0x1FD
#define KEY_TASKS    0x1FC
#define KEY_HELP     0x1FB
#define KEY_CONFIG   0x1FA
#define KEY_SYSINFO  0x1F9
#endif
