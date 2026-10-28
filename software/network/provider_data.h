#ifndef PROVIDER_DATA_H
#define PROVIDER_DATA_H
#include "provider_control.h"

namespace provider_data {
// Fits one existing UART packet. No pointers, native structs or host endianness
// cross the link. One outstanding operation keeps copying and storage bounded.
enum { PayloadSize = 256, HeaderSize = 80, FrameSize = HeaderSize + PayloadSize };
enum Opcode { Open = 1, Read = 2, Write = 3 };
enum Status { Ok = 1, Failed = 2, Unavailable = 3 };
struct Frame {
    uint8_t opcode, status;
    bool reply;
    uint64_t request;
    provider_session::Ticket ticket;
    uint32_t budget_ms;
    uint16_t count, port, length;
    uint8_t payload[PayloadSize];
};
bool decode(const uint8_t*, size_t, Frame&);
bool encode(const Frame&, uint8_t[FrameSize]);
bool same_ticket(const provider_session::Ticket&, const provider_session::Ticket&);

// Task locks must exclude receive/loss ingress. send copies the whole frame,
// never waits and never calls back into Delivery. Overflow loses the generation,
// even while the owner is processing a previously dequeued request.
class Port {
public:
    virtual ~Port() {}
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool send(const uint8_t*, size_t) = 0;
};
struct Event { Frame frame; OperationCancellation generation; uint32_t arrived_ms; };
class Delivery {
public:
    Delivery(Port&, bool replies);
    bool receive_isr(const uint8_t*, size_t, uint32_t now_ms = 0); // Caller holds lock, or ISR.
    void invalidate_isr();
    void disconnect(); // Task-context loss; no callbacks or resource cleanup.
    bool take(Event&); // Serialized owner. Slot stays occupied until finish.
    void finish(OperationCancellation);
    bool send(const Frame&, OperationCancellation, OperationCancellation operation = {},
              OperationCancellation boot = {}); // Final fences under transmit lock.
    OperationCancellation lifetime() const { return generation_.token(); }
private:
    Port& port_;
    CancellationSource generation_;
    uint8_t bytes_[FrameSize];
    OperationCancellation queued_;
    uint32_t arrived_ms_;
    bool replies_, occupied_, taken_;
    Delivery(const Delivery&) = delete;
    Delivery& operator=(const Delivery&) = delete;
};

// Called only by the provider owner task. Open must transfer a resource to the
// control Worker before doing TLS; subsequent operations use that same resource.
// TLS cancellation callbacks must consult Worker::current. Cleanup must wait
// until execute returns. No callback may retain the copied payload buffer.
class Backend {
public:
    virtual ~Backend() {}
    virtual int execute(Frame&) = 0; // Open: 0 success. Read/write: byte count.
    virtual uint64_t now_ms() = 0; // Same clock as owner step, sampled after work.
};
class Owner {
public:
    Owner(Delivery&, provider_control::Worker&, Backend* = 0);
    void step(uint64_t now); // At most one operation; TLS must honor budget_ms.
private:
    Delivery& delivery_;
    provider_control::Worker& control_;
    Backend *backend_;
    provider_session::Ticket ticket_;
    uint64_t request_;
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
};
}
#endif
