/*
 * esp32_verify_test.cpp
 *
 * Esp32 :: VerifyFlash and Esp32 :: Command (software/io/wifi/esp32.cc), cut out of the file as
 * they are (see the Makefile), against a UART that records what is sent and answers with the
 * frames a case scripts: the reply of the ROM loader to SPI_FLASH_MD5 and the ways it can go
 * wrong (the digest of other data, an error status, a short frame, another opcode, silence).
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../io/usb/tests/host_test/host_test.h"
#define CHECK(c, msg) EXPECT_TRUE((c) && (msg))

#include "esp32.h"
#include "esp_md5.h"

typedef std::vector<uint8_t> Bytes;

static uint8_t receiveBuffer[512];
Esp32::Esp32() { }
static void dump_hex_relative(const void *, int) { }
#include "output/esp32_funcs.cc"

static Bytes md5_of(const Bytes &b, size_t len)
{
    EspMd5 m;
    m.update(b.data(), len);
    Bytes d(16);
    m.final(d.data());
    return d;
}

// The reply of the loader: direction 1, opcode, size, value, <data>, then the status bytes
static Bytes reply(uint8_t opcode, const Bytes &data, const Bytes &status)
{
    Bytes f(8, 0);
    f[0] = 1; f[1] = opcode;
    f.insert(f.end(), data.begin(), data.end());
    f.insert(f.end(), status.begin(), status.end());
    f[2] = (f.size() - 8) & 0xFF; f[3] = (f.size() - 8) >> 8;
    return f;
}

static Bytes hex_of(const Bytes &d)
{
    char h[33];
    for (int i = 0; i < 16; i++) snprintf(h + 2 * i, 3, "%02x", d[i]);
    return Bytes(h, h + 32);
}

static Bytes image(size_t n, uint8_t seed)
{
    Bytes b(n);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + 7 * i);
    return b;
}

static const Bytes OK4 = { 0, 0, 0, 0 };

struct Rig {
    DmaUART uart;
    Esp32 esp;
    Rig() { esp.uart = &uart; }
    int verify(const Bytes &img, size_t len = 0, uint32_t address = 0x10000) {
        return esp.VerifyFlash(img.data(), address, len ? len : img.size());
    }
};

TEST(EspVerifyFlash, flash_that_holds_the_image_verifies)
{
    Rig r;
    Bytes img = image(3000, 1);
    r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, img.size())), OK4));
    CHECK(r.verify(img) == 0, "ROM loader: 32 hex digits, status, reserved");
}

TEST(EspVerifyFlash, reply_forms_all_verify)
{
    Bytes img = image(100, 2);
    Bytes d = md5_of(img, img.size());
    {
        Rig r;
        r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(d), { 0, 0 }));
        CHECK(r.verify(img) == 0, "32 hex digits and two status bytes");
    }
    {
        Rig r;
        r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, d, OK4));
        CHECK(r.verify(img) == 0, "stub loader: 16 raw bytes");
    }
}

TEST(EspVerifyFlash, flash_that_holds_other_data_does_not_verify)
{
    Rig r;
    Bytes img = image(3000, 1);
    Bytes flash = img;
    flash[2999] ^= 1;                           // one bit of the last byte
    r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(flash, flash.size())), OK4));
    CHECK(r.verify(img) == ESP_VERIFY_MISMATCH, "a different digest is a mismatch");
}

TEST(EspVerifyFlash, digest_covers_the_length_asked_not_the_buffer)
{
    Bytes img = image(100, 3);
    {
        Rig r;
        r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, 60)), OK4));
        CHECK(r.verify(img, 60) == 0, "MD5 of the first 60 bytes");
    }
    {
        Rig r;
        r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, 100)), OK4));
        CHECK(r.verify(img, 60) == ESP_VERIFY_MISMATCH, "MD5 of 100 bytes is not that of 60");
    }
}

TEST(EspVerifyFlash, asks_for_the_md5_of_the_range)
{
    Rig r;
    Bytes img = image(6000, 4);
    r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, img.size())), OK4));
    r.verify(img, 0, 0x120000);
    CHECK(r.uart.sent.size() == 1, "one command sent");
    const Bytes &s = r.uart.sent[0];
    CHECK(s.size() == 24, "8 header bytes and 16 parameter bytes");
    CHECK(s[0] == 0 && s[1] == ESP_SPI_FLASH_MD5, "request, SPI_FLASH_MD5");
    CHECK(s[2] == 16 && s[3] == 0, "16 bytes of parameters");
    CHECK(s[8] == 0x00 && s[9] == 0x00 && s[10] == 0x12 && s[11] == 0x00, "address 0x120000, little endian");
    CHECK(s[12] == 0x70 && s[13] == 0x17 && s[14] == 0 && s[15] == 0, "length 6000");
    CHECK(r.uart.last_timeout == 1000 + 6000 / 600, "time allowed grows with the length");
}

TEST(EspVerifyFlash, error_reply_is_no_answer)
{
    Rig r;
    Bytes img = image(100, 5);
    r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, Bytes(), { 1, 5, 0, 0 }));
    CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "status 1, error 5");
}

TEST(EspVerifyFlash, short_reply_is_no_answer)
{
    Rig r;
    Bytes img = image(100, 6);
    Bytes f = reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, 100)), OK4);
    f.resize(20);                               // the size field still says 36
    r.uart.replies.push_back(f);
    CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "frame shorter than its size field");
}

TEST(EspVerifyFlash, short_reply_after_a_full_one_does_not_read_the_old_bytes)
{
    Rig r;
    Bytes img = image(100, 7);
    Bytes good = reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, 100)), OK4);
    r.uart.replies.push_back(good);
    CHECK(r.verify(img) == 0, "full reply first");
    Bytes f = good;
    f.resize(20);                               // the buffer still holds the rest of the full reply
    r.uart.replies.push_back(f);
    CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "only the bytes of this reply count");
}

TEST(EspVerifyFlash, silence_is_no_answer)
{
    Rig r;
    Bytes img = image(100, 8);
    CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "the module never replies");
    CHECK(r.uart.sent.size() == 1, "the command was sent");
}

TEST(EspVerifyFlash, no_buffer_is_no_answer_and_nothing_is_sent)
{
    Rig r;
    Bytes img = image(100, 9);
    r.uart.no_buffer = true;
    CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "no transmit buffer");
    CHECK(r.uart.sent.empty(), "nothing sent");
}

TEST(EspVerifyFlash, reply_to_another_command_is_skipped)
{
    Bytes img = image(100, 10);
    Bytes good = reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(img, 100)), OK4);
    {
        Rig r;
        r.uart.replies.push_back(reply(0x03, Bytes(), { 0, 0 }));
        r.uart.replies.push_back(good);
        CHECK(r.verify(img) == 0, "the answer after a stray reply is used");
    }
    {
        Rig r;
        r.uart.replies.push_back(reply(0x03, Bytes(), { 0, 0 }));
        CHECK(r.verify(img) == ESP_VERIFY_NO_ANSWER, "a stray reply alone is no answer");
    }
}

TEST(EspCommand, replySize_returns_the_reply_as_it_came)
{
    Rig r;
    Bytes data(16, 0);
    Bytes f = reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(image(10, 1), 10)), { 1, 5, 0, 0 });
    r.uart.replies.push_back(f);
    uint8_t rb[512];
    int size = -1;
    CHECK(r.esp.Command(ESP_SPI_FLASH_MD5, 16, 0, data.data(), rb, 100, &size), "true, whatever the status says");
    CHECK(size == (int)f.size(), "the size of the frame");
    CHECK(memcmp(rb, f.data(), f.size()) == 0, "the frame is in the buffer");
}

TEST(EspCommand, without_replySize_the_status_decides)
{
    Bytes data(16, 0);
    uint8_t rb[512];
    {
        Rig r;
        r.uart.replies.push_back(reply(0x0B, Bytes(), { 0, 0, 0, 0 }));
        CHECK(r.esp.Command(0x0B, 16, 0, data.data(), rb, 100), "size 4, status 0");
    }
    {
        Rig r;
        r.uart.replies.push_back(reply(0x0B, Bytes(), { 1, 5, 0, 0 }));
        CHECK(!r.esp.Command(0x0B, 16, 0, data.data(), rb, 100), "size 4, status 1");
    }
    {
        Rig r;
        r.uart.replies.push_back(reply(ESP_SPI_FLASH_MD5, hex_of(md5_of(image(10, 1), 10)), OK4));
        CHECK(!r.esp.Command(ESP_SPI_FLASH_MD5, 16, 0, data.data(), rb, 100), "size 36 is not a status reply");
    }
}

TEST(EspCommand, no_reply_is_false_and_leaves_replySize_alone)
{
    Rig r;
    Bytes data(16, 0);
    uint8_t rb[512];
    int size = 1234;
    CHECK(!r.esp.Command(ESP_SPI_FLASH_MD5, 16, 0, data.data(), rb, 100, &size), "timeout");
    CHECK(size == 1234, "replySize not set");
}

TEST(EspCommand, request_direction_in_a_reply_is_skipped)
{
    Rig r;
    Bytes data(16, 0);
    uint8_t rb[512];
    int size = 0;
    Bytes echo = reply(0x0B, Bytes(), { 0, 0, 0, 0 });
    echo[0] = 0;                                // a request, not a response
    r.uart.replies.push_back(echo);
    CHECK(!r.esp.Command(0x0B, 16, 0, data.data(), rb, 100, &size), "not accepted as the answer");
}
