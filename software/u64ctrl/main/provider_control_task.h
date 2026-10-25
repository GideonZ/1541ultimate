#ifndef PROVIDER_CONTROL_TASK_H
#define PROVIDER_CONTROL_TASK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void provider_control_start(void);
// True means the reserved envelope was consumed, including malformed or dropped
// requests. Caller always retains and frees its receive buffer exactly once.
bool provider_control_receive(const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
#endif
