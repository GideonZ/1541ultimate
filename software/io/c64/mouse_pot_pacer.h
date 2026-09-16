#ifndef MOUSE_POT_PACER_H
#define MOUSE_POT_PACER_H

#include "integer.h"

// Paces a 1351 mouse position onto control port 1's POT lines, so a C64 mouse
// driver never sees the pointer jump backward.
//
// A driver reads the 7-bit values once per frame and takes the change as a move
// of -64 to +63 counts; more than that reads as a move the other way. Reports
// come every 20ms but are handled after a varying delay, so two of them can
// land inside one frame.
//
// Movement goes out as it arrives, except that the counts shown within any
// WINDOW_MS of each other add up to at most MAX_STEP per axis. WINDOW_MS covers
// a PAL frame (19.95ms), the 0.52ms the SID takes to measure a POT line and
// 2.5ms of interrupt delay, so everything between two reads of a driver that
// reads once per frame falls inside one window; NTSC frames are shorter. Only
// movement that does not fit waits, so slow and precise movement reaches the
// lines with no delay and in the steps the mouse made.
//
// What waits is capped at MAX_BEHIND counts per axis, which is what one report
// can move (63 of motion plus 63 of wheel), and the rest is dropped, so the
// pointer stops just after the mouse.
class MousePotPacer
{
public:
    enum {
        MAX_STEP = 63,
        MAX_BEHIND = 2 * MAX_STEP,
        WINDOW_MS = 24
    };

private:
    struct Axis {
        int16_t position;               // the running position last given
        int16_t shown;                  // the position on the lines
        int16_t behind;                 // counts given but not shown yet
        uint8_t shown_at[WINDOW_MS];    // counts shown in each recent millisecond
    };

    Axis x_axis;
    Axis y_axis;
    uint16_t latest;                    // the millisecond shown_at[0] belongs to
    int window;

    static void resetAxis(Axis &axis, int16_t position, uint8_t spent)
    {
        axis.position = position;
        axis.shown = position;
        axis.behind = 0;
        for (int i = 0; i < WINDOW_MS; i++) {
            axis.shown_at[i] = 0;
        }
        axis.shown_at[0] = spent;
    }

    static void follow(Axis &axis, int16_t position)
    {
        int behind = axis.behind + (int16_t)(position - axis.position);
        axis.position = position;
        axis.behind = (int16_t)((behind > MAX_BEHIND) ? MAX_BEHIND : (behind < -MAX_BEHIND) ? -MAX_BEHIND : behind);
    }

    static void age(Axis &axis, int milliseconds)
    {
        for (int i = WINDOW_MS - 1; i >= 0; i--) {
            axis.shown_at[i] = (i >= milliseconds) ? axis.shown_at[i - milliseconds] : 0;
        }
    }

    // Shows as much of the waiting movement as the window has room for.
    bool step(Axis &axis)
    {
        if (!axis.behind) {
            return false;
        }
        int room = MAX_STEP;
        for (int i = 0; i < window; i++) {
            room -= axis.shown_at[i];
        }
        if (room <= 0) {
            return false;
        }
        int change = (axis.behind > room) ? room : (axis.behind < -room) ? -room : axis.behind;
        axis.shown = (int16_t)(axis.shown + change);
        axis.behind = (int16_t)(axis.behind - change);
        axis.shown_at[0] = (uint8_t)(axis.shown_at[0] + ((change < 0) ? -change : change));
        return true;
    }

public:
    // A `window_ms` other than WINDOW_MS is for tests that show the pacing is needed.
    explicit MousePotPacer(int window_ms = WINDOW_MS) : latest(0), window(window_ms)
    {
        resetAxis(x_axis, 0, 0);
        resetAxis(y_axis, 0, 0);
    }

    // Shows (x, y) at once. How far that jumps is not known, so it spends the
    // whole window.
    void reset(int16_t x, int16_t y, uint16_t now)
    {
        resetAxis(x_axis, x, MAX_STEP);
        resetAxis(y_axis, y, MAX_STEP);
        latest = now;
    }

    // The running position, which may wrap past int16_t.
    void setTarget(int16_t x, int16_t y)
    {
        follow(x_axis, x);
        follow(y_axis, y);
    }

    bool isBehind(void) const
    {
        return x_axis.behind || y_axis.behind;
    }

    // Shows what the window has room for, and says whether the lines changed. A
    // wrap of the 65.536s clock can make movement wait up to WINDOW_MS longer,
    // never less.
    bool advance(uint16_t now)
    {
        uint16_t elapsed = (uint16_t)(now - latest);
        if (elapsed) {
            int milliseconds = (elapsed < WINDOW_MS) ? elapsed : WINDOW_MS;
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
