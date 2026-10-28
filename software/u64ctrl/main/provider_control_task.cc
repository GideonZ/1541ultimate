#include "sdkconfig.h"
#include "provider_control_task.h"
#ifdef CONFIG_ULTIMATE_PROVIDER_CONTROL
#include "../../network/provider_control.h"
#include "../../network/provider_data.h"
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
#include "provider_tls_platform.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
extern "C" {
#include "my_uart.h"
#include "provider_boot.h"
}

namespace {
class UartPort : public provider_control::Port, public provider_data::Port {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
public:
    QueueHandle_t queue = NULL;
    void lock() override { portENTER_CRITICAL(&mux); }
    void unlock() override { portEXIT_CRITICAL(&mux); }
    bool enqueue(const provider_control::Event& event) override { return xQueueSend(queue, &event, 0) == pdTRUE; }
    bool dequeue(provider_control::Event& event) override { return xQueueReceive(queue, &event, 0) == pdTRUE; }
    bool send(const uint8_t *bytes, size_t size) override {
        command_buf_t *buffer;
        if (my_uart_get_buffer(1, &buffer, 0) != pdTRUE) return false;
        memcpy(buffer->data, bytes, size); buffer->size = size;
        return my_uart_try_transmit_packet(1, buffer) == pdTRUE;
    }
};
UartPort port;
provider_control::Worker *worker;
provider_data::Delivery data(port,false);
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
EspProviderTlsPlatform tls_platform;
#endif
void run(void *context)
{
    provider_control::Worker *self = static_cast<provider_control::Worker *>(context);
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
    provider_data::TlsBackend tls_backend(*self,tls_platform);
    provider_data::Owner data_owner(data,*self,tls_platform.configured() ? &tls_backend : 0);
#else
    provider_data::Owner data_owner(data,*self);
#endif
    for (;;) {
        self->step(uint64_t(esp_timer_get_time() / 1000));
        data_owner.step(uint64_t(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    }
}
}
bool provider_control_configure_tls(const unsigned char *roots,size_t size,bool (*clock_ready)(),bool (*entropy_ready)())
{
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
    return !worker && !port.queue && tls_platform.configure(roots,size,clock_ready,entropy_ready);
#else
    (void)roots; (void)size; (void)clock_ready; (void)entropy_ready; return false;
#endif
}
void provider_control_start(void)
{
    if (worker || port.queue) return;
    const uint8_t *boot = provider_boot_identity();
    if (!boot) return;
    provider_session::Identity identity; memcpy(identity.bytes, boot, sizeof(identity.bytes));
    port.queue = xQueueCreate(provider_control::QueueDepth, sizeof(provider_control::Event));
    if (!port.queue) return;
    unsigned features = 0;
    unsigned stack_bytes = 4096;
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
    if (tls_platform.configured()) features = provider_session::Required;
    stack_bytes = 12288; // Initial TLS budget; measure high-water before activation.
#endif
    static provider_control::Worker instance(port, identity, features, {1500, 5000, 1500});
    if (xTaskCreate(run, "Provider control", stack_bytes, &instance, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        vQueueDelete(port.queue); port.queue = NULL; return;
    }
    worker = &instance; // Published before the dispatcher starts; immutable thereafter.
}
bool provider_control_receive(const uint8_t *bytes, size_t size)
{
    if (size < 2 || bytes[0] != 2 || (bytes[1] != 0xfe && bytes[1] != 0xfd)) return false;
    if (worker) {
        if (bytes[1] == 0xfd) {
            port.lock(); bool accepted = data.receive_isr(bytes,size,uint32_t(esp_timer_get_time()/1000)); port.unlock();
            if (!accepted) worker->disconnect();
        } else if (!worker->submit(bytes, size)) {
            data.disconnect(); worker->disconnect();
        }
    }
    return true;
}
#else
void provider_control_start(void) {}
bool provider_control_configure_tls(const unsigned char*,size_t,bool (*)(),bool (*)()) { return false; }
bool provider_control_receive(const uint8_t*, size_t) { return false; }
#endif
