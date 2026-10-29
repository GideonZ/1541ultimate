#ifndef PROVIDER_CONTROL_TASK_H
#define PROVIDER_CONTROL_TASK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void provider_control_start(void);
// Serialized boot-time provisioning only, before start. Returns false when the
// optional backend is not built. No built-in roots or readiness fallback.
bool provider_control_configure_tls(const unsigned char*, size_t, bool (*clock_ready)(void), bool (*entropy_ready)(void));
// True means the reserved envelope was consumed, including malformed or dropped
// requests. Caller always retains and frees its receive buffer exactly once.
bool provider_control_receive(const uint8_t*, size_t);
#ifdef __cplusplus
}
namespace provider_data { class TlsPolicy; }
bool provider_control_provision_tls(const unsigned char*, size_t, provider_data::TlsPolicy&);
bool provider_control_provision_system_tls(provider_data::TlsPolicy&);
#endif
#endif
