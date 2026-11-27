#ifndef MONITOR_DEBUG_U2_H
#define MONITOR_DEBUG_U2_H

#include "monitor_debug_session.h"

class U2MemoryBackend;

// U2 (cartridge) Debug session implementation. Shares the BRK capture engine
// with the U64 backend through BrkDebugSession; U2 only supplies the
// hardware hooks. Lives in a separate file so host test builds do not pull
// in c64.h.
DebugSession *create_u2_debug_session(U2MemoryBackend *backend);

#endif
