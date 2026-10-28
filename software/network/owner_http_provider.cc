#include "owner_http_provider.h"
#include "provider_data_client.h"
#include <limits.h>
#include <new>

class OwnerHttpProvider::Connection : public HttpConnection {
    OwnerHttpProvider& provider_;
    OperationCancellation command_, boot_;
    uint32_t started_;
    bool attempted_, closed_, opened_;
    bool cancelled() const { return command_.cancelled() || boot_.cancelled(); }
    int transact(unsigned opcode, void *buffer, int count, uint16_t port = 0) {
        HttpOwnerEndpoint& endpoint = provider_.endpoint_;
        bool closing = opcode == provider_data::Client::Close;
        if (!closing && (cancelled() || uint32_t(endpoint.now_ms()-started_) >= provider_.exchange_ms_)) return -1;
        provider_data::Frame frame = {};
        frame.opcode = opcode; frame.port = port;
        frame.budget_ms = closing ? provider_.cleanup_ms_ : provider_.exchange_ms_;
        if (opcode == provider_data::Open) frame.length = count;
        else frame.count = count;
        if (opcode == provider_data::Write) frame.length = count;
        if (opcode != provider_data::Read && count) memcpy(frame.payload,buffer,count);
        if (!endpoint.submit(frame,closing ? OperationCancellation() : command_)) return -1;
        uint32_t began = endpoint.now_ms(), cancel_at = began;
        bool draining = closing;
        for (;;) {
            uint32_t now = endpoint.now_ms();
            if (!draining && (cancelled() || uint32_t(now-started_) >= provider_.exchange_ms_)) {
                cancel_at = now; draining = true;
            }
            if (endpoint.collect(frame)) {
                if (frame.status != provider_data::Ok || (!closing && (draining || cancelled()))) return -1;
                if (frame.opcode != opcode || frame.count > count ||
                    (opcode == provider_data::Read && frame.length != frame.count)) { endpoint.retire(); return -1; }
                if (opcode == provider_data::Read && frame.length) memcpy(buffer,frame.payload,frame.length);
                return frame.count;
            }
            // An unresponsive owner cannot retain the caller or permit reuse.
            if (draining && uint32_t(now-cancel_at) >= provider_.cleanup_ms_) { endpoint.retire(); return -1; }
            endpoint.wait();
        }
    }
public:
    Connection(OwnerHttpProvider& p, OperationCancellation token) : provider_(p),command_(token),boot_(p.lifetime()),
        started_(0),attempted_(false),closed_(false),opened_(false) {}
    ~Connection() override { close_session(); provider_.endpoint_.lock(); provider_.occupied_ = false; provider_.endpoint_.unlock(); }
    int open(const char *host,uint16_t port) override {
        if (closed_ || attempted_) return -1;
        attempted_ = true; started_ = provider_.endpoint_.now_ms();
        if (!host || !port) return -1;
        size_t length = strnlen(host,provider_data::PayloadSize);
        if (!length || length >= provider_data::PayloadSize) return -1;
        int result = transact(provider_data::Open,const_cast<char*>(host),int(length+1),port);
        opened_ = result == 0; return result;
    }
    int read(void *bytes,int count) override {
        if (!opened_ || closed_ || !bytes || count <= 0) return -1;
        int result = transact(provider_data::Read,bytes,count > provider_data::PayloadSize ? provider_data::PayloadSize : count);
        if (result < 0) close_session();
        return result;
    }
    int write(const void *bytes,int count) override {
        if (!opened_ || closed_ || !bytes || count <= 0) return -1;
        int result = transact(provider_data::Write,const_cast<void*>(bytes),count > provider_data::PayloadSize ? provider_data::PayloadSize : count);
        if (result <= 0) { close_session(); return -1; }
        return result;
    }
    void close_session() override {
        if (closed_) return;
        closed_ = true;
        if (attempted_ && transact(provider_data::Client::Close,0,0) < 0) provider_.endpoint_.retire();
    }
};
OwnerHttpProvider::OwnerHttpProvider(HttpOwnerEndpoint& endpoint,uint32_t exchange_ms,uint32_t cleanup_ms) :
    endpoint_(endpoint),exchange_ms_(exchange_ms),cleanup_ms_(cleanup_ms),occupied_(0) {}
bool OwnerHttpProvider::available() const {
    return exchange_ms_ && exchange_ms_ <= INT32_MAX && cleanup_ms_ && cleanup_ms_ <= INT32_MAX && endpoint_.ready();
}
OperationCancellation OwnerHttpProvider::lifetime() const { return endpoint_.lifetime(); }
HttpConnection *OwnerHttpProvider::create_connection(const OperationCancellation& token) {
    if (!available() || token.cancelled()) return 0;
    endpoint_.lock();
    bool occupied = occupied_; occupied_ = true;
    endpoint_.unlock();
    if (occupied) return 0;
    Connection *connection = new(std::nothrow) Connection(*this,token);
    if (!connection) { endpoint_.lock(); occupied_ = false; endpoint_.unlock(); }
    return connection;
}
