#ifndef TEST_MANAGER_DMA_UART_H
#define TEST_MANAGER_DMA_UART_H
#include <stdint.h>
#include <vector>
#include <array>
#include <assert.h>
extern unsigned manager_critical_depth;
#define portENTER_CRITICAL() (++manager_critical_depth)
#define portEXIT_CRITICAL() do { assert(manager_critical_depth); --manager_critical_depth; } while (0)
enum { pdTRUE = 1, pdFALSE = 0 };
struct command_buf_t { uint8_t data[1552]; int size; };
class DmaUART {
public:
    command_buf_t buffer = {};
    bool available = true, transmit_ok = true;
    unsigned gets = 0, consumed = 0;
    std::vector<std::array<uint8_t, 76>> sent;
    int GetBuffer(command_buf_t **out, unsigned wait) {
        assert(manager_critical_depth && wait == 0); ++gets;
        if (!available) return pdFALSE;
        *out = &buffer; return pdTRUE;
    }
    int TryTransmitPacket(command_buf_t *buf) {
        assert(manager_critical_depth && buf == &buffer && buf->size == 76);
        ++consumed;
        if (!transmit_ok) return pdFALSE;
        std::array<uint8_t,76> value;
        for (unsigned i = 0; i < value.size(); ++i) value[i] = buf->data[i];
        sent.push_back(value); return pdTRUE;
    }
};
#endif
