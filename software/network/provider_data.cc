#include "provider_data.h"
#include <limits.h>

namespace provider_data {
namespace {
uint64_t get(const uint8_t *p, unsigned n) {
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i) value |= uint64_t(p[i]) << (8*i);
    return value;
}
void put(uint8_t *p, uint64_t value, unsigned n) {
    for (unsigned i = 0; i < n; ++i) p[i] = uint8_t(value >> (8*i));
}
bool valid(const Frame& f) {
    if (f.opcode < Open || f.opcode > Write || !f.request ||
        !f.ticket.grant.challenge.provider.valid() || !f.ticket.grant.challenge.epoch ||
        !f.ticket.grant.owner.valid() || !f.ticket.session ||
        f.length > PayloadSize || f.count > PayloadSize) return false;
    if (f.reply) {
        if (f.budget_ms || f.port || f.status < Ok || f.status > Unavailable) return false;
        if (f.status != Ok) return !f.length && !f.count;
        if (f.opcode == Open) return !f.length && !f.count;
        return f.opcode == Read ? f.length == f.count : !f.length;
    }
    if (f.status || !f.budget_ms || f.budget_ms > INT32_MAX) return false;
    if (f.opcode == Open) {
        if (!f.port || f.count || f.length < 2 || f.payload[f.length-1]) return false;
        for (unsigned i = 0; i + 1 < f.length; ++i)
            if (f.payload[i] <= 32 || f.payload[i] > 126) return false;
        return true;
    }
    return !f.port && f.count && (f.opcode == Read ? !f.length : f.length == f.count);
}
struct Guard {
    Port& port;
    explicit Guard(Port& p) : port(p) { port.lock(); }
    ~Guard() { port.unlock(); }
};
}
bool same_ticket(const provider_session::Ticket& a, const provider_session::Ticket& b) {
    return a.grant.challenge.provider == b.grant.challenge.provider &&
        a.grant.challenge.epoch == b.grant.challenge.epoch && a.grant.owner == b.grant.owner && a.session == b.session;
}
bool decode(const uint8_t *p, size_t size, Frame& output) {
    if (!p || size != FrameSize || p[0] != 2 || p[1] != 0xfd || p[2] || p[3] ||
        memcmp(p+4,"PSD1",4) || p[8] != 1 || p[11] > 1 || p[78] || p[79]) return false;
    Frame f = {};
    f.opcode = p[9]; f.status = p[10]; f.reply = p[11]; f.request = get(p+12,8);
    memcpy(f.ticket.grant.challenge.provider.bytes,p+20,16);
    f.ticket.grant.challenge.epoch = get(p+36,8);
    memcpy(f.ticket.grant.owner.bytes,p+44,16); f.ticket.session = get(p+60,8);
    f.budget_ms = uint32_t(get(p+68,4)); f.count = uint16_t(get(p+72,2));
    f.port = uint16_t(get(p+74,2)); f.length = uint16_t(get(p+76,2));
    if (f.length > PayloadSize) return false;
    memcpy(f.payload,p+HeaderSize,f.length);
    for (unsigned i = f.length; i < PayloadSize; ++i) if (p[HeaderSize+i]) return false;
    if (!valid(f)) return false;
    output = f; return true;
}
bool encode(const Frame& f, uint8_t *p) {
    if (!p || !valid(f)) return false;
    memset(p,0,FrameSize); p[0] = 2; p[1] = 0xfd; memcpy(p+4,"PSD1",4);
    p[8] = 1; p[9] = f.opcode; p[10] = f.status; p[11] = f.reply;
    put(p+12,f.request,8); memcpy(p+20,f.ticket.grant.challenge.provider.bytes,16);
    put(p+36,f.ticket.grant.challenge.epoch,8); memcpy(p+44,f.ticket.grant.owner.bytes,16);
    put(p+60,f.ticket.session,8); put(p+68,f.budget_ms,4); put(p+72,f.count,2);
    put(p+74,f.port,2); put(p+76,f.length,2); memcpy(p+HeaderSize,f.payload,f.length);
    return true;
}
Delivery::Delivery(Port& port, bool replies) : port_(port), replies_(replies), occupied_(false), taken_(false) {}
void Delivery::invalidate_isr() { generation_.signal(); occupied_ = taken_ = false; }
void Delivery::disconnect() { Guard guard(port_); invalidate_isr(); }
bool Delivery::receive_isr(const uint8_t *bytes, size_t size, uint32_t now_ms) {
    Frame f;
    if (!decode(bytes,size,f) || f.reply != replies_) { invalidate_isr(); return false; }
    if (occupied_ || generation_.token().cancelled()) { invalidate_isr(); return false; }
    memcpy(bytes_,bytes,FrameSize); queued_ = generation_.token(); arrived_ms_ = now_ms; occupied_ = true; return true;
}
bool Delivery::take(Event& event) {
    Guard guard(port_);
    if (!occupied_ || taken_) return false;
    taken_ = true; event.generation = queued_; event.arrived_ms = arrived_ms_;
    return !queued_.cancelled() && decode(bytes_,FrameSize,event.frame);
}
void Delivery::finish(OperationCancellation generation) {
    Guard guard(port_);
    if (!generation.cancelled()) occupied_ = taken_ = false;
}
bool Delivery::send(const Frame& frame, OperationCancellation generation, OperationCancellation operation, OperationCancellation boot) {
    return transmit(frame,generation,operation,boot,false);
}
bool Delivery::complete(const Frame& frame, OperationCancellation generation, OperationCancellation operation) {
    return transmit(frame,generation,operation,{},true);
}
bool Delivery::transmit(const Frame& frame, OperationCancellation generation, OperationCancellation operation, OperationCancellation boot, bool complete) {
    uint8_t bytes[FrameSize];
    if (!encode(frame,bytes)) return false;
    Guard guard(port_);
    if (generation.cancelled()) return false;
    if (complete && (replies_ || !frame.reply || !occupied_ || !taken_)) return false;
    if (operation.cancelled() || boot.cancelled()) {
        if (complete) occupied_ = taken_ = false;
        return false;
    }
    if (port_.send(bytes,FrameSize)) {
        if (complete) occupied_ = taken_ = false;
        return true;
    }
    invalidate_isr(); return false;
}
Owner::Owner(Delivery& delivery, provider_control::Worker& control, Backend *backend) :
    delivery_(delivery), control_(control), backend_(backend), ticket_{}, request_(0) {}
void Owner::step(uint64_t now) {
    Event event;
    if (!delivery_.take(event)) return;
    Frame& f = event.frame;
    int result = -1;
    bool accepted = false;
    uint32_t elapsed = uint32_t(now) - event.arrived_ms;
    if (backend_ && !event.generation.cancelled() && elapsed < f.budget_ms) {
        f.budget_ms -= elapsed;
        if (f.opcode == Open) {
            accepted = f.request == 1 && !control_.occupied() && !same_ticket(f.ticket,ticket_);
            if (accepted) { ticket_ = f.ticket; request_ = 0; }
        } else accepted = same_ticket(f.ticket,ticket_) && control_.current(ticket_,now);
        accepted = accepted && request_ != UINT64_MAX && f.request == request_+1;
        if (accepted) { request_ = f.request; result = backend_->execute(f); }
    }
    // Backend owns admission and the original TLS deadline. It must check the
    // worker token while blocked; this final check also fences late body bytes.
    uint64_t completed = backend_ ? backend_->now_ms() : now;
    bool current = completed >= now && control_.current(ticket_,completed);
    if (result > f.count && f.opcode != Open) result = -1;
    if (f.opcode == Open && result != 0) result = -1;
    f.reply = true; f.budget_ms = 0; f.port = 0;
    f.status = !backend_ ? Unavailable : accepted && current && result >= 0 ? Ok : Failed;
    f.count = f.status == Ok && f.opcode != Open ? uint16_t(result) : 0;
    f.length = f.opcode == Read ? f.count : 0;
    if (!delivery_.complete(f,event.generation,current ? control_.resource_lifetime() : OperationCancellation())) control_.disconnect();
}
}
