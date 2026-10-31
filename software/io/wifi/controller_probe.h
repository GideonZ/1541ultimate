#ifndef CONTROLLER_PROBE_H
#define CONTROLLER_PROBE_H
#include "controller_identity.h"

enum class ControllerProbeResult { Identified, UnsupportedUart, Timeout, Malformed, Overflow };

// IO supplies register access and dedicated DMA buffers. This does not change
// baud/module pins, send bootloader commands, or invoke the firmware's tasks.
template<class IO> ControllerProbeResult controller_probe(IO &io, ControllerIdentity &id)
{
    id = ControllerIdentity{};
    const uint8_t flow = io.read(3);
    // C64U normal run: module bits and route are zero, SLIP and flow control on.
    // Only the already established 5 Mbaud session is supported; never sweep.
    if (flow != 5 || io.read(0) != 19 || io.read(1) != 0)
        return ControllerProbeResult::UnsupportedUart;
    io.write(3, flow | 128); // reset local DMA only; preserve module pins
    io.write(2, 15);        // interrupts off, clear overflow
    const uint16_t started = io.milliseconds();
    const uint16_t sequence = uint16_t(started ^ 0xd137);
    uint8_t *request = io.tx();
    request[0] = 2; request[1] = 0xfe;
    request[2] = uint8_t(sequence); request[3] = uint8_t(sequence >> 8);
    bool sent = false, receiving = false;
    ControllerProbeResult result = ControllerProbeResult::Timeout;
    // Absolute 2 s budget and a finite poll bound even if the timer stops.
    for (unsigned polls = 0; polls < 2000000; ++polls) {
        if (uint16_t(io.milliseconds() - started) >= 2000) break;
        const uint8_t status = io.read(2);
        if (status & 8) { result = ControllerProbeResult::Overflow; break; }
        if (!receiving && (status & 128)) { io.receive(); receiving = true; }
        if (!sent && (status & 64)) { io.submit(4); sent = true; }
        if (status & 32) {
            const unsigned length = unsigned(io.read(8)) | unsigned(io.read(9)) << 8;
            const uint8_t *data = io.rx();
            // Unrelated events and stale replies cannot satisfy this request.
            if (sent && length >= 4 && data[1] == 0xfe &&
                data[2] == uint8_t(sequence) && data[3] == uint8_t(sequence >> 8)) {
                result = controller_identity_parse(data, length, 0xfe, sequence, id)
                    ? ControllerProbeResult::Identified : ControllerProbeResult::Malformed;
                io.write(11, 1);
                break;
            }
            io.write(11, 1);
            receiving = false;
        }
    }
    io.write(3, flow | 128); // stop diagnostic DMA before displaying the result
    io.write(2, 15);
    return result;
}
#endif
