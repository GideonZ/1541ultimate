#ifndef PROVIDER_MANAGER_UART_H
#define PROVIDER_MANAGER_UART_H
#include "provider_manager_delivery.h"
class DmaUART;
// Lifetime must cover the WiFi binding and all issued tokens. No task is created;
// a single owner calls delivery.step() and request methods. Binding is one-shot.
class ProviderManagerUart : private provider_control::ManagerPort {
    DmaUART *uart_;
    void lock() override;
    void unlock() override;
    bool send(const uint8_t*, size_t) override;
public:
    provider_control::ManagerDelivery delivery;
    ProviderManagerUart(provider_control::IncarnationSource, provider_control::ManagerLimits);
    bool bind_uart(DmaUART*);
    bool receive_isr(const uint8_t*, size_t); // True means reserved frame consumed, including malformed frames.
};
#endif
