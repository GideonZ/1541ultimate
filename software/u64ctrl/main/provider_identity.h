#ifndef PROVIDER_IDENTITY_H
#define PROVIDER_IDENTITY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Optional CMD_IDENTIFY extension. Byte encoding is independent of struct
// padding and endianness. Legacy requests and their replies are unchanged.
#define PROVIDER_PROBE_SIZE 16
#define PROVIDER_ID_SIZE 16
#define PROVIDER_IDENTITY_TAIL 32
#define PROVIDER_NAME_LIMIT 64

static inline int provider_probe_cookie(const uint8_t *packet, size_t size, uint8_t cookie[8])
{
    if (size != PROVIDER_PROBE_SIZE || packet[0] != 2 || packet[1] != 0xff || packet[2] || packet[3] ||
        memcmp(packet + 4, "PID1", 4)) return 0;
    memcpy(cookie, packet + 8, 8);
    return 1;
}

static inline size_t provider_identity_append(uint8_t *packet, size_t capacity,
    size_t name_end, const uint8_t cookie[8], const uint8_t boot[PROVIDER_ID_SIZE])
{
    if (name_end > capacity || capacity - name_end < PROVIDER_IDENTITY_TAIL) return 0;
    uint8_t *tail = packet + name_end;
    memcpy(tail, "PID1", 4);
    tail[4] = 1; // identity format, not a TLS capability version
    memset(tail + 5, 0, 3);
    memcpy(tail + 8, cookie, 8);
    memcpy(tail + 16, boot, PROVIDER_ID_SIZE);
    return name_end + PROVIDER_IDENTITY_TAIL;
}

static inline int provider_identity_read(const uint8_t *packet, size_t size,
    const uint8_t cookie[8], uint8_t boot[PROVIDER_ID_SIZE])
{
    if (size < 9 || size > 8 + PROVIDER_NAME_LIMIT + PROVIDER_IDENTITY_TAIL ||
        packet[0] != 2 || packet[1] != 0xff || packet[2] || packet[3]) return 0;
    const uint8_t *end = (const uint8_t *)memchr(packet + 8, 0,
        size - 8 < PROVIDER_NAME_LIMIT ? size - 8 : PROVIDER_NAME_LIMIT);
    if (!end || (size_t)(packet + size - end - 1) != PROVIDER_IDENTITY_TAIL) return 0;
    const uint8_t *tail = end + 1;
    if (memcmp(tail, "PID1", 4) || tail[4] != 1 || tail[5] || tail[6] || tail[7] ||
        memcmp(tail + 8, cookie, 8)) return 0;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < PROVIDER_ID_SIZE; ++i) nonzero |= tail[16 + i];
    if (!nonzero) return 0;
    memcpy(boot, tail + 16, PROVIDER_ID_SIZE);
    return 1;
}

#endif
