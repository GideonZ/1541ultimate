// Host stand-in for software/io/wifi/wifi_cmd.h: only what wifi_dedicated.cc
// uses, backed by world.cc. Keep the declarations in step with the real header.
#ifndef WIFI_CMD_H_
#define WIFI_CMD_H_

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "rpc_calls.h"
extern "C" {
    #include "cmd_buffer.h"
}

class DmaUART
{
public:
    volatile uint32_t generation;
    BaseType_t GetBuffer(command_buf_t **buf, TickType_t ticks);
    BaseType_t TransmitPacket(command_buf_t *buf, uint16_t *ms = NULL);
    BaseType_t FreeBuffer(command_buf_t *buf);
};

class Esp32
{
public:
    DmaUART *uart;
};
extern Esp32 esp32;

extern uint16_t sequence_nr;
extern const char *no_wifi_buf;

#define WIFI_DEDICATED_THREAD NUM_TX_BUFFERS
typedef struct {
    command_buf_t *buf;
    uint32_t generation;
} dedicated_reply_t;
extern QueueHandle_t dedicated_replies;
extern volatile bool dedicated_waiting;

void world_enter_safe(void);
void world_leave_safe(void);
#define ENTER_SAFE_SECTION world_enter_safe()
#define LEAVE_SAFE_SECTION world_leave_safe()

// Everything the code under test prints goes into the trace.
int world_printf(const char *fmt, ...);
#define printf world_printf

#endif /* WIFI_CMD_H_ */
