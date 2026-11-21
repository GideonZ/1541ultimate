/*
 * esp_md5.h
 *
 * What it takes to check a flashed image against the ESP32's own MD5 of the flash
 * (SPI_FLASH_MD5, 0x13 of the ROM loader): MD5 of the image as the Ultimate holds it, and the
 * comparison with the loader's reply. Header only, no dependencies, so that a host test can
 * include it.
 */
#ifndef ESP_MD5_H_
#define ESP_MD5_H_

#include <stdint.h>
#include <string.h>

#define ESP_SPI_FLASH_MD5 0x13

struct EspMd5
{
    uint32_t a, b, c, d;
    uint64_t total;
    uint8_t  buf[64];
    uint32_t fill;

    static uint32_t rol(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }

    void block(const uint8_t *p)
    {
        static const uint8_t S[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            M[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) | ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
        }
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; i++) {
            uint32_t F;
            int g;
            if (i < 16)      { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) & 15; }
            else if (i < 48) { F = B ^ C ^ D;          g = (3 * i + 5) & 15; }
            else             { F = C ^ (B | ~D);       g = (7 * i) & 15; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + rol(F, S[i]);
        }
        a += A; b += B; c += C; d += D;
    }

    EspMd5() { a = 0x67452301; b = 0xefcdab89; c = 0x98badcfe; d = 0x10325476; total = 0; fill = 0; }

    void update(const uint8_t *p, uint32_t len)
    {
        total += len;
        while (len) {
            uint32_t n = 64 - fill;
            if (n > len) n = len;
            memcpy(buf + fill, p, n);
            fill += n; p += n; len -= n;
            if (fill == 64) { block(buf); fill = 0; }
        }
    }

    void final(uint8_t out[16])
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (fill != 56) update(&pad, 1);
        uint8_t lenb[8];
        for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (8 * i));
        update(lenb, 8);
        uint32_t v[4] = { a, b, c, d };
        for (int i = 0; i < 16; i++) out[i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
    }
};

static inline int esp_hex_value(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// The whole reply to SPI_FLASH_MD5 as the loader sends it (rb: the frame from the direction
// byte on; rblen bytes): direction 1, opcode 0x13, size (LE), value (4), then <size> bytes of
// data. Those are the MD5 (32 hex digits from a ROM loader, 16 raw bytes from a stub), two
// status bytes (status, error) and, on every loader but the ESP8266's, two reserved bytes.
// An error reply holds just the status bytes (and the reserved pair).
// Returns 0 when the reply says the flash holds this digest, -7 when the reply is not an
// answer (error status, malformed, too short), -8 when the digest differs.
#define ESP_VERIFY_NO_ANSWER -7
#define ESP_VERIFY_MISMATCH  -8
static inline int esp_md5_parse_reply(const uint8_t *rb, int rblen, const uint8_t digest[16]);

// Does the data of a SPI_FLASH_MD5 reply (dataLen bytes, without the two status bytes) hold
// this digest? The ROM loader answers with 32 hex digits, the stub loader with 16 raw bytes.
static inline bool esp_md5_reply_matches(const uint8_t *data, int dataLen, const uint8_t digest[16])
{
    if (dataLen == 16) {
        return memcmp(data, digest, 16) == 0;
    }
    if (dataLen == 32) {
        for (int i = 0; i < 16; i++) {
            int hi = esp_hex_value(data[2 * i]), lo = esp_hex_value(data[2 * i + 1]);
            if (hi < 0 || lo < 0 || ((hi << 4) | lo) != digest[i]) return false;
        }
        return true;
    }
    return false;
}

static inline int esp_md5_parse_reply(const uint8_t *rb, int rblen, const uint8_t digest[16])
{
    if (rblen < 10 || rb[0] != 1 || rb[1] != ESP_SPI_FLASH_MD5) {
        return ESP_VERIFY_NO_ANSWER;
    }
    int size = rb[2] | (rb[3] << 8);
    if (size + 8 > rblen) {
        return ESP_VERIFY_NO_ANSWER;
    }
    int dlen;
    switch (size) {
    case 34: case 36: dlen = 32; break;   // hex digest, status, (reserved)
    case 18: case 20: dlen = 16; break;   // raw digest, status, (reserved)
    default: return ESP_VERIFY_NO_ANSWER; // 2 or 4: an error reply; anything else is not ours
    }
    if (rb[8 + dlen] != 0) {              // status byte after the data
        return ESP_VERIFY_NO_ANSWER;
    }
    return esp_md5_reply_matches(rb + 8, dlen, digest) ? 0 : ESP_VERIFY_MISMATCH;
}

#endif /* ESP_MD5_H_ */
