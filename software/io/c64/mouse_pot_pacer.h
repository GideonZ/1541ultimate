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
// The lines therefore change at most once per GAP_MS, by at most MAX_STEP
// counts per axis. GAP_MS - 1 ms covers a PAL frame (19.95ms), the 0.52ms the
// SID takes to measure a POT line and 2.5ms of interrupt delay, so no two reads
// of a driver that reads once per frame see two changes; NTSC frames are
// shorter. Movement that has to wait is capped at MAX_BEHIND counts per axis
// and the rest is dropped, so the pointer stops two changes after the mouse.
class MousePotPacer
{
public:
    enum {
        MAX_STEP = 63,
        MAX_BEHIND = 2 * MAX_STEP,
        GAP_MS = 24
    };

private:
    struct Axis {
        int16_t position;           // the running position last given
        int16_t shown;              // the position on the lines
        int16_t behind;             // counts given but not shown yet
    };

    Axis x_axis;
    Axis y_axis;
    uint16_t last_change;           // millisecond clock of the last change
    int gap_ms;

    static void resetAxis(Axis &axis, int16_t position)
    {
        axis.position = position;
        axis.shown = position;
        axis.behind = 0;
    }

    static void follow(Axis &axis, int16_t position)
    {
        int behind = axis.behind + (int16_t)(position - axis.position);
        axis.position = position;
        axis.behind = (int16_t)((behind > MAX_BEHIND) ? MAX_BEHIND : (behind < -MAX_BEHIND) ? -MAX_BEHIND : behind);
    }

    static void step(Axis &axis)
    {
        int change = (axis.behind > MAX_STEP) ? MAX_STEP : (axis.behind < -MAX_STEP) ? -MAX_STEP : axis.behind;
        axis.shown = (int16_t)(axis.shown + change);
        axis.behind = (int16_t)(axis.behind - change);
    }

public:
    // `gap` other than GAP_MS is for tests that show the pacing is needed.
    explicit MousePotPacer(int gap = GAP_MS) : last_change(0), gap_ms(gap)
    {
        resetAxis(x_axis, 0);
        resetAxis(y_axis, 0);
    }

    // Shows (x, y) at once; it counts as a change, so the next waits GAP_MS.
    void reset(int16_t x, int16_t y, uint16_t now)
    {
        resetAxis(x_axis, x);
        resetAxis(y_axis, y);
        last_change = now;
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

    // Shows more of the waiting movement once GAP_MS has passed, and says
    // whether the lines changed. A wrap of the 65.536s clock can make a change
    // wait up to GAP_MS longer, never less.
    bool advance(uint16_t now)
    {
        if (!isBehind() || ((uint16_t)(now - last_change) < gap_ms)) {
            return false;
        }
        step(x_axis);
        step(y_axis);
        last_change = now;
        return true;
    }

    uint8_t potX(void) const { return (uint8_t)(x_axis.shown & 0x7F); }
    uint8_t potY(void) const { return (uint8_t)(y_axis.shown & 0x7F); }
};

#endif
