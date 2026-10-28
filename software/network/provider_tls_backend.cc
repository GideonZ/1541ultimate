#include "provider_tls_backend.h"
#include <new>
namespace provider_data {
class TlsBackend::Resource : public provider_session::Resource {
public:
    TlsBackend& backend;
    TlsHttpConnection tls;
    bool stopped;
    Resource(TlsBackend& b,const tls_stream_port& port,uint32_t timeout) : backend(b),tls(port,timeout),stopped(false) {}
    void cancel() override { stopped = true; }
    bool cleanup() override { tls.close_session(); return true; }
    ~Resource() override {
        backend.last_ = tls.diagnostic();
        backend.platform_.bind_cancel(0,0);
        backend.resource_ = 0;
    }
};
TlsBackend::TlsBackend(provider_control::Worker& worker,TlsPlatform& platform) :
    worker_(worker),platform_(platform),resource_(0),ticket_{},last_{TLS_STAGE_VALIDATE,TLS_OK,0,0} {}
uint64_t TlsBackend::now_ms() {
    tls_stream_port port = platform_.callbacks();
    int64_t now = port.now_ms ? port.now_ms(port.context) : -1;
    return now < 0 ? UINT64_MAX : uint64_t(now);
}
bool TlsBackend::cancelled(void *context) {
    TlsBackend& self = *static_cast<TlsBackend*>(context);
    return !self.resource_ || self.resource_->stopped || !self.worker_.current(self.ticket_,self.now_ms());
}
int TlsBackend::execute(Frame& frame) {
    if (frame.opcode == Open) {
        if (resource_ || !frame.length || frame.length > PayloadSize || frame.payload[frame.length-1] || !frame.budget_ms) return -1;
        Resource *candidate = new(std::nothrow) Resource(*this,platform_.callbacks(),frame.budget_ms);
        if (!candidate) return -1;
        if (!worker_.admit(frame.ticket.grant,frame.ticket.session,candidate,now_ms(),ticket_)) { delete candidate; return -1; }
        resource_ = candidate;
        platform_.bind_cancel(cancelled,this);
    }
    if (!resource_ || !same_ticket(frame.ticket,ticket_) || cancelled(this)) return -1;
    int result = -1;
    if (frame.opcode == Open) result = resource_->tls.open(reinterpret_cast<char*>(frame.payload),frame.port);
    else if (frame.opcode == Read && frame.count && frame.count <= PayloadSize) result = resource_->tls.read(frame.payload,frame.count);
    else if (frame.opcode == Write && frame.count && frame.count == frame.length && frame.count <= PayloadSize) result = resource_->tls.write(frame.payload,frame.count);
    last_ = resource_->tls.diagnostic();
    return result;
}
tls_stream_diagnostics TlsBackend::diagnostic() const { return resource_ ? resource_->tls.diagnostic() : last_; }
}
