// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "malloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "hal/uart_hal.h"
#include "hal/gpio_hal.h"
#include "soc/uart_periph.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/uart_select.h"
#include "driver/periph_ctrl.h"
#include "sdkconfig.h"
#include "esp_rom_gpio.h"
#include "cmd_buffer.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/clk.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/clk.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/clk.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/clk.h"
#endif

#if 0 // CONFIG_UART_ISR_IN_IRAM
#define MY_ISR_ATTR     IRAM_ATTR
#define MY_MALLOC_CAPS  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#else
#define UART_ISR_ATTR
#define UART_MALLOC_CAPS  MALLOC_CAP_DEFAULT
#endif

#define UART_INTR_CONFIG_FLAG ((UART_INTR_RXFIFO_FULL) | (UART_INTR_RXFIFO_TOUT) | (UART_INTR_RXFIFO_OVF))

#define UART_ENTER_CRITICAL_ISR(mux)    portENTER_CRITICAL_ISR(mux)
#define UART_EXIT_CRITICAL_ISR(mux)     portEXIT_CRITICAL_ISR(mux)
#define UART_ENTER_CRITICAL(mux)    portENTER_CRITICAL(mux)
#define UART_EXIT_CRITICAL(mux)     portEXIT_CRITICAL(mux)

static const char *UART_TAG = "my_uart";
#define UART_CHECK(a, str, ret_val) \
    if (!(a)) { \
        ESP_LOGE(UART_TAG,"%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_val); \
    }

#define UART_CONTEX_INIT_DEF(uart_num) {\
    .hal.dev = UART_LL_GET_HW(uart_num),\
    .spinlock = portMUX_INITIALIZER_UNLOCKED,\
    .hw_enabled = false,\
}

typedef struct {
    uart_port_t uart_num;               /*!< UART port number*/
    command_buf_context_t *buffer_context; /*!< Buffer context */

    command_buf_t *current_tx_buf;      /*!< Buffer currently being transmitted, null when none */
    int current_tx_pnt;                 /*!< Pointer in buffer being currently transmitted */

    command_buf_t *current_rx_buf;      /*!< Buffer currently being received into; null when none */

} my_uart_obj_t;

typedef struct {
    uart_hal_context_t hal;        /*!< UART hal context*/
    portMUX_TYPE spinlock;
    bool hw_enabled;
} uart_context_t;

static my_uart_obj_t *p_uart_obj[UART_NUM_MAX] = {0};

static uart_context_t uart_context[UART_NUM_MAX] = {
    UART_CONTEX_INIT_DEF(UART_NUM_0),
    UART_CONTEX_INIT_DEF(UART_NUM_1),
#if UART_NUM_MAX > 2
    UART_CONTEX_INIT_DEF(UART_NUM_2),
#endif
};


// User ISR handler for UART
void UART_ISR_ATTR my_uart_intr_handler(void *param)
{
    my_uart_obj_t *p_uart = (my_uart_obj_t *) param;
    uint8_t uart_num = p_uart->uart_num;
    uint32_t uart_intr_status = 0;
    command_buf_t *buf;
    uint8_t store, data;
    uint32_t fifo_addr;
    int rx_fifo_len;

    portBASE_TYPE HPTaskAwoken = pdFALSE;

//    uint32_t debug_uart = UART_FIFO_AHB_REG(0);
//    WRITE_PERI_REG(debug_uart, '_');

    while (1) {
        // The `continue statement` may cause the interrupt to loop infinitely
        // we exit the interrupt here
        uart_intr_status = uart_hal_get_intsts_mask(&(uart_context[uart_num].hal));
        //Exit form while loop
        if (uart_intr_status == 0) {
            break;
        }
        if (uart_intr_status & UART_INTR_TXFIFO_EMPTY) {
            // turn off tx interrupt temporarily
            UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            uart_hal_disable_intr_mask(&(uart_context[uart_num].hal), UART_INTR_TXFIFO_EMPTY);
            UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            uart_hal_clr_intsts_mask(&(uart_context[uart_num].hal), UART_INTR_TXFIFO_EMPTY);

            if (! p_uart->current_tx_buf) { // no buffer available; let's try to get one
                if (cmd_buffer_get_tx_isr(p_uart->buffer_context, &(p_uart->current_tx_buf), &HPTaskAwoken) == pdTRUE) {
                    p_uart->current_tx_pnt = -1; // first transmit 0xC0
                }
            }
            if (p_uart->current_tx_buf) { // buffer available!
                // Get FIFO address to write to
                fifo_addr = (uart_num == UART_NUM_0) ? UART_FIFO_AHB_REG(0) : (uart_num == UART_NUM_1) ? UART_FIFO_AHB_REG(1) : UART_FIFO_AHB_REG(2);

                // We KNOW that there is more than one char available, otherwise the interrupt would not have triggered,
                // so we can safely send one char before asking how much space is available in the FIFO.
                if (p_uart->current_tx_pnt < 0) {
                    WRITE_PERI_REG(fifo_addr, 0xC0);
                    p_uart->current_tx_pnt = 0;
                }
                // check how much space there is in the TXFIFO, and copy at most this number of bytes into it
                // from the current buffer, encode it using the SLIP protocol on the fly.
                uint16_t fill_len = uart_hal_get_txfifo_len(&(uart_context[uart_num].hal));
                int remain = p_uart->current_tx_buf->size - p_uart->current_tx_pnt;
                uint8_t *pnt = p_uart->current_tx_buf->data + p_uart->current_tx_pnt;

                while ((remain > 0) && (fill_len > 2)) { // at least 3 chars will fit (one escaped char, data + end char)
                    if (*pnt == 0xC0) {
                        WRITE_PERI_REG(fifo_addr, 0xDB);
                        WRITE_PERI_REG(fifo_addr, 0xDC);
                        fill_len-=2;
                    } else if (*pnt == 0xDB) {
                        WRITE_PERI_REG(fifo_addr, 0xDB);
                        WRITE_PERI_REG(fifo_addr, 0xDD);
                        fill_len-=2;
                    } else {
                        WRITE_PERI_REG(fifo_addr, *pnt);
                        fill_len--;
                    }
                    pnt++;
                    remain--;
                }
                p_uart->current_tx_pnt = p_uart->current_tx_buf->size - remain;
                if (remain == 0) {
                    WRITE_PERI_REG(fifo_addr, 0xC0); // close!
                    cmd_buffer_free_isr(p_uart->buffer_context, p_uart->current_tx_buf, &HPTaskAwoken); // free buffer!
                    p_uart->current_tx_buf = NULL; // currently not transmitting
                }
                UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
                uart_hal_ena_intr_mask(&(uart_context[uart_num].hal), UART_INTR_TXFIFO_EMPTY);
                UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            }
        } // end of transmit code


        if (uart_intr_status & (UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL)) {
            // Turn off Interrupts temporarily
            UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            uart_hal_disable_intr_mask(&(uart_context[uart_num].hal), (UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL));
            UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            // Acknowledge the interrupt
            uart_hal_clr_intsts_mask(&(uart_context[uart_num].hal), (UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL));

            if (! p_uart->current_rx_buf) { // no buffer available; let's try to get one
//                WRITE_PERI_REG(debug_uart, '<');
                if (cmd_buffer_get_free_isr(p_uart->buffer_context, &(p_uart->current_rx_buf), &HPTaskAwoken) == pdTRUE) {
                    p_uart->current_rx_buf->size = 0; // reset pointer
                    p_uart->current_rx_buf->slipEscape = 0;
                } else {
//                    WRITE_PERI_REG(debug_uart, '#');
                }
            }

            if (p_uart->current_rx_buf) { // Buffer available
                // Let's see how many bytes there are in the FIFO
                rx_fifo_len = (int)uart_hal_get_rxfifo_len(&(uart_context[uart_num].hal));
                buf = p_uart->current_rx_buf;
                // Get FIFO address to read from
                fifo_addr = (uart_num == UART_NUM_0) ? UART_FIFO_REG(0) : (uart_num == UART_NUM_1) ? UART_FIFO_REG(1) : UART_FIFO_REG(2);

                // Copy data into buffer, decoding SLIP packet on the fly
                while(rx_fifo_len > 0) {
                    data = READ_PERI_REG(fifo_addr);
                    store = 1;
                    switch (data) {
                    case 0x20:
                        buf->slipEscape = 0;
                        store = 0;
                        if (buf->size) {
//                            WRITE_PERI_REG(debug_uart, '>');
                            cmd_buffer_received_isr(p_uart->buffer_context, buf, &HPTaskAwoken);
                            p_uart->current_rx_buf = NULL;
                            rx_fifo_len = 0;
                            buf = NULL;
                            // let the next IRQ get a new buffer.
                        }
                        break;

                    case 0xDB:
                        store = 0;
                        buf->slipEscape = 1;
                        break;

                    case 0xDC:
                        if (buf->slipEscape) {
                            data = 0xC0;
                        }
                        buf->slipEscape = 0;
                        break;

                    case 0xDD:
                        if (buf->slipEscape) {
                            data = 0xDB;
                        }
                        buf->slipEscape = 0;
                        break;

                    default:
                        buf->slipEscape = 0;
                        break;
                    }

                    if (store && buf) {
                        if (buf->size < CMD_BUF_SIZE) {
                            buf->data[buf->size++] = data;
                        } else {
                            buf->dropped++;
                        }
                    }
                    rx_fifo_len--;
                }
//                WRITE_PERI_REG(debug_uart, '{');
                UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
                uart_hal_ena_intr_mask(&(uart_context[uart_num].hal), (UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL));
                UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
//                WRITE_PERI_REG(debug_uart, '}');
            } else {
//                WRITE_PERI_REG(debug_uart, '$');
            }
        } // end of receive code

        if (uart_intr_status & UART_INTR_RXFIFO_OVF) {
            // When fifo overflows, we reset the fifo.
            UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            uart_hal_rxfifo_rst(&(uart_context[uart_num].hal));
            UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
            uart_hal_clr_intsts_mask(&(uart_context[uart_num].hal), UART_INTR_RXFIFO_OVF);
        }
    }
    if (HPTaskAwoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
//    WRITE_PERI_REG(debug_uart, '=');
}

BaseType_t my_uart_transmit_packet(uint8_t uart_num, command_buf_t *buf)
{
    my_uart_obj_t *obj = p_uart_obj[uart_num];
    if (cmd_buffer_transmit(obj->buffer_context, buf) == pdTRUE) {

        // Now enable the interrupt, so that the packet is going to be transmitted
        UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
        uart_hal_ena_intr_mask(&(uart_context[uart_num].hal), UART_INTR_TXFIFO_EMPTY);
        UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
        return pdTRUE;
    }
    cmd_buffer_free(obj->buffer_context, buf);
    return pdFALSE;
}

BaseType_t my_uart_receive_packet(uint8_t uart_num, command_buf_t **buf, TickType_t ticks)
{
    my_uart_obj_t *obj = p_uart_obj[uart_num];
    return cmd_buffer_received(obj->buffer_context, buf, ticks);
}

BaseType_t my_uart_free_buffer(uint8_t uart_num, command_buf_t *buf)
{
    my_uart_obj_t *obj = p_uart_obj[uart_num];
    BaseType_t ret = cmd_buffer_free(obj->buffer_context, buf);

    UART_ENTER_CRITICAL_ISR(&(uart_context[uart_num].spinlock));
    uart_hal_ena_intr_mask(&(uart_context[uart_num].hal), UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL);
    UART_EXIT_CRITICAL_ISR(&(uart_context[uart_num].spinlock));

    return ret;
}

BaseType_t my_uart_get_buffer(uint8_t uart_num, command_buf_t **buf, TickType_t ticks)
{
    my_uart_obj_t *obj = p_uart_obj[uart_num];
    BaseType_t ret = cmd_buffer_get(obj->buffer_context, buf, ticks);
    ESP_LOGI(UART_TAG, "Cmd Buffer Get: %d: %p->%p (context=%p)", ret, buf, *buf, obj->buffer_context);
    return ret;
}

esp_err_t my_uart_init(command_buf_context_t *buffers, uint8_t uart_num)
{
    UART_CHECK((uart_num < UART_NUM_MAX), "uart_num error", ESP_FAIL);

    my_uart_obj_t *obj = heap_caps_calloc(1, sizeof(my_uart_obj_t), UART_MALLOC_CAPS);
    p_uart_obj[uart_num] = obj;

    obj->uart_num = uart_num;
    obj->buffer_context = buffers;

    // Initialize the single copy transport buffers
    cmd_buffer_init(buffers);

    // Enable the UART and reset it
    periph_module_enable(uart_periph_signal[uart_num].module);
    periph_module_reset(uart_periph_signal[uart_num].module);

    const uart_intr_config_t interrupt_config = {
            .intr_enable_mask  = UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL,
            .rx_timeout_thresh = 2,
            .txfifo_empty_intr_thresh = 10,
            .rxfifo_full_thresh = 120,
    };

    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, // UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 122,
    };

    // Configure UART parameters
    esp_err_t ret = uart_param_config(uart_num, &uart_config);
    if (ret == ESP_OK) {
        // Register the interrupt routine
        ret = uart_isr_register(uart_num, my_uart_intr_handler, obj, 0, NULL);
        if (ret == ESP_OK) {
            // Configure the interrupt parameters and enables
            ret = uart_intr_config(uart_num, &interrupt_config);
        }
    }
    return ret;
}
