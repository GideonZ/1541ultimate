#ifndef HTTPS_MANAGEMENT_H
#define HTTPS_MANAGEMENT_H
#include "FreeRTOS.h"
extern "C" {
#include "cmd_buffer.h"
}
void https_management_init();
bool https_management_rx(command_buf_context_t *,command_buf_t *,BaseType_t *);
void https_management_time(uint32_t seconds);
void https_management_version(uint16_t major,uint16_t minor);
class JSON_Object;
void https_management_add_metrics(JSON_Object *json);
#endif
