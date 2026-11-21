/*
 * esp_flash_test.cpp
 *
 * "Flash into ESP32" (FileTypeESP :: execute): what a good image does, and what a file that
 * is empty, cut short or wrong does. A bad file must be refused with a message that says so
 * and must never be reported as flashed.
 */

#include "esp_env.h"

extern "C" int fw_printf(const char *, ...);

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
}

// One segment: "SEGM", length, address, then len bytes of a pattern
static void segment(std::vector<uint8_t> &v, uint32_t address, uint32_t len, uint8_t seed)
{
    put32(v, 0x4d474553);
    put32(v, len);
    put32(v, address);
    for (uint32_t i = 0; i < len; i++) v.push_back((uint8_t)(seed + i));
}

static SubsysResultCode_e run()
{
    UserInterface ui;
    SubsysCommand cmd;
    cmd.path = "/Usb0";
    cmd.filename = "app.esp";
    cmd.user_interface = &ui;
    return FileTypeESP::execute(&cmd);
}

static bool has(const char *s) { return world.log.find(s) != std::string::npos; }

TEST(EspFlash, good_image_flashes_every_segment_as_before)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    segment(img, 0x8000, 40, 7);
    set_image(img);
    run();
    CHECK(world.written.size() == 2, "two segments written");
    CHECK(world.written[0].address == 0x1000 && world.written[0].data.size() == 1500, "first segment");
    CHECK(world.written[1].address == 0x8000 && world.written[1].data.size() == 40, "second segment");
    CHECK(world.written[0].data[0] == 1 && world.written[0].data[1499] == (uint8_t)(1 + 1499), "first segment bytes");
    CHECK(world.written[1].data[39] == (uint8_t)(7 + 39), "second segment bytes");
    CHECK(has("[Flashing ESP32 Success!]"), "success shown");
    CHECK(has("{run}{start}"), "app restarted");
    CHECK(world.opens == 1 && world.closes == 1, "file closed");
}

TEST(EspFlash, good_image_leaks_nothing)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 100000, 1);
    segment(img, 0x20000, 5000, 3);
    set_image(img);
    run();
    CHECK(has("[Flashing ESP32 Success!]"), "success shown");
    CHECK(world.live_bytes == 0, "every segment buffer freed");
}

TEST(EspFlash, truncated_last_segment_is_refused_and_nothing_written)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    img.resize(img.size() - 300);   // the file ends inside the segment
    set_image(img);
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(has("[File is truncated.]"), "says the file is truncated");
    CHECK(world.written.empty(), "nothing written");
    CHECK(!has("{download}"), "the ESP32 never left its application");
    CHECK(world.opens == world.closes, "file closed");
    CHECK(world.live_bytes == 0, "no buffer left");
}

TEST(EspFlash, empty_file_is_refused)
{
    reset_world();
    set_image(std::vector<uint8_t>());
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(has("[File is empty.]"), "says the file is empty");
    CHECK(world.written.empty(), "nothing written");
    CHECK(world.opens == world.closes, "file closed");
}

TEST(EspFlash, bad_signature_in_second_segment_writes_nothing)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 100, 1);
    segment(img, 0x8000, 100, 2);
    img[112] = 'X';                 // the second header's signature
    set_image(img);
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(has("[Incorrect signature in file.]"), "says so");
    CHECK(world.written.empty(), "the good first segment is not written either");
}

TEST(EspFlash, trailing_bytes_less_than_a_header_are_refused)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 100, 1);
    img.push_back(0); img.push_back(0);
    set_image(img);
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(world.written.empty(), "nothing written");
}

TEST(EspFlash, file_that_reads_short_is_refused_not_flashed_with_garbage)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    set_image(img);
    world.content.resize(world.content.size() - 300);   // the file system promised more than it gives
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(world.live_bytes == 0, "no buffer left");
}

TEST(EspFlash, flash_that_does_not_verify_is_reported)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    set_image(img);
    world.verify_ok = false;
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(has("[Flashing Error -8]"), "says the flash does not hold the image");
    CHECK(has("{run}{start}"), "app restarted");
    CHECK(world.live_bytes == 0, "no buffer left");
}

TEST(EspFlash, verify_command_failure_is_reported)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    set_image(img);
    world.verify_status = -7;
    run();
    CHECK(!has("Success"), "not reported as success");
    CHECK(has("[Flashing Error -7]"), "reported");
}

TEST(EspFlash, flash_error_is_reported_and_frees_the_buffer)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 1500, 1);
    set_image(img);
    world.flash_status = -5;
    run();
    CHECK(has("[Flashing Error -5]"), "reported");
    CHECK(!has("Success"), "not success");
    CHECK(world.live_bytes == 0, "no buffer left");
}

TEST(EspFlash, no_download_mode_closes_file_and_restarts_the_app)
{
    reset_world();
    std::vector<uint8_t> img;
    segment(img, 0x1000, 100, 1);
    set_image(img);
    world.download_status = -1;
    SubsysResultCode_e r = run();
    CHECK(r == SSRET_INTERNAL_ERROR, "an error is returned");
    CHECK(has("[Could not switch to download mode.]"), "reported");
    CHECK(world.opens == world.closes, "file closed");
    CHECK(has("{run}{start}"), "the application is started again");
    CHECK(world.written.empty(), "nothing written");
}
