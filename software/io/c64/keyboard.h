#ifndef KEYBOARD_H
#define KEYBOARD_H

class Keyboard
{

public:
    Keyboard() { }
    virtual ~Keyboard() { }
    
    virtual int  getch(void) { return -1; }
    virtual void wait_free(void) { }
    virtual void clear_buffer(void) { }
};

#define KEY_DOWN  0x11
#define KEY_HOME  0x13
#define KEY_BACK  0x14
#define KEY_RIGHT 0x1D
#define KEY_F1    0x85
#define KEY_F3    0x86
#define KEY_F5    0x87
#define KEY_F7    0x88
#define KEY_F2    0x89
#define KEY_F4    0x8A
#define KEY_F6    0x8B
#define KEY_F8    0x8C
#define KEY_F9    0x8D
#define KEY_F10   0x8E
#define KEY_F11	  0x8F
#define KEY_F12   0x90

#define KEY_UP       0x91
#define KEY_CLEAR	 0x93
#define KEY_INSERT   0x94
#define KEY_LEFT     0x9D

#define KEY_PAGEUP   0x92
#define KEY_PAGEDOWN 0x95
#define KEY_END		 0x96
#define KEY_INS		 0x94

#endif
