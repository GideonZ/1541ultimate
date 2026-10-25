#ifndef OPERATION_CANCELLATION_H
#define OPERATION_CANCELLATION_H

#include <stdint.h>

class CancellationSource;

// A copied token never becomes current again after its source is signalled.
// The source must outlive every token. A default token is not cancellable.
class OperationCancellation
{
    const CancellationSource *source;
    uint32_t generation;
    OperationCancellation(const CancellationSource *s, uint32_t g) : source(s), generation(g) {}
    friend class CancellationSource;
public:
    OperationCancellation() : source(0), generation(0) {}
    bool cancelled() const;
};

// One serialized producer (the command-interface ISRs) and any number of
// readers. Loads/stores are lock-free; no allocation, callbacks or cleanup in
// the producer. Do not use signal() from concurrent tasks or nested producers.
class CancellationSource
{
    uint32_t generation;
    CancellationSource(const CancellationSource&);
    CancellationSource& operator=(const CancellationSource&);
public:
    explicit CancellationSource(uint32_t initial = 0) : generation(initial) {}
    uint32_t current() const { return __atomic_load_n(&generation, __ATOMIC_ACQUIRE); }
    OperationCancellation token() const { return OperationCancellation(this, current()); }
    void signal() {
        uint32_t value = current();
        // Exhaustion is terminal until the source is recreated. Never wrap and
        // accidentally authorize an old token.
        if (value != UINT32_MAX) __atomic_store_n(&generation, value + 1, __ATOMIC_RELEASE);
    }
};

inline bool OperationCancellation::cancelled() const
{
    if (!source) return false;
    uint32_t value = source->current();
    return value == UINT32_MAX || value != generation;
}

// Do not require lock-free read/modify/write instructions: this source only
// uses aligned word loads/stores. The firmware build must verify that these
// compile inline without atomic-library helpers on each supported processor.

#endif
