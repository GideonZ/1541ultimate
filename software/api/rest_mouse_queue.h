#ifndef REST_MOUSE_QUEUE_H
#define REST_MOUSE_QUEUE_H

#include "input_api.h"

#include <stdlib.h>

// The reports the REST mouse still has to send, in order. Each one is what a
// USB wheel mouse would send: the buttons held, a relative move and a wheel
// turn. `wait_ticks` is how long after the previous report it goes out, so a
// path keeps its timing and a tap keeps its button down for a while.
//
// Reports leave no closer together than the firmware polls a USB mouse, so the
// REST mouse does no more than a USB mouse can. MousePotPacer spreads fast
// movement over frames, and route_input.cc holds the next report back until
// the movement before it is on the POT lines, so none of a path is dropped.
//
// The queue only builds and hands out reports; route_input.cc sends them to
// the virtual mouse from its timer, under the input mutex.

struct RestMouseReport {
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
    int8_t wheel;               // vertical, positive away from the user
    int8_t pan;                 // horizontal, positive to the right
    uint16_t wait_ticks;
};

class RestMouseQueue
{
public:
    enum {
        CAPACITY = 1024
    };

private:
    RestMouseReport reports[CAPACITY];
    int head;
    int count;
    uint8_t tail_buttons;       // the buttons as the last queued report leaves them

    void push(uint8_t buttons, int dx, int dy, int wheel, int pan, int wait_ticks)
    {
        RestMouseReport &report = reports[(head + count) % CAPACITY];
        report.buttons = buttons;
        report.dx = (int8_t)dx;
        report.dy = (int8_t)dy;
        report.wheel = (int8_t)wheel;
        report.pan = (int8_t)pan;
        report.wait_ticks = (uint16_t)wait_ticks;
        count++;
        tail_buttons = buttons;
    }

public:
    RestMouseQueue() : head(0), count(0), tail_buttons(0) { }

    // Drops every pending report. `buttons` is what the mouse holds now.
    void clear(uint8_t buttons)
    {
        head = 0;
        count = 0;
        tail_buttons = buttons;
    }

    int pending(void) const { return count; }
    int room(void) const { return CAPACITY - count; }

    static int ticksFor(int milliseconds, int milliseconds_per_tick)
    {
        int ticks = (milliseconds + milliseconds_per_tick - 1) / milliseconds_per_tick;
        return (ticks > 0) ? ticks : 1;
    }

    // How many reports `event` adds. Events that are not mouse events add none.
    static int reportsFor(const InputParsedEvent &event)
    {
        switch (event.kind) {
        case INPUT_PARSED_MOUSE_BUTTONS:
            return (event.transition == INPUT_PARSED_TAP) ? 2 : 1;
        case INPUT_PARSED_MOUSE_MOVE:
            return 1;
        case INPUT_PARSED_MOUSE_WHEEL:
            return abs(event.wheel_vertical) + abs(event.wheel_horizontal);
        case INPUT_PARSED_MOUSE_PATH:
            return event.path_steps;
        default:
            return 0;
        }
    }

    // Whether a batch's mouse events fit into a queue with `room` free, counting
    // a `release_all` as emptying it. The events before a `release_all` are
    // queued too, so they have to fit as well. On false, `needed` and
    // `room_left` describe the part that does not fit.
    static bool batchFits(const InputParsedEvent *events, int event_count, int room, int &needed, int &room_left)
    {
        room_left = room;
        needed = 0;
        for (int i = 0; i < event_count; i++) {
            if (events[i].kind == INPUT_PARSED_RELEASE_ALL) {
                if (needed > room_left) {
                    return false;
                }
                room_left = CAPACITY;
                needed = 0;
            }
            needed += reportsFor(events[i]);
        }
        return needed <= room_left;
    }

    // Queues the reports for a validated mouse event. The caller has checked
    // there is room, with reportsFor(). `path` steps are read from the
    // request's JSON, which has to live until this returns.
    void append(const InputParsedEvent &event, int tap_hold_ticks, int milliseconds_per_tick)
    {
        switch (event.kind) {
        case INPUT_PARSED_MOUSE_BUTTONS:
            if (event.transition == INPUT_PARSED_RELEASE) {
                push(tail_buttons & ~event.mouse_buttons, 0, 0, 0, 0, 0);
            } else {
                push(tail_buttons | event.mouse_buttons, 0, 0, 0, 0, 0);
                if (event.transition == INPUT_PARSED_TAP) {
                    push(tail_buttons & ~event.mouse_buttons, 0, 0, 0, 0, tap_hold_ticks);
                }
            }
            break;
        case INPUT_PARSED_MOUSE_MOVE:
            push(tail_buttons, event.mouse_x, event.mouse_y, 0, 0, 0);
            break;
        case INPUT_PARSED_MOUSE_WHEEL:
        {
            for (int i = 0; i < abs(event.wheel_vertical); i++) {
                push(tail_buttons, 0, 0, (event.wheel_vertical > 0) ? 1 : -1, 0, 0);
            }
            for (int i = 0; i < abs(event.wheel_horizontal); i++) {
                push(tail_buttons, 0, 0, 0, (event.wheel_horizontal > 0) ? 1 : -1, 0);
            }
            break;
        }
        case INPUT_PARSED_MOUSE_PATH:
        {
            int interval = ticksFor(event.path_interval_ms, milliseconds_per_tick);
            for (int i = 0; i < event.path_steps; i++) {
                JSON_List *step = (JSON_List *)(*event.path)[i];
                push(tail_buttons, ((JSON_Integer *)(*step)[0])->get_value(),
                    ((JSON_Integer *)(*step)[1])->get_value(), 0, 0, (i == 0) ? 0 : interval);
            }
            break;
        }
        default:
            break;
        }
    }

    // The next report, if `ticks_since_last` is long enough for it and at least
    // `min_ticks`, the shortest time between two reports.
    bool takeDue(int ticks_since_last, int min_ticks, RestMouseReport &out)
    {
        if (!count) {
            return false;
        }
        int wait = reports[head].wait_ticks;
        if ((ticks_since_last < wait) || (ticks_since_last < min_ticks)) {
            return false;
        }
        out = reports[head];
        head = (head + 1) % CAPACITY;
        count--;
        return true;
    }
};

#endif
