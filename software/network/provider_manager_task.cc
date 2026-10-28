#include "provider_manager_task.h"
#include "wifi.h"
#include "FreeRTOS.h"
#include "task.h"

using provider_control::ManagerRunner;
ProviderManagerTask::ProviderManagerTask(provider_control::IncarnationSource source,
    provider_control::ManagerLimits limits, uint32_t renew_ms) :
    link_(source, limits), runner_(link_.delivery, renew_ms), wifi_(0), state_(Unstarted), stop_(0),
    attempted_(false), valid_limits_(ManagerRunner::valid_limits(limits, renew_ms)) {}

bool ProviderManagerTask::start(WiFi& wifi)
{
    if (attempted_) return false;
    attempted_ = true;
    if (__atomic_load_n(&stop_, __ATOMIC_ACQUIRE)) { publish(Stopped); return false; }
    if (!valid_limits_ || !link_.delivery.enabled()) { publish(Disabled); return false; }
    wifi_ = &wifi;
    publish(Starting);
    TaskHandle_t task = 0;
    // The new task waits for notification before touching WiFi or the driver.
    if (xTaskCreate(entry, "Provider Owner", configMINIMAL_STACK_SIZE, this, PRIO_DRIVER, &task) != pdPASS) {
        publish(Failed); return false;
    }
    if (!wifi.BindProviderManager(link_)) {
        vTaskDelete(task); publish(Failed); return false;
    }
    xTaskNotifyGive(task);
    return true;
}
void ProviderManagerTask::request_stop()
{
    __atomic_store_n(&stop_, uint32_t(1), __ATOMIC_RELEASE);
    portENTER_CRITICAL(); link_.delivery.invalidate_isr(); portEXIT_CRITICAL();
}
void ProviderManagerTask::entry(void *context)
{
    while (!ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {}
    static_cast<ProviderManagerTask *>(context)->run();
    vTaskDelete(NULL);
    for (;;) {} // FreeRTOS must not return from self deletion.
}
void ProviderManagerTask::run()
{
    static_assert(sizeof(TickType_t) == sizeof(uint32_t), "Provider clock requires 32-bit ticks");
    static_assert(portTICK_PERIOD_MS > 0, "Provider clock requires millisecond ticks");
    const TickType_t interval = pdMS_TO_TICKS(PollMs) ? pdMS_TO_TICKS(PollMs) : 1;
    uint32_t previous = xTaskGetTickCount();
    uint64_t now = 0;
    publish(WaitingForBoot);
    for (;;) {
        if (__atomic_load_n(&stop_, __ATOMIC_ACQUIRE)) { runner_.stop(); publish(Stopped); return; }
        uint32_t tick = xTaskGetTickCount(), delta = tick - previous;
        // Extend normal tick wrap, but reject backwards/ambiguous gaps rather
        // than resetting deadlines. The active task must run at least half-wrap.
        uint64_t elapsed = uint64_t(delta) * portTICK_PERIOD_MS;
        if (delta > UINT32_MAX / 2 || elapsed > UINT64_MAX - now) {
            runner_.stop(); publish(Failed); return;
        }
        previous = tick; now += elapsed;
        if (state() == WaitingForBoot) {
            provider_session::Identity boot = {}; OperationCancellation lifetime;
            if (now >= BootstrapMs) { runner_.stop(); publish(Failed); return; }
            if (wifi_->ProviderSnapshot(boot, lifetime)) {
                // Stop may race the snapshot. Serialize the final check and
                // zero-wait offer with task-context cancellation writers.
                portENTER_CRITICAL();
                bool stopped = __atomic_load_n(&stop_, __ATOMIC_ACQUIRE);
                bool started = !stopped && runner_.start(boot, lifetime, now);
                portEXIT_CRITICAL();
                if (stopped) { runner_.stop(); publish(Stopped); return; }
                if (!started) { publish(Failed); return; }
                publish(Negotiating);
            }
        } else {
            runner_.step(now);
            if (runner_.state() == ManagerRunner::Failed || runner_.state() == ManagerRunner::Disabled) {
                publish(Failed); return;
            }
            if (runner_.state() == ManagerRunner::Active) publish(Active);
        }
        vTaskDelay(interval);
    }
}
