#ifndef PROVIDER_MANAGER_TASK_H
#define PROVIDER_MANAGER_TASK_H
#include "provider_manager_uart.h"
#include "provider_manager_runner.h"
class WiFi;

// Optional management task. No default instance or weak identity fallback.
// Construct with a qualified fresh incarnation source, then start once from a
// serialized control task. This object must have firmware lifetime: WiFi keeps
// its UART binding even after the task exits. Session/data-plane requests will
// need owner-task delivery; do not expose the runner to other tasks.
class ProviderManagerTask {
public:
    enum State { Unstarted, Disabled, Starting, WaitingForBoot, Negotiating, Active, Stopped, Failed };
    enum { PollMs = 10, BootstrapMs = 1500 };
    ProviderManagerTask(provider_control::IncarnationSource, provider_control::ManagerLimits, uint32_t renew_ms);
    bool start(WiFi&); // True means launched, not ready. No retry on failure.
    void request_stop(); // Task context; invalidates delivery before the owner wakes.
    State state() const { return static_cast<State>(__atomic_load_n(&state_, __ATOMIC_ACQUIRE)); }
private:
    ProviderManagerUart link_;
    provider_control::ManagerRunner runner_;
    WiFi *wifi_;
    alignas(4) uint32_t state_, stop_;
    bool attempted_, valid_limits_;
    void publish(State s) { __atomic_store_n(&state_, uint32_t(s), __ATOMIC_RELEASE); }
    static void entry(void*);
    void run();
    ProviderManagerTask(const ProviderManagerTask&) = delete;
    ProviderManagerTask& operator=(const ProviderManagerTask&) = delete;
};
#endif
