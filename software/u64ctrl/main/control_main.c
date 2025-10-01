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
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "pinout.h"
#include "esp_err.h"
#include "rpc_dispatch.h"
#include "button_handler.h"
#include "jtag.h"
#include "sntp.h"

static const char *TAG = "u64ctrl";

#define SCALE_VBUS  ((12000 + 2200) * 65536 / 2200)
#define SCALE_VAUX  (( 2200 + 2200) * 65536 / 2200)
#define SCALE_V50   (( 3900 + 2200) * 65536 / 2200)
#define SCALE_V33   (( 2200 + 2200) * 65536 / 2200)
#define SCALE_V18   65536
#define SCALE_V10   65536
#define SCALE_VUSB  (( 3900 + 2200) * 65536 / 2200)

static void configure_led(void)
{
    gpio_reset_pin(IO_ESP_LED);
    gpio_set_direction(IO_ESP_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(IO_ESP_LED, 1); // off!
    gpio_reset_pin(IO_ESP_LED_RED);
    gpio_set_direction(IO_ESP_LED_RED, GPIO_MODE_OUTPUT);
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

static adc_oneshot_unit_handle_t adc1_handle;
static SemaphoreHandle_t adc_lock = NULL;
static adc_cali_handle_t adc1_cali_handle[7] = { 0 };

static void configure_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    static adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    for(int i=0;i<7;i++) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0+i, &config));
    }
    //-------------ADC1 Calibration Init---------------//
    for(int i=0;i<7;i++) {
        adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0+i, ADC_ATTEN_DB_12, &adc1_cali_handle[i]);
    }

    adc_lock = xSemaphoreCreateMutex();
}

const int adc_scale[7] = { SCALE_VBUS, SCALE_VAUX, SCALE_V50, SCALE_V33, SCALE_V18, SCALE_V10, SCALE_VUSB };
static uint16_t adc_data_local[7];

// static void read_adcs(void)
// {
//     const char *names[] = { "Vbus", "Vaux", "5.0V", "3.3V", "1.8V", "1.0V", "Vusb" };
//     int adc_raw, voltage;
//     for(int i=0;i<7;i++) {
//         ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0+i, &adc_raw));
//         ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle[i], adc_raw, &voltage));
//         ESP_LOGI(TAG, "ADC Channel %d, read: %d. Node %s: %d mV", i, adc_raw, names[i], (voltage * adc_scale[i]) >> 16);
//     }
// }

esp_err_t read_adc_channels(uint16_t *adc_data)
{
    int adc_raw, voltage;
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(adc_lock, portMAX_DELAY);
    for(int i=0;i<7;i++) {
        ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL_0+i, &adc_raw);
        if (ret != ESP_OK) {
            xSemaphoreGive(adc_lock);
            return ret;
        }
        ret = adc_cali_raw_to_voltage(adc1_cali_handle[i], adc_raw, &voltage);
        if (ret != ESP_OK) {
            xSemaphoreGive(adc_lock);
            return ret;
        }
        adc_data[i] = (voltage * adc_scale[i]) >> 16;
    }
    xSemaphoreGive(adc_lock);
    return ESP_OK;
}


void setup_modem();


int check_fpga(void)
{
    gpio_config_t io_conf_uart_off = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IO_UART_RXD),
        .pull_down_en = 1,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, -1, -1, -1, -1) );
    ESP_ERROR_CHECK( gpio_config(&io_conf_uart_off) );
    vTaskDelay(100 / portTICK_PERIOD_MS); // Allow pull down to do its work.
    int rxd = gpio_get_level(IO_UART_RXD);
    ESP_LOGW(TAG, "Current UART Status: RXD=%d", rxd);
    return rxd;
}

void app_main(void)
{
    // Check whether the application FPGA was already loaded.
    int initial_state = check_fpga();

    // Configure IOs
    jtag_disable_io();
    configure_led();
    configure_adc();
    setup_modem();
    start_button_handler(initial_state);

    while (1) {
        read_adc_channels(adc_data_local);
        ESP_LOGI(TAG, "App Main Alive; 5V_GOOD: %d (Initial: %d). VBus = %d mV", gpio_get_level(IO_5V_GOOD), initial_state, adc_data_local[0]);
        if (adc_data_local[0] < 8000) {
            gpio_set_level(IO_ESP_LED_RED, 0);
        } else {
            gpio_set_level(IO_ESP_LED_RED, 1);
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
