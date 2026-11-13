/*
 * isr_env.h
 *
 * What the receive interrupt's dispatch (wifi_rx_isr(), cut out of wifi_cmd.cc as it is) and
 * the UART's buffer pool reset (DmaUART::ClearRxBuffer(), cut out of dma_uart.cc) are built
 * against on the build host. The real headers need the hardware; these are their parts that
 * the two functions use.
 */

#ifndef ISR_ENV_H_
#define ISR_ENV_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "rpc_calls.h"
#include "wifi_reply.h"

typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE  1
#define pdFALSE 0
typedef enum { eSetValueWithOverwrite } eNotifyAction;

#define CMD_BUF_SIZE 1600
#define NUM_TX_BUFFERS 12
#define WIFI_DEDICATED_THREAD 0xFC // as wifi_cmd.h has it
typedef struct {
    uint8_t data[CMD_BUF_SIZE];
    int bufnr;
    int size;
} command_buf_t;
typedef struct { int dummy; } command_buf_context_t;
typedef struct {
    command_buf_t *buf;
    uint32_t generation;
} dedicated_reply_t;

typedef struct { volatile uint8_t flowctrl; } dma_uart_t;
#define DMAUART_RESET 0x01

class DmaUART {
public:
    volatile dma_uart_t *uart;
    command_buf_context_t *packets;
    QueueHandle_t rx_bufs;
    volatile uint32_t generation;
    void ClearRxBuffer(void);
};
struct Esp32Stub { DmaUART *uart; };
extern Esp32Stub esp32;

extern wifi_reply_slot_t replySlots[WIFI_REPLY_SLOTS]; // static in wifi_cmd.cc
extern QueueHandle_t dedicated_replies;
extern volatile bool dedicated_waiting;

BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w);
// The value is the buffer's address in 32 bits on the target; the stub does not need it,
// so it is dropped here rather than cast on a 64-bit host
BaseType_t notify_from_isr(TaskHandle_t t, eNotifyAction a, BaseType_t *w);
#define xTaskNotifyFromISR(t, value, a, w) notify_from_isr((t), (a), (w))
BaseType_t cmd_buffer_received_isr(command_buf_context_t *context, command_buf_t *b, BaseType_t *w);
void ioWrite8(int reg, uint8_t value);
#define UART_DATA 0
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t new_queue);
void cmd_buffer_reset(command_buf_context_t *context);

#endif /* ISR_ENV_H_ */
