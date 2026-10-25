#ifndef PROVIDER_LIFECYCLE_H
#define PROVIDER_LIFECYCLE_H

#include "operation_cancellation.h"
#include "../../u64ctrl/main/provider_identity.h"

// Mutations are serialized by the UART ISR / task critical sections. Other
// tasks may only read identified() or copy token(); they never free resources.
// Identity is liveness evidence, not TLS capability or cleanup qualification.
class ProviderLifecycle
{
    CancellationSource cancellation;
    uint64_t serial;
    uint8_t cookie[8], identity[PROVIDER_ID_SIZE];
    uint32_t issued;
    bool pending, online;
public:
    enum { ReplyTimeoutMs = 1500, ProbeIntervalMs = 5000 };
    explicit ProviderLifecycle(uint64_t initial_serial = 0) : serial(initial_serial), issued(0), pending(false), online(false) {
        memset(cookie, 0, sizeof(cookie)); memset(identity, 0, sizeof(identity));
    }
    bool identified() const { return __atomic_load_n(&online, __ATOMIC_ACQUIRE); }
    OperationCancellation token() const { return cancellation.token(); }
    void invalidate() {
        __atomic_store_n(&online, false, __ATOMIC_RELEASE);
        cancellation.signal();
        pending = false;
    }
    bool expire(uint32_t now) {
        if (pending && uint32_t(now - issued) >= ReplyTimeoutMs) {
            invalidate(); return true;
        }
        return false;
    }
    bool begin(uint32_t now, uint8_t request[PROVIDER_PROBE_SIZE]) {
        expire(now);
        if (pending || (serial && uint32_t(now - issued) < ProbeIntervalMs)) return false;
        if (serial == UINT64_MAX) { invalidate(); return false; }
        ++serial; issued = now; pending = true;
        request[0] = 2; request[1] = 0xff; request[2] = request[3] = 0;
        memcpy(request + 4, "PID1", 4);
        for (unsigned i = 0; i < 8; ++i) cookie[i] = uint8_t(serial >> (8 * i));
        memcpy(request + 8, cookie, 8);
        return true;
    }
    bool receive(const uint8_t *packet, size_t size, uint32_t now) {
        if (expire(now) || !pending) return false;
        uint8_t boot[PROVIDER_ID_SIZE];
        if (!provider_identity_read(packet, size, cookie, boot)) return false;
        pending = false;
        if (!identified() || memcmp(identity, boot, sizeof(identity))) {
            invalidate();
            memcpy(identity, boot, sizeof(identity));
        }
        __atomic_store_n(&online, true, __ATOMIC_RELEASE);
        return true;
    }
};

#endif
