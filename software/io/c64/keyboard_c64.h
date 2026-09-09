#ifndef KEYBOARD_C64_H
#define KEYBOARD_C64_H

#include "keyboard.h"
#include "host.h"

// A cartridge reads the keyboard off the host computer's CIA through the
// expansion port, and a key tapped into that matrix by the computer itself
// (machine:input on a C64 Ultimate) is down for about 40 ms with a 20 ms
// release. The scan used to run from the user interface task's getch(), which
// is away for 40 to 115 ms whenever it stops the machine to read memory or
// redraw, so such a tap was missed outright or, with the release unseen, read
// as the previous key still held. On those builds the scan runs from a timer
// instead, at this period in ticks, and getch() only drains the buffer.
#if !U64 && !RECOVERYAPP && !defined(NO_FILE_ACCESS)
#define KEYBOARD_C64_TIMER_SCAN 1
#define KEYBOARD_C64_SCAN_PERIOD_TICKS 2
#else
#define KEYBOARD_C64_TIMER_SCAN 0
#endif

#define KEY_BUFFER_SIZE 16

class GenericHost;

class Keyboard_C64 : public Keyboard
{
    GenericHost *host;
    volatile uint8_t *row_register;
    volatile uint8_t *col_register;
    volatile uint8_t *joy_register;

    uint8_t shift_prev;
    uint8_t mtrx_prev;

    int  repeat_speed;
    int  first_delay;

    int  delay_count;

    int key_buffer[KEY_BUFFER_SIZE];
    volatile int  key_head;
    volatile int  key_tail;
    // Held by the user interface task while it drives the CIA itself
    // (wait_free), so the timer scan keeps its hands off the column select.
    volatile int  scan_paused;
    void *scan_timer;
    static void scan_timer_callback(void *timer);
public:
    Keyboard_C64(GenericHost *, volatile uint8_t *r, volatile uint8_t *c, volatile uint8_t *j);
    ~Keyboard_C64();

    static uint8_t scan_keyboard(volatile uint8_t *r, volatile uint8_t *c);
    static bool joystick_blocks_keyboard(uint8_t observed_active_low, uint8_t injected_active_low);
    static uint8_t matrixModifierFlag(uint8_t row, uint8_t col);
    static uint8_t matrixToKeyCode(uint8_t row, uint8_t col, uint8_t shift_flag);

    void scan(void);
    bool scans_from_timer(void) const { return scan_timer != 0; }
    void set_delays(int, int);
    int  getch(void);
    void push_head(int);
    void wait_free(void);
    void clear_buffer(void);
};

#endif
