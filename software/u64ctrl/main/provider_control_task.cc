#include "sdkconfig.h"
#include "provider_control_task.h"
#ifdef CONFIG_ULTIMATE_PROVIDER_CONTROL
#include "../../network/provider_control.h"
#include "../../network/provider_data.h"
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
void run(void *context)
{
    provider_control::Worker *self = static_cast<provider_control::Worker *>(context);
    // No embedded TLS backend yet: reserved data requests fail unavailable.
    provider_data::Owner data_owner(data,*self);
    for (;;) {
        self->step(uint64_t(esp_timer_get_time() / 1000));
        data_owner.step(uint64_t(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    }
}
}
void provider_control_start(void)
{
    if (worker || port.queue) return;
    const uint8_t *boot = provider_boot_identity();
    if (!boot) return;
    provider_session::Identity identity; memcpy(identity.bytes, boot, sizeof(identity.bytes));
    port.queue = xQueueCreate(provider_control::QueueDepth, sizeof(provider_control::Event));
    if (!port.queue) return;
    // Delivery validation only. No backend is installed or capability promised.
    static provider_control::Worker instance(port, identity, 0, {1500, 5000, 1500});
    if (xTaskCreate(run, "Provider control", 4096, &instance, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
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
bool provider_control_receive(const uint8_t*, size_t) { return false; }
#endif
