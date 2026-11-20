#ifndef OWNER_HTTP_PROVIDER_H
#define OWNER_HTTP_PROVIDER_H
#include "http_connection.h"
#include "provider_data.h"

// Task-context boundary. wait() yields to the owner; it must return promptly.
// retire() invalidates the owner without freeing resources from the caller.
// The endpoint and provider outlive every connection, including failed ones.
class HttpOwnerEndpoint {
public:
    virtual ~HttpOwnerEndpoint() {}
    virtual bool ready() const = 0;
    virtual OperationCancellation lifetime() const = 0;
    virtual bool submit(const provider_data::Frame&, OperationCancellation) = 0;
    virtual bool collect(provider_data::Frame&) = 0;
    virtual uint32_t now_ms() const = 0;
    virtual void wait() = 0;
    virtual void retire() = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

class OwnerHttpProvider : public HttpSecureProvider {
public:
    OwnerHttpProvider(HttpOwnerEndpoint&, uint32_t exchange_ms, uint32_t cleanup_ms);
    bool available() const override;
    OperationCancellation lifetime() const override;
    HttpConnection *create_connection(const OperationCancellation&) override;
private:
    class Connection;
    HttpOwnerEndpoint& endpoint_;
    uint32_t exchange_ms_, cleanup_ms_;
    bool occupied_;
    OwnerHttpProvider(const OwnerHttpProvider&) = delete;
    OwnerHttpProvider& operator=(const OwnerHttpProvider&) = delete;
};
#endif
