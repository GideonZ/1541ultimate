#ifndef MOUSE_POT_PACER_H
#define MOUSE_POT_PACER_H

#include "integer.h"

// Paces a 1351 mouse position onto the POT lines of a control port.
//
// A 1351 driver reads the POT values once per frame and takes the change of
// each 7-bit value as a move of -64 to +63 counts. A USB mouse report moves up
// to 63 counts per axis, and the wheel can add as much again. Reports come
// every 20ms, but the firmware handles each one after a varying delay, so two
// of them can land inside one frame. Their sum is then more than 63 counts,
// which the driver reads as a move the other way.
//
// The pacer takes the running position the mouse has reached and moves the
// position on the lines (shown) after it, within a budget per axis: the changes
// made within `window` milliseconds of each other add up to at most MAX_CHANGE
// counts. Two changes that do not count together are at least `window` apart
// on the millisecond clock, so more than `window - 1` ms apart in time. That
// has to cover a frame, plus the 512 cycles (0.52ms) the SID takes to measure
// a POT line, plus 0.5ms for the driver's interrupt: WINDOW_50HZ for 19.95ms
// frames and WINDOW_60HZ for 16.72ms frames. No frame then sees more than
// MAX_CHANGE counts.
//
// Movement inside the budget goes out at once, exactly as it arrives; at 60Hz
// that includes 63 counts every 20ms when reports are handled at least 19ms
// apart. Movement over the budget waits until
// advance() is called with room in the budget. At most MAX_BACKLOG counts per
// axis wait: movement faster than the POT lines can carry is dropped beyond
// that, so the pointer stops soon after the mouse does.
class MousePotPacer
{
public:
    enum {
        MAX_CHANGE = 63,
        MAX_BACKLOG = 4 * MAX_CHANGE,
        WINDOW_50HZ = 22,
        WINDOW_60HZ = 19,
        MAX_WINDOW = 24
    };

private:
    struct Axis {
        int16_t position;           // the running position last given
        int16_t shown;
        int pending;                // counts given but not shown yet
        uint8_t changed[MAX_WINDOW]; // counts shown in each recent millisecond, [0] = latest
    };

    Axis x_axis;
    Axis y_axis;
    uint16_t latest;                // the millisecond changed[0] belongs to
    int window;

    static void clearAxis(Axis &axis)
    {
        axis.pending = 0;
        for (int i = 0; i < MAX_WINDOW; i++) {
            axis.changed[i] = 0;
        }
    }

    static void follow(Axis &axis, int16_t position)
    {
        axis.pending += (int16_t)(position - axis.position);
        axis.position = position;
        if (axis.pending > MAX_BACKLOG) {
            axis.pending = MAX_BACKLOG;
        } else if (axis.pending < -MAX_BACKLOG) {
            axis.pending = -MAX_BACKLOG;
        }
    }

    static void age(Axis &axis, int milliseconds)
    {
        for (int i = MAX_WINDOW - 1; i >= 0; i--) {
            axis.changed[i] = (i >= milliseconds) ? axis.changed[i - milliseconds] : 0;
        }
    }

    bool step(Axis &axis)
    {
        if (!axis.pending) {
            return false;
        }
        int room = MAX_CHANGE;
        for (int i = 0; i < window; i++) {
            room -= axis.changed[i];
        }
        if (room <= 0) {
            return false;
        }
        int change = axis.pending;
        if (change > room) {
            change = room;
        } else if (change < -room) {
            change = -room;
        }
        axis.shown = (int16_t)(axis.shown + change);
        axis.pending -= change;
        axis.changed[0] += (uint8_t)((change < 0) ? -change : change);
        return true;
    }

public:
    MousePotPacer() : window(WINDOW_50HZ)
    {
        reset(0, 0, 0);
    }

    // Shows (x, y) at once, as the start of a new run of positions. `now` is
    // the millisecond clock. How far that jumps from what the lines showed
    // before is not known, so the jump spends the whole budget of its window.
    void reset(int16_t x, int16_t y, uint16_t now)
    {
        x_axis.position = x_axis.shown = x;
        y_axis.position = y_axis.shown = y;
        clearAxis(x_axis);
        clearAxis(y_axis);
        x_axis.changed[0] = MAX_CHANGE;
        y_axis.changed[0] = MAX_CHANGE;
        latest = now;
    }

    // Takes (x, y) as the running position from now on without moving the
    // pointer: the positions of another mouse do not follow on from this one's.
    void rebase(int16_t x, int16_t y)
    {
        x_axis.position = x;
        y_axis.position = y;
    }

    // WINDOW_50HZ or WINDOW_60HZ, for the frame rate of the machine.
    void setWindow(int milliseconds)
    {
        window = (milliseconds < 1) ? 1 : (milliseconds > MAX_WINDOW) ? MAX_WINDOW : milliseconds;
    }

    // The running position the mouse has reached. It may wrap past int16_t.
    void setTarget(int16_t x, int16_t y)
    {
        follow(x_axis, x);
        follow(y_axis, y);
    }

    bool settled(void) const
    {
        return !x_axis.pending && !y_axis.pending;
    }

    // Shows as much of the pending movement as the budget allows at
    // millisecond `now`. Returns whether the shown position changed. The clock
    // wraps every 65.536 seconds; a pacer left alone for a multiple of that
    // sees its old changes again, which can only hold one change back.
    bool advance(uint16_t now)
    {
        uint16_t elapsed = (uint16_t)(now - latest);
        if (elapsed) {
            int milliseconds = (elapsed < MAX_WINDOW) ? elapsed : MAX_WINDOW;
            age(x_axis, milliseconds);
            age(y_axis, milliseconds);
            latest = now;
        }
        bool moved_x = step(x_axis);
        bool moved_y = step(y_axis);
        return moved_x || moved_y;
    }

    uint8_t potX(void) const { return (uint8_t)(x_axis.shown & 0x7F); }
    uint8_t potY(void) const { return (uint8_t)(y_axis.shown & 0x7F); }
};

#endif
