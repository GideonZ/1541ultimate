/*
 * md5_test.cpp
 *
 * The MD5 the Ultimate computes of an image, and the comparison with the ESP32 loader's
 * SPI_FLASH_MD5 reply (esp_md5.h).
 */

#include "esp_env.h"
#include "esp_md5.h"
#include <algorithm>
#include <string>

static std::string md5_hex(const std::string &s, size_t chunk)
{
    EspMd5 m;
    for (size_t i = 0; i < s.size(); i += chunk) {
        m.update((const uint8_t *)s.data() + i, (uint32_t)std::min(chunk, s.size() - i));
    }
    uint8_t d[16];
    m.final(d);
    char out[33];
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    return out;
}

TEST(EspMd5, rfc1321_vectors)
{
    CHECK(md5_hex("", 1) == "d41d8cd98f00b204e9800998ecf8427e", "empty");
    CHECK(md5_hex("abc", 1) == "900150983cd24fb0d6963f7d28e17f72", "abc");
    CHECK(md5_hex("message digest", 5) == "f96b697d7cb7938d525a2f31aaf161d0", "message digest");
    CHECK(md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 7)
          == "57edf4a22be3c955ac49da2e2107b67a", "80 digits, over a block boundary");
}

TEST(EspMd5, reply_hex_and_raw)
{
    EspMd5 m;
    m.update((const uint8_t *)"abc", 3);
    uint8_t d[16];
    m.final(d);
    const char *hex = "900150983cd24fb0d6963f7d28e17f72";
    CHECK(esp_md5_reply_matches((const uint8_t *)hex, 32, d), "ROM loader: 32 hex digits");
    CHECK(esp_md5_reply_matches((const uint8_t *)"900150983CD24FB0D6963F7D28E17F72", 32, d), "upper case");
    CHECK(esp_md5_reply_matches(d, 16, d), "stub loader: 16 raw bytes");
    const char *bad = "900150983cd24fb0d6963f7d28e17f73";
    CHECK(!esp_md5_reply_matches((const uint8_t *)bad, 32, d), "one digit off");
    CHECK(!esp_md5_reply_matches((const uint8_t *)hex, 31, d), "wrong length");
    CHECK(!esp_md5_reply_matches((const uint8_t *)"zz0150983cd24fb0d6963f7d28e17f72", 32, d), "not hex");
}
