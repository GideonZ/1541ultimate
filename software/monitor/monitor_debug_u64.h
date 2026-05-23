#ifndef MONITOR_DEBUG_U64_H
#define MONITOR_DEBUG_U64_H

#include "monitor_debug_session.h"

class U64MemoryBackend;

// U64-specific Debug session implementation. Lives in a separate file so the
// host test build does not pull in `c64.h` / `u64_machine.h`.
DebugSession *create_u64_debug_session(U64MemoryBackend *backend);

#endif
