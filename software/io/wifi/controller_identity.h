#ifndef CONTROLLER_IDENTITY_H
#define CONTROLLER_IDENTITY_H
#include <stdint.h>
#include <stddef.h>

// CMD_IDENTIFY has no esp_err field. The legacy response has three padding
// bytes after its terminated string (sizeof(rpc_identify_resp) includes padding).
struct ControllerIdentity {
    uint16_t major, minor;
    char name[64];
};

inline bool controller_identity_parse(const uint8_t *data, size_t size,
                                      uint8_t thread, uint16_t sequence,
                                      ControllerIdentity &out)
{
    out = ControllerIdentity{};
    if (!data || size < 10 || size > 75 || data[0] != 2 || data[1] != thread ||
        data[2] != uint8_t(sequence) || data[3] != uint8_t(sequence >> 8)) return false;
    size_t end = 8;
    while (end < size && data[end]) {
        if (end - 8 >= sizeof(out.name) - 1 || data[end] < 32 || data[end] > 126) return false;
        ++end;
    }
    if (end == size || end == 8 || (size != end + 1 && size != end + 4)) return false;
    const uint16_t major = uint16_t(data[4]) | uint16_t(data[5]) << 8;
    const uint16_t minor = uint16_t(data[6]) | uint16_t(data[7]) << 8;
    if (major == 0 || major == 65535 || minor == 65535) return false;
    const char prefix[] = "ESP32 WiFi Bridge V";
    size_t cursor = 8;
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
        if (cursor >= end || data[cursor++] != uint8_t(prefix[i])) return false;
    for (unsigned field = 0; field < 2; ++field) {
        unsigned value = 0, digits = 0;
        while (cursor < end && data[cursor] >= '0' && data[cursor] <= '9') {
            if (++digits > 5) return false;
            value = value * 10 + data[cursor++] - '0';
        }
        if (!digits || value != (field ? minor : major)) return false;
        if (!field && (cursor >= end || data[cursor++] != '.')) return false;
    }
    if (cursor != end) return false;
    out.major = major;
    out.minor = minor;
    for (size_t i = 8; i < end; ++i) out.name[i - 8] = char(data[i]);
    return true;
}

enum class ControllerInstallDecision { Unknown, Older, EqualUnverified, Newer };
inline ControllerInstallDecision controller_install_decision(bool identified,
    const ControllerIdentity &id, uint16_t target_major, uint16_t target_minor)
{
    if (!identified) return ControllerInstallDecision::Unknown;
    if (id.major < target_major || (id.major == target_major && id.minor < target_minor))
        return ControllerInstallDecision::Older;
    if (id.major == target_major && id.minor == target_minor)
        return ControllerInstallDecision::EqualUnverified;
    return ControllerInstallDecision::Newer;
}
#endif
