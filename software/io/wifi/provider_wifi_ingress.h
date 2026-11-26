#ifndef PROVIDER_WIFI_INGRESS_H
#define PROVIDER_WIFI_INGRESS_H
#include "cmd_buffer.h"
// Called from UART receive ingress after validating the complete RPC header.
// Returns true when the provider consumed the frame; the caller frees its buffer.
bool wifi_provider_receive(command_buf_t *buffer);
#endif
