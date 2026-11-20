#ifndef PROVIDER_TLS_BACKEND_H
#define PROVIDER_TLS_BACKEND_H
#include "provider_data.h"
#include "tls_http_connection.h"

namespace provider_data {
// One platform belongs to one owner. Blocking callbacks must poll the bound
// cancellation predicate, including DNS/connect waits. No resource cleanup or
// Worker::step may run from that predicate while TLS is on the stack.
class TlsPlatform {
public:
    virtual ~TlsPlatform() {}
    virtual tls_stream_port callbacks() = 0;
    virtual void bind_cancel(bool (*predicate)(void*), void *context) = 0;
};
class TlsBackend : public Backend {
public:
    TlsBackend(provider_control::Worker&, TlsPlatform&);
    int execute(Frame&) override;
    uint64_t now_ms() override;
    tls_stream_diagnostics diagnostic() const;
    bool occupied() const { return resource_ != 0; }
private:
    class Resource;
    provider_control::Worker& worker_;
    TlsPlatform& platform_;
    Resource *resource_;
    provider_session::Ticket ticket_;
    tls_stream_diagnostics last_;
    static bool cancelled(void*);
    TlsBackend(const TlsBackend&) = delete;
    TlsBackend& operator=(const TlsBackend&) = delete;
};
}
#endif
