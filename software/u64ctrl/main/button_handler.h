#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_UP_SHORT 1
#define BUTTON_UP_LONG 2
#define BUTTON_DOWN_SHORT 3
#define BUTTON_DOWN_LONG 4
#define BUTTON_ON 5
#define BUTTON_OFF 6

void start_button_handler(int initial);
void extern_button_event(uint8_t button);

#endif
