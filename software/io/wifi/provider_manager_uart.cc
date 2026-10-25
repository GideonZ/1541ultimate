#include "provider_manager_uart.h"
#include "dma_uart.h"

ProviderManagerUart::ProviderManagerUart(provider_control::IncarnationSource source, provider_control::ManagerLimits limits) :
    uart_(0), delivery(*this, source, limits) {}
void ProviderManagerUart::lock() { portENTER_CRITICAL(); }
void ProviderManagerUart::unlock() { portEXIT_CRITICAL(); }
bool ProviderManagerUart::bind_uart(DmaUART *uart)
{
    if (!uart || !delivery.enabled()) return false;
    lock();
    bool accepted = !uart_;
    if (accepted) uart_ = uart;
    unlock();
    return accepted;
}
bool ProviderManagerUart::send(const uint8_t *bytes, size_t size)
{
    if (!uart_ || size != provider_control::FrameSize) return false;
    command_buf_t *buffer;
    if (uart_->GetBuffer(&buffer, 0) != pdTRUE) return false;
    memcpy(buffer->data, bytes, size); buffer->size = size;
    return uart_->TryTransmitPacket(buffer) == pdTRUE;
}
bool ProviderManagerUart::receive_isr(const uint8_t *bytes, size_t size)
{
    if (size < 2 || bytes[0] != 2 || bytes[1] != 0xfe) return false;
    delivery.receive_isr(bytes, size);
    return true;
}
