/*
 * radio_entropy_esp.c
 *
 * The chip side of radio_entropy.c, for the ESP32 (software/wifi/raw_u64) and the
 * ESP32-S3 (software/u64ctrl).
 */

#include <stdarg.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "soc/soc.h"
#include "esp_private/periph_ctrl.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/apb_saradc_reg.h"
#include "soc/system_reg.h"
#include "soc/sens_reg.h"
#elif CONFIG_IDF_TARGET_ESP32
#include "soc/dport_access.h"
#include "soc/dport_reg.h"
#include "soc/i2s_reg.h"
#include "soc/sens_reg.h"
#else
#error "radio_entropy: no readback for this chip"
#endif
#include "radio_entropy.h"
#include "radio_entropy_port.h"

_Static_assert(RE_ERR_BUSY == ESP_ERR_TIMEOUT, "RE_ERR_BUSY is an esp_err_t");
_Static_assert(RE_ERR_RECURSION == ESP_ERR_INVALID_STATE, "RE_ERR_RECURSION is an esp_err_t");
_Static_assert(RE_ESP_ERR_INVALID_ARG == ESP_ERR_INVALID_ARG, "an esp_err_t");
_Static_assert(RE_ESP_ERR_INVALID_STATE == ESP_ERR_INVALID_STATE, "an esp_err_t");

// On ESP-IDF 5 the S3's bootloader_random_disable() leaves the SAR ADC's digital block in
// reset, so that every later enable fails its readback (measured on the S3 with 5.3.1),
// and it writes the SAR power force behind the reference count that the PHY and the ADC
// driver share. ESP-IDF 6 does neither, and the module is built with it.
#if CONFIG_IDF_TARGET_ESP32S3 && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))
#error "radio_entropy: the ESP32-S3 needs ESP-IDF 6 for the SAR ADC source"
#endif

static const char *TAG = "radio_entropy";

static portMUX_TYPE lock_mux = portMUX_INITIALIZER_UNLOCKED;

void re_port_lock(void)
{
    portENTER_CRITICAL(&lock_mux);
}

void re_port_unlock(void)
{
    portEXIT_CRITICAL(&lock_mux);
}

uint32_t re_port_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void re_port_delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

uintptr_t re_port_task(void)
{
    return (uintptr_t)xTaskGetCurrentTaskHandle();
}

void re_port_random_enable(void)
{
    bootloader_random_enable();
}

void re_port_random_disable(void)
{
    bootloader_random_disable();
}

void re_port_adc_ready(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    // The same register and lock as the ADC driver's own claim (adc_apb_periph_claim())
    PERIPH_RCC_ATOMIC() {
        SET_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_APB_SARADC_CLK_EN);
    }
#endif
}

// The registers that bootloader_random_enable() sets, on both ESP-IDF versions
bool re_port_random_readback(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    return ((REG_READ(APB_SARADC_CTRL2_REG) & APB_SARADC_TIMER_EN) != 0) &&
           ((REG_READ(SENS_SAR_MEAS1_MUX_REG) & SENS_SAR1_DIG_FORCE) != 0);
#else
    return ((REG_READ(SENS_SAR_START_FORCE_REG) & SENS_SAR2_EN_TEST) != 0) &&
           ((REG_READ(I2S_CONF_REG(0)) & I2S_RX_START) != 0) &&
           ((DPORT_REG_READ(DPORT_PERIP_CLK_EN_REG) & DPORT_I2S0_CLK_EN) != 0);
#endif
}

void re_port_log(const char *fmt, ...)
{
    char line[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    ESP_LOGW(TAG, "%s", line);
}

//----------------------------------
// radio_entropy_poll() every 10 s, for the module that has no loop of its own. The timer
// only notifies; the task takes the gate.

static TaskHandle_t poll_task;

static void poll_timer(TimerHandle_t timer)
{
    xTaskNotifyGive(poll_task);
}

static void poll_loop(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        radio_entropy_poll();
    }
}

void radio_entropy_start_poll_task(void)
{
    if (xTaskCreate(poll_loop, "re_poll", 3072, NULL, 1, &poll_task) != pdPASS) {
        ESP_LOGE(TAG, "no poll task");
        return;
    }
    TimerHandle_t timer = xTimerCreate("re_poll", pdMS_TO_TICKS(10000), pdTRUE, NULL, poll_timer);
    if (!timer || (xTimerStart(timer, 0) != pdPASS)) {
        ESP_LOGE(TAG, "no poll timer");
    }
}
