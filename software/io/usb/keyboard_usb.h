/*
 * keyboard_usb.h
 *
 *  Created on: Mar 5, 2017
 *      Author: gideon
 */

#ifndef KEYBOARD_USB_H_
#define KEYBOARD_USB_H_

#include "keyboard.h"
#include "integer.h"

#if U64 && !RECOVERYAPP
typedef struct tmrTimerControl * TimerHandle_t;
#endif

static const int USB_KEY_BUFFER_SIZE = 64;
static const int REST_TAP_QUEUE_SIZE = 128;
#define USB_DATA_SIZE 8

class GenericHost;

class Keyboard_USB : public Keyboard
{
	struct RestTapEntry {
		uint8_t matrix[8];
		uint8_t setup_matrix[8];
		uint8_t hold_ticks;
		bool restore;
	};

	volatile uint8_t *matrix;
	bool matrixEnabled;
	uint8_t matrix_state[8];
	uint8_t injected_matrix_state[8];
	uint8_t rest_matrix_state[8];
	uint8_t rest_matrix_overlay[8];
	uint8_t rest_overlay_hold[8][8];
	bool rest_matrix_control;
	RestTapEntry rest_tap_queue[REST_TAP_QUEUE_SIZE];
	uint8_t rest_tap_head;
	uint8_t rest_tap_tail;
	uint8_t rest_tap_gap_ticks;
	uint8_t rest_pending_tap_matrix[8];
	uint8_t rest_pending_tap_hold;
	uint8_t rest_pending_tap_delay;
	uint8_t key_buffer[USB_KEY_BUFFER_SIZE];
	uint8_t injected_buffer[USB_KEY_BUFFER_SIZE];
    uint8_t last_data[USB_DATA_SIZE];
    uint8_t usb_restore;
    uint8_t usb_freeze;
    bool rest_restore;
    uint8_t rest_restore_overlay;
    uint8_t rest_restore_hold;
#if U64 && !RECOVERYAPP
    TimerHandle_t rest_timer;
#endif
    int  key_head;
    int  key_tail;
    int  injected_head;
    int  injected_tail;
    void putch(uint8_t ch);
    bool PresentInLastData(uint8_t check);
    void usb2matrix(uint8_t *data);

    int  num_keys;
    int  repeat_speed;
    int  first_delay;
    int  delay_count;
    int  injected_matrix_hold;
	void applyMatrixState(void);
	void clearInjectedMatrixState(void);
	void setInjectedMatrixKey(int key);
	uint8_t effectiveRestoreBit(void) const;
	void applyRestWasdGuard(void) const;
	bool restTapQueueEmpty(void) const;
	bool restTapOverlayActive(void) const;
	void startRestTap(const RestTapEntry& entry);
#if U64 && !RECOVERYAPP
	static void S_rest_timer(TimerHandle_t a);
#endif
public:
    Keyboard_USB();
    ~Keyboard_USB();

    // called from USB thread
    void process_data(uint8_t *kbdata);

    // called from the user interface thread
    int  getch(void);
    void push_head(int c);
    void push_head_repeat(int c, int repeat);
    int  count_injected_key(int c) const;
    bool has_injected_key(int c) const;
    void remove_injected_key(int c);
    void wait_free(void);
    void clear_buffer(void);

	void restPress(uint8_t row, uint8_t col_bit);
	void restRelease(uint8_t row, uint8_t col_bit);
		void restPressMatrix(const uint8_t press_matrix[8]);
		void restReleaseMatrix(const uint8_t release_matrix[8]);
		void restBeginMatrixControl(void);
		void restEndMatrixControl(void);
		void restTap(uint8_t row, uint8_t col_bit, uint8_t hold_ticks);
	bool restQueueTap(const uint8_t matrix[8], bool restore, uint8_t hold_ticks);
	bool restQueueTap(const uint8_t matrix[8], const uint8_t setup_matrix[8], bool restore, uint8_t hold_ticks);
	void restPressRestore(void);
	void restReleaseRestore(void);
    void restTapRestore(uint8_t hold_ticks);
    void restReleaseAll(void);
    void restSnapshot(uint8_t out_matrix[8], bool &out_restore) const;
    void restPersistentSnapshot(uint8_t out_matrix[8], bool &out_restore) const;
    bool restMatrixActive(void) const;
    void tickRestOverlays(void);

    bool isMatrixEnabled() const { return matrixEnabled; }
    int initialRepeatDelay() const { return first_delay; }
    int repeatSpeed() const { return repeat_speed; }

    // attach / detach matrix peripheral to send keystrokes to.
    void setMatrix(volatile uint8_t *matrix);
    void enableMatrix(bool enable);
};

extern Keyboard_USB system_usb_keyboard;

#endif /* KEYBOARD_USB_H_ */
