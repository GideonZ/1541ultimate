#ifndef MONITOR_DEBUG_U64_H
#define MONITOR_DEBUG_U64_H

#include "memory_backend.h"
#include "monitor_debug_session.h"

class U64MemoryBackend;

static inline uint8_t u64_debug_step_cpu_port(MemoryBackend *backend)
{
    return backend ? backend->get_live_cpu_port() : (uint8_t)0x07;
}

// U64-specific Debug session implementation. Lives in a separate file so the
// host test build does not pull in `c64.h` / `u64_machine.h`.
DebugSession *create_u64_debug_session(U64MemoryBackend *backend);

#endif
