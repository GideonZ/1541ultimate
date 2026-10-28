#include "provider_tls_platform.h"
extern "C" {
#include "lwip_dns.h"
}
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <errno.h>
#include <time.h>

EspProviderTlsPlatform::EspProviderTlsPlatform() : socket_(-1),configured_(false),
    clock_ready_(0),entropy_ready_(0),cancel_(0),cancel_context_(0) { mbedtls_x509_crt_init(&roots_); }
EspProviderTlsPlatform::~EspProviderTlsPlatform() { close_socket(this); mbedtls_x509_crt_free(&roots_); }
bool EspProviderTlsPlatform::configure(const unsigned char *pem,size_t length,bool (*clock_ready)(),bool (*entropy_ready)()) {
    if (configured_ || !pem || !length || !clock_ready || !entropy_ready) return false;
    if (mbedtls_x509_crt_parse(&roots_,pem,length)) {
        mbedtls_x509_crt_free(&roots_); mbedtls_x509_crt_init(&roots_); return false;
    }
    clock_ready_ = clock_ready; entropy_ready_ = entropy_ready; configured_ = true; return true;
}
void EspProviderTlsPlatform::bind_cancel(bool (*predicate)(void*),void *context) { cancel_ = predicate; cancel_context_ = context; }
int64_t EspProviderTlsPlatform::clock(void*) { return esp_timer_get_time()/1000; }
bool EspProviderTlsPlatform::time_ready(void *context) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    return self.configured_ && self.clock_ready_() && time(0) >= 1577836800;
}
bool EspProviderTlsPlatform::cancelled(void *context) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    return !time_ready(context) || !self.entropy_ready_() || !self.cancel_ || self.cancel_(self.cancel_context_);
}
int EspProviderTlsPlatform::entropy(void *context,unsigned char *bytes,size_t count) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (!self.configured_ || !self.entropy_ready_()) return -1;
    esp_fill_random(bytes,count); return 0;
}
int EspProviderTlsPlatform::trust(void *context,mbedtls_ssl_config *config) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (!self.configured_) return -1;
    mbedtls_ssl_conf_ca_chain(config,&self.roots_,0); return 0;
}
uint32_t EspProviderTlsPlatform::remaining(void *context) {
    Budget& budget = *static_cast<Budget*>(context);
    int64_t left = budget.deadline-clock(0);
    return left <= 0 || cancelled(budget.self) ? 0 : uint32_t(left);
}
bool EspProviderTlsPlatform::wait_socket(bool writing,int64_t deadline) {
    Budget budget = {this,deadline};
    uint32_t left;
    while ((left = remaining(&budget))) {
        fd_set set; FD_ZERO(&set); FD_SET(socket_,&set);
        timeval timeout = {0, long((left < 10 ? left : 10)*1000)};
        int result = lwip_select(socket_+1,writing ? 0 : &set,writing ? &set : 0,0,&timeout);
        if (result > 0) return remaining(&budget) != 0;
        if (result < 0 && errno != EINTR) return false;
    }
    return false;
}
int EspProviderTlsPlatform::open_socket(void *context,const char *host,uint16_t port,uint32_t timeout) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (self.socket_ >= 0) return -1;
    Budget budget = {&self,clock(0)+timeout};
    ip_addr_t resolved;
    if (!remaining(&budget) || (!ipaddr_aton(host,&resolved) && lwip_dns_resolve(host,&resolved,remaining,&budget)) ||
        !IP_IS_V4(&resolved) || !remaining(&budget)) return -1;
    sockaddr_in address = {}; address.sin_family = AF_INET; address.sin_port = htons(port);
    address.sin_addr.s_addr = ip4_addr_get_u32(ip_2_ip4(&resolved));
    self.socket_ = lwip_socket(AF_INET,SOCK_STREAM,0);
    linger abort_on_close = {1,0};
    if (self.socket_ < 0 || lwip_setsockopt(self.socket_,SOL_SOCKET,SO_LINGER,&abort_on_close,sizeof(abort_on_close)) ||
        lwip_fcntl(self.socket_,F_SETFL,O_NONBLOCK)) return -1;
    int result = lwip_connect(self.socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address));
    if (result < 0 && errno == EINPROGRESS && self.wait_socket(true,budget.deadline)) {
        int error = 0; socklen_t size = sizeof(error);
        result = lwip_getsockopt(self.socket_,SOL_SOCKET,SO_ERROR,&error,&size);
        if (error) result = -1;
    }
    return result;
}
int EspProviderTlsPlatform::read_socket(void *context,uint8_t *bytes,size_t count,uint32_t timeout) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (self.socket_ < 0 || !self.wait_socket(false,clock(0)+timeout)) return -1;
    return lwip_recv(self.socket_,bytes,count,0);
}
int EspProviderTlsPlatform::write_socket(void *context,const uint8_t *bytes,size_t count,uint32_t timeout) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (self.socket_ < 0 || !self.wait_socket(true,clock(0)+timeout)) return -1;
    return lwip_send(self.socket_,bytes,count,0);
}
void EspProviderTlsPlatform::close_socket(void *context) {
    auto& self = *static_cast<EspProviderTlsPlatform*>(context);
    if (self.socket_ >= 0) { lwip_close(self.socket_); self.socket_ = -1; }
}
tls_stream_port EspProviderTlsPlatform::callbacks() {
    return {this,clock,time_ready,cancelled,entropy,trust,open_socket,read_socket,write_socket,close_socket};
}
