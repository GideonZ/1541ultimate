#ifndef ROUTE_INPUT_MENU_H
#define ROUTE_INPUT_MENU_H

#include "input_api.h"
#include "keyboard_c64.h"

#include <stdint.h>
#include <string.h>

class RouteInputMenuKeyboardState
{
    uint8_t held_matrix[8];
    uint8_t held_modifier_flags;
    uint8_t repeat_row;
    uint8_t repeat_col;
    int repeat_delay;
    bool menu_active;
    bool repeat_active;

    static uint8_t modifierFlagsFromMatrix(const uint8_t matrix[8])
    {
        uint8_t modifier_flags = 0;
        const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
        for (int i = 0; i < input_api_keyboard_map_count(); i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[i];
            if (entry.restore || ((matrix[entry.row] & (1 << entry.col)) == 0)) {
                continue;
            }
            modifier_flags |= Keyboard_C64::matrixModifierFlag(entry.row, entry.col);
        }
        return modifier_flags;
    }

    void updateRepeat(int initial_repeat_delay)
    {
        const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
        bool found = false;
        uint8_t row = 0;
        uint8_t col = 0;

        for (int i = 0; i < input_api_keyboard_map_count(); i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[i];
            if (entry.restore || ((held_matrix[entry.row] & (1 << entry.col)) == 0)) {
                continue;
            }
            if (Keyboard_C64::matrixModifierFlag(entry.row, entry.col) != 0) {
                continue;
            }
            if (found) {
                repeat_active = false;
                repeat_delay = 0;
                return;
            }
            found = true;
            row = entry.row;
            col = entry.col;
        }

        if (!found) {
            repeat_active = false;
            repeat_delay = 0;
            return;
        }

        if (!repeat_active || (repeat_row != row) || (repeat_col != col)) {
            repeat_delay = initial_repeat_delay;
        }
        repeat_row = row;
        repeat_col = col;
        repeat_active = true;
    }

public:
    RouteInputMenuKeyboardState()
    {
        menu_active = false;
        clear();
    }

    void reset(void)
    {
        menu_active = false;
        clear();
    }

    void clear(void)
    {
        memset(held_matrix, 0, sizeof(held_matrix));
        held_modifier_flags = 0;
        repeat_row = 0;
        repeat_col = 0;
        repeat_delay = 0;
        repeat_active = false;
    }

    void sync(bool active)
    {
        if (menu_active != active) {
            clear();
        }
        menu_active = active;
    }

    void snapshot(uint8_t matrix[8]) const
    {
        memcpy(matrix, held_matrix, sizeof(held_matrix));
    }

    static int collectKeys(const InputParsedEvent &event, uint8_t *keys, int max_keys, uint8_t *held_modifier_flags)
    {
        if (!keys || (max_keys <= 0)) {
            return 0;
        }

        const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
        uint8_t event_modifier_flags = 0;
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (!entry.restore) {
                event_modifier_flags |= Keyboard_C64::matrixModifierFlag(entry.row, entry.col);
            }
        }

        uint8_t shift_flag = event_modifier_flags;
        if (held_modifier_flags) {
            shift_flag |= *held_modifier_flags;
            if (event.transition == INPUT_PARSED_PRESS) {
                *held_modifier_flags |= event_modifier_flags;
            } else if (event.transition == INPUT_PARSED_RELEASE) {
                *held_modifier_flags &= ~event_modifier_flags;
            }
        }

        if (event.transition == INPUT_PARSED_RELEASE) {
            return 0;
        }

        int key_count = 0;
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (entry.restore || (Keyboard_C64::matrixModifierFlag(entry.row, entry.col) != 0)) {
                continue;
            }
            uint8_t key = Keyboard_C64::matrixToKeyCode(entry.row, entry.col, shift_flag);
            if ((key != 0) && (key_count < max_keys)) {
                keys[key_count++] = key;
            }
        }
        return key_count;
    }

    static int collectKeys(const InputParsedEvent &event, uint8_t *keys, int max_keys)
    {
        return collectKeys(event, keys, max_keys, 0);
    }

    int applyKeyboardEvent(const InputParsedEvent &event, uint8_t *keys, int max_keys, int initial_repeat_delay)
    {
        int key_count = collectKeys(event, keys, max_keys, &held_modifier_flags);
        if (event.transition != INPUT_PARSED_TAP) {
            const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
            for (int i = 0; i < event.keyboard_count; i++) {
                const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
                if (entry.restore) {
                    continue;
                }
                uint8_t bit = (1 << entry.col);
                if (event.transition == INPUT_PARSED_PRESS) {
                    held_matrix[entry.row] |= bit;
                } else if (event.transition == INPUT_PARSED_RELEASE) {
                    held_matrix[entry.row] &= ~bit;
                }
            }
            held_modifier_flags = modifierFlagsFromMatrix(held_matrix);
            updateRepeat(initial_repeat_delay);
        }
        return key_count;
    }

    int collectReleaseKeys(const InputParsedEvent &event, uint8_t *keys, int max_keys) const
    {
        if (!keys || (max_keys <= 0) || (event.transition != INPUT_PARSED_RELEASE)) {
            return 0;
        }

        int key_count = 0;
        const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (entry.restore || (Keyboard_C64::matrixModifierFlag(entry.row, entry.col) != 0)) {
                continue;
            }
            uint8_t key = Keyboard_C64::matrixToKeyCode(entry.row, entry.col, held_modifier_flags);
            if ((key != 0) && (key_count < max_keys)) {
                keys[key_count++] = key;
            }
        }
        return key_count;
    }

    int repeatTick(uint8_t *keys, int max_keys, int repeat_speed)
    {
        if (!keys || (max_keys <= 0) || !repeat_active) {
            return 0;
        }
        if (repeat_delay > 0) {
            repeat_delay--;
            return 0;
        }

        repeat_delay = repeat_speed;
        uint8_t key = Keyboard_C64::matrixToKeyCode(repeat_row, repeat_col, held_modifier_flags);
        if (key == 0) {
            return 0;
        }
        keys[0] = key;
        return 1;
    }
};

#endif
