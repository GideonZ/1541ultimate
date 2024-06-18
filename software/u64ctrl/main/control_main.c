/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "u64ctrl";

#define IO_BOOT        0
#define IO_SENSE_VBUS  1
#define IO_SENSE_VAUX  2
#define IO_SENSE_V50   3
#define IO_SENSE_V33   4
#define IO_SENSE_V18   5
#define IO_SENSE_V10   6
#define IO_SENSE_VUSB  7
#define IO_FPGA_TMS    8
#define IO_FPGA_TDI    9
#define IO_FPGA_TDO    10
#define IO_FPGA_TCK    11
#define IO_5V_GOOD     12
#define IO_BUTTON_UP   13
#define IO_BUTTON_DOWN 14
#define IO_UART_CTS    15
#define IO_UART_RTS    16
#define IO_ESP_LED     18
#define IO_ENABLE_MOD  21
#define IO_I2S_MCLK    33
#define IO_I2S_BCLK    34
#define IO_I2S_SDO     35
#define IO_I2S_LRCLK   36
#define IO_ENABLE_V50  37
#define IO_USB_PWROK   38
#define IO_TP16        39
#define IO_TP17        40
#define IO_TP18        41
#define IO_TP19        42

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;

static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

void app_main(void)
{

    /* Configure the peripheral according to the LED type */
    configure_led();

    while (1) {
        ESP_LOGI(TAG, "LED goes %s!", s_led_state == true ? "ON" : "OFF");
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
