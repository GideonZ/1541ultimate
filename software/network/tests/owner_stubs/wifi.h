#ifndef TEST_OWNER_WIFI_H
#define TEST_OWNER_WIFI_H
#include "dma_uart.h"
#include "provider_manager_uart.h"
#include <functional>
class WiFi {
public:
    DmaUART uart;
    ProviderManagerUart *bound = 0;
    provider_session::Identity boot = {};
    CancellationSource lifetime;
    bool online = true, bind_ok = true;
    unsigned binds = 0, snapshots = 0;
    std::function<void()> snapshot_hook = []{};
    WiFi() { boot.bytes[0] = 1; }
    bool BindProviderManager(ProviderManagerUart& link) {
        ++binds;
        if (!bind_ok || bound || !link.bind_uart(&uart)) return false;
        bound = &link; return true;
    }
    bool ProviderSnapshot(provider_session::Identity& value, OperationCancellation& token) {
        ++snapshots; if (!online) return false;
        portENTER_CRITICAL(); value = boot; token = lifetime.token(); portEXIT_CRITICAL(); snapshot_hook(); return true;
    }
};
#endif
