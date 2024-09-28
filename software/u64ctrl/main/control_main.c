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
#include "hal/adc_types.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

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

#define SCALE_VBUS  ((12000 + 2200) * 65536 / 2200)
#define SCALE_VAUX  (( 2200 + 2200) * 65536 / 2200)
#define SCALE_V50   (( 3900 + 2200) * 65536 / 2200)
#define SCALE_V33   (( 2200 + 2200) * 65536 / 2200)
#define SCALE_V18   65536
#define SCALE_V10   65536
#define SCALE_VUSB  (( 3900 + 2200) * 65536 / 2200)

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

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

adc_oneshot_unit_handle_t adc1_handle;
adc_cali_handle_t adc1_cali_handle[7] = { 0 };

static void configure_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    static adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };

    for(int i=0;i<7;i++) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0+i, &config));
    }
    //-------------ADC1 Calibration Init---------------//
    for(int i=0;i<7;i++) {
        adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0+i, ADC_ATTEN_DB_11, &adc1_cali_handle[i]);
    }
}

static void read_adcs(void)
{
    const int scale[7] = { SCALE_VBUS, SCALE_VAUX, SCALE_V50, SCALE_V33, SCALE_V18, SCALE_V10, SCALE_VUSB };
    const char *names[] = { "Vbus", "Vaux", "5.0V", "3.3V", "1.8V", "1.0V", "Vusb" };
    int adc_raw, voltage;
    for(int i=0;i<7;i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0+i, &adc_raw));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle[i], adc_raw, &voltage));
        ESP_LOGI(TAG, "ADC Channel %d, read: %d. Node %s: %d mV", i, adc_raw, names[i], (voltage * scale[i]) >> 16);
    }
}

void app_main(void)
{
    /* Configure the peripheral according to the LED type */
    configure_led();
    configure_adc();
    while (1) {
        ESP_LOGI(TAG, "LED goes %s!", s_led_state == true ? "ON" : "OFF");
        blink_led();
        read_adcs();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
