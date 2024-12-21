#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pinout.h"
#include "button_handler.h"
#include "rpc_dispatch.h"
#include "jtag.h"

#define SHORT_PRESS 100
#define LONG_PRESS 1000
#define OFF_PRESS 4000
#define IGNORE_PRESS 300

static const char *TAG = "btn_handler";

int initial_power_state = 0;
QueueHandle_t button_queue;

static void regulator_enable(int enable, int delay)
{
    gpio_config_t io_conf_uart_off = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IO_UART_TXD) | (1ULL << IO_UART_RXD) | (1ULL << IO_UART_RTS) | (1ULL << IO_UART_CTS),
        .pull_down_en = 1,
        .pull_up_en = 0,
    };
    if (!enable) {
        ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, -1, -1, -1, -1) );
        ESP_ERROR_CHECK( gpio_config(&io_conf_uart_off) );
        ESP_LOGI(TAG, "UART OFF");
    }
    if (enable) {
        ESP_LOGI(TAG, "5V regultor on");
        gpio_set_level(IO_ENABLE_V50, 1);
        if (delay) {
            vTaskDelay(delay / portTICK_PERIOD_MS);
        }        
        gpio_set_level(IO_ENABLE_MOD, 1);
        ESP_LOGI(TAG, "mod regulators on");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
        gpio_set_level(IO_ENABLE_MOD, 0);
        ESP_LOGI(TAG, "mod regulators off");
        //vTaskDelay(10 / portTICK_PERIOD_MS);
        gpio_set_level(IO_ENABLE_V50, 0);
        ESP_LOGI(TAG, "5V regultor off");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (enable) {
        ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, IO_UART_TXD, IO_UART_RXD, IO_UART_RTS, IO_UART_CTS));
    }
}

static void handle_button_event(int event)
{
    uint32_t id_code;
    send_button_event(event);
    switch(event) {
        case BUTTON_UP_SHORT:
            ESP_LOGI(TAG, "Up Short");
            break;
        case BUTTON_UP_LONG:
            ESP_LOGI(TAG, "Up Long");
            break;
        case BUTTON_DOWN_SHORT:
            ESP_LOGI(TAG, "Down Short");
            break;
        case BUTTON_DOWN_LONG:
            ESP_LOGI(TAG, "Down Long");
            break;
        case BUTTON_ON:
            ESP_LOGI(TAG, "** ON **");
            regulator_enable(1, 0);
            break;
        case BUTTON_ON2:
            ESP_LOGI(TAG, "** ON with delay **");
            regulator_enable(1, 20);
            break;
        case BUTTON_OFF:
            ESP_LOGI(TAG, "** OFF **");
            regulator_enable(0, 0);
            break;
    }
}

static void button_handler(void *arg)
{
    int initial_power_state = *((int *)arg);
    int up, down;
    int up_count = 0;
    int down_count = 0;
    int machine_on = initial_power_state;
    uint8_t event;

    gpio_set_direction(IO_BUTTON_UP, GPIO_MODE_INPUT);
    gpio_set_direction(IO_BUTTON_DOWN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(IO_BUTTON_UP, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(IO_BUTTON_DOWN, GPIO_PULLUP_ONLY);
    gpio_set_level(IO_ENABLE_V50, initial_power_state);
    gpio_set_level(IO_ENABLE_MOD, initial_power_state);
    gpio_set_direction(IO_ENABLE_V50, GPIO_MODE_OUTPUT);
    gpio_set_direction(IO_ENABLE_MOD, GPIO_MODE_OUTPUT);
    vTaskDelay(100 / portTICK_PERIOD_MS); // Allow pull up to do its work.
    regulator_enable(initial_power_state, 0); // turn off uart also

    while (1) {
        up = gpio_get_level(IO_BUTTON_UP);
        down = gpio_get_level(IO_BUTTON_DOWN);

        if (!machine_on) {
            if (up == 0 || down == 0) {
                if (up == 0)
                    handle_button_event(BUTTON_ON);
                else if (down == 0) {
                    handle_button_event(BUTTON_ON2);
                }
                machine_on = 1;
                do {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                } while(gpio_get_level(IO_BUTTON_UP) == 0 || gpio_get_level(IO_BUTTON_DOWN) == 0);
                vTaskDelay(IGNORE_PRESS / portTICK_PERIOD_MS);
            }
            vTaskDelay(1);
            continue;
        }

        if (up == 0) {
            up_count++;
        } else { // released
            if (up_count > (LONG_PRESS / portTICK_PERIOD_MS)) {
                handle_button_event(BUTTON_UP_LONG);
                vTaskDelay(IGNORE_PRESS / portTICK_PERIOD_MS);
            } else if (up_count > (SHORT_PRESS / portTICK_PERIOD_MS)) {
                handle_button_event(BUTTON_UP_SHORT);
                vTaskDelay(IGNORE_PRESS / portTICK_PERIOD_MS);
            }
            up_count = 0;
        }

        if (down == 0) {
            down_count++;
            if (down_count > (OFF_PRESS / portTICK_PERIOD_MS)) {
                handle_button_event(BUTTON_OFF);
                while(gpio_get_level(IO_BUTTON_DOWN) == 0) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                machine_on = 0;
                down_count = 0;
            }
        } else { // released
            if (down_count > (LONG_PRESS / portTICK_PERIOD_MS)) {
                handle_button_event(BUTTON_DOWN_LONG);
                vTaskDelay(IGNORE_PRESS / portTICK_PERIOD_MS);
            } else if (down_count > (SHORT_PRESS / portTICK_PERIOD_MS)) {
                handle_button_event(BUTTON_DOWN_SHORT);
                vTaskDelay(IGNORE_PRESS / portTICK_PERIOD_MS);
            }
            down_count = 0;
        }

        if (xQueueReceive(button_queue, &event, 1) == pdTRUE) {
            handle_button_event(event);
            if (event == BUTTON_OFF) {
                machine_on = 0;
            } else if (event == BUTTON_ON) {
                machine_on = 1;
            }
        }
    }
}

void start_button_handler(int initial)
{
    button_queue = xQueueCreate(8, sizeof(uint8_t));
    xTaskCreate(button_handler, "button_handler", 3072, &initial, tskIDLE_PRIORITY + 2, NULL);
}

void extern_button_event(uint8_t button)
{
    xQueueSend(button_queue, &button, 0);
}
