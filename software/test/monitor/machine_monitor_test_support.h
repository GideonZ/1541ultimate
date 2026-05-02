#ifndef MACHINE_MONITOR_TEST_SUPPORT_H
#define MACHINE_MONITOR_TEST_SUPPORT_H

#include <stdint.h>

#include "machine_monitor.h"
#include "screen.h"
#include "ui_elements.h"
#include "userinterface.h"

void reset_fake_config_manager_state(void);
void set_fake_ms_timer(uint16_t value);
void advance_fake_ms_timer(uint16_t delta);

extern int g_set_screen_title_call_count;

struct FakeMemoryBackend : public MemoryBackend
{
    uint8_t memory[65536];
    int session_begin_count;
    int session_end_count;

    FakeMemoryBackend();
    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual void read_block(uint16_t address, uint8_t *dst, uint16_t len);
    virtual void begin_session(void);
    virtual void end_session(void);
};

struct FakeBankedMemoryBackend : public MemoryBackend
{
    uint8_t ram[65536];
    uint8_t basic[8192];
    uint8_t kernal[8192];
    uint8_t charrom[4096];
    uint8_t io[4096];
    uint8_t screen_backup[1024];
    uint8_t ram_backup[2048];
    bool frozen;
    uint8_t live_cpu_port;
    uint8_t live_cpu_ddr;
    uint8_t live_dd00;
    int session_begin_count;
    int session_end_count;

    FakeBankedMemoryBackend();
    virtual uint8_t read(uint16_t address);
    virtual void write(uint16_t address, uint8_t value);
    virtual uint8_t get_live_cpu_port(void);
    virtual uint8_t get_live_vic_bank(void);
    virtual void begin_session(void);
    virtual void end_session(void);
    virtual bool supports_freeze(void) const;
    virtual bool is_frozen(void) const;
    virtual void set_frozen(bool on);
};

struct FakeFreezeControlBackend : public FakeMemoryBackend
{
    bool available;
    bool frozen;
    int set_frozen_calls;

    FakeFreezeControlBackend(bool available_now, bool frozen_now = false);
    virtual bool supports_freeze(void) const;
    virtual bool freeze_available(void) const;
    virtual bool is_frozen(void) const;
    virtual void set_frozen(bool on);
};

struct FakeFrozenVicBackend : public FakeBankedMemoryBackend
{
    uint8_t reported_vic_bank;
    uint8_t requested_vic_bank;
    int set_live_vic_bank_calls;

    FakeFrozenVicBackend();
    virtual uint8_t get_live_vic_bank(void);
    virtual void set_live_vic_bank(uint8_t vic_bank);
};

struct FakeRestrictedMemoryBackend : public FakeMemoryBackend
{
    int set_monitor_cpu_port_calls;
    int set_live_vic_bank_calls;

    FakeRestrictedMemoryBackend();
    virtual void set_monitor_cpu_port(uint8_t);
    virtual bool supports_cpu_banking(void) const;
    virtual bool supports_vic_bank(void) const;
    virtual void set_live_vic_bank(uint8_t);
    virtual const char *source_name(uint16_t) const;
};

class FakeKeyboard : public Keyboard
{
    const int *keys;
    int count;
    int index;
public:
    FakeKeyboard(const int *k, int c);
    int getch(void);
};

class CaptureScreen : public Screen
{
    int width;
    int height;
    int cursor_x;
    int cursor_y;
    int color;
public:
    char chars[25][40];
    uint8_t colors[25][40];
    bool reverse_chars[25][40];
    int write_counts[25][40];
    bool reverse_mode_on;
    int clear_calls;

    CaptureScreen();
    void set_background(int);
    void set_color(int c);
    int get_color();
    void reverse_mode(int enabled);
    int get_size_x(void);
    int get_size_y(void);
    void backup(void);
    void restore(void);
    void clear();
    void move_cursor(int x, int y);
    int output(char c);
    int output(const char *string);
    void repeat(char c, int rep);
    void output_fixed_length(const char *string, int, int width_to_write);
    void get_slice(int x, int y, int len, char *out) const;
    void reset_write_counts();
    int count_writes_outside_rect(int left, int top, int right, int bottom) const;
};

class CaptureWindow : public Window
{
    int width;
public:
    int last_x;
    int last_y;

    CaptureWindow(Screen *screen, int width);
    void move_cursor(int x, int y);
    void output_length(const char *, int);
    void repeat(char, int);
    int get_size_x(void);
};

class TestUserInterface : public UserInterface
{
public:
    int popup_count;
    char last_popup[128];
    char last_prompt_message[128];
    int last_prompt_maxlen;
    char prompt_texts[8][64];
    int prompt_results[8];
    int prompt_count;
    int prompt_index;

    TestUserInterface();
    void set_prompt(const char *text, int result);
    void push_prompt(const char *text, int result);
    int popup(const char *msg, uint8_t);
    int string_box(const char *msg, char *buffer, int maxlen);
    int string_box(const char *msg, char *buffer, int maxlen, bool);
    int string_box(const char *msg, char *buffer, int maxlen, bool, bool);
};

int fail(const char *message);
int expect(int condition, const char *message);
int popup_longest_line(const char *msg);
int expect_screens_equal(const CaptureScreen &a, const CaptureScreen &b, const char *message);
bool find_popup_rect(const CaptureScreen &screen, int *left, int *top, int *right, int *bottom);
int find_highlighted_cell(const CaptureScreen &screen, int *x, int *y);
void get_popup_line(const CaptureScreen &screen, int row, char *out, int out_len);
int popup_corner_diagonal_distance(int popup_left, int popup_top, int popup_right, int popup_bottom,
                                   int cursor_x, int cursor_y);

#endif