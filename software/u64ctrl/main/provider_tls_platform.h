#ifndef PROVIDER_TLS_PLATFORM_H
#define PROVIDER_TLS_PLATFORM_H
#include "provider_tls_backend.h"
#include "mbedtls/x509_crt.h"

// Explicit boot-time provisioning, before the UART owner starts. Readiness
// predicates must qualify the UTC clock used by Mbed TLS and hardware entropy;
// they remain live and are checked during every bounded network wait.
class EspProviderTlsPlatform : public provider_data::TlsPlatform {
public:
    EspProviderTlsPlatform();
    bool configure(const unsigned char *roots, size_t length,
                   bool (*clock_ready)(), bool (*entropy_ready)());
    bool configured() const { return configured_; }
    tls_stream_port callbacks() override;
    void bind_cancel(bool (*predicate)(void*), void *context) override;
private:
    int socket_;
    mbedtls_x509_crt roots_;
    bool configured_;
    bool (*clock_ready_)(), (*entropy_ready_)(), (*cancel_)(void*);
    void *cancel_context_;
    static int64_t clock(void*);
    static bool time_ready(void*);
    static bool cancelled(void*);
    static int entropy(void*,unsigned char*,size_t);
    static int trust(void*,mbedtls_ssl_config*);
    static int open_socket(void*,const char*,uint16_t,uint32_t);
    static int read_socket(void*,uint8_t*,size_t,uint32_t);
    static int write_socket(void*,const uint8_t*,size_t,uint32_t);
    static void close_socket(void*);
    bool wait_socket(bool writing,int64_t deadline);
    struct Budget { EspProviderTlsPlatform *self; int64_t deadline; };
    static uint32_t remaining(void*);
    EspProviderTlsPlatform(const EspProviderTlsPlatform&) = delete;
    EspProviderTlsPlatform& operator=(const EspProviderTlsPlatform&) = delete;
};
#endif
