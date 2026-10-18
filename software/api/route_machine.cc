#include "routes.h"
#include "attachment_writer.h"
#include "pattern.h"
#include "subsys.h"
#include "c64.h"
#include "c64_subsys.h"
#include "userinterface.h"
#include "filemanager.h"
#if U64
#include "u64.h"
#if !defined(RECOVERYAPP)
#include "u64_config.h"
#endif
#include "keyboard_usb.h"
#include "joystick_output.h"
extern "C" void route_input_note_menu_button(void);
extern "C" void route_input_release_rest_mouse(void);
extern "C" bool push_active_menu_button(void) __attribute__((weak));
#endif

#define MENU_C64_PAUSE      0x640B
#define MENU_C64_RESUME     0x640C

static uint8_t chartohex(const char a)
{
    if ((a >= '0') && (a <= '9'))
        return a - '0';
    if ((a >= 'A') && (a <= 'F'))
        return 10 + a - 'A';
    if ((a >= 'a') && (a <= 'f'))
        return 10 + a - 'a';
    return 0xff;
}

// The grammar this file's hexadecimal parameters document: hex digits, no
// larger than `limit`, and nothing else. strtol is not that grammar. It skips
// leading whitespace, accepts an optional sign, and stops at the first
// character it cannot use, so " 1234", "+1234", "-0" and "12ZZ" all parsed and
// were acted on, and a value over the limit was truncated rather than refused.
static bool parse_hex(const char *text, int limit, int &value)
{
    if ((text == NULL) || (*text == '\0')) {
        return false;
    }
    int parsed = 0;
    for (const char *p = text; *p; p++) {
        uint8_t digit = chartohex(*p);
        if ((digit == 0xff) || (parsed > (limit >> 4))) {
            return false;
        }
        parsed = (parsed << 4) | digit;
    }
    value = parsed;
    return true;
}

API_DOC(PUT, machine, menu_button,
    TAG("Machine")
    SUMMARY("Press the menu button")
    DESCRIPTION("Acts as if the menu button on the device had been pressed, which opens the "
                "Ultimate menu or closes it again. There is no way to ask whether the menu is "
                "open; `GET /v1/machine:menu_screen` answers 404 while it is not.\n"
                "\n"
                "While the menu is open it takes the keyboard, so keys injected through "
                "`POST /v1/machine:input` reach the menu rather than the running program.")
    PATH("/v1/machine:menu_button", "pushMenuButton", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The button press was delivered.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, machine, menu_button, NULL, ARRAY( {  }))
{
#if U64
    route_input_note_menu_button();
    if (push_active_menu_button && push_active_menu_button()) {
        resp->json_response(HTTP_OK);
        return;
    }
#endif
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_PUSH_BUTTON, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, machine, reset,
    TAG("Machine")
    SUMMARY("Reset the machine")
    CAUTION("machine-state", "Whatever the machine was running is lost.")
    DESCRIPTION("Pulls reset on the C64. The machine restarts with whatever cartridge is active, "
                "which is not the same as starting from cold; use `machine:reboot` for that.\n"
                "\n"
                "On Ultimate 64 hardware every key and joystick direction the input API is "
                "holding is released as part of the reset, and its mouse is detached, so a reset "
                "cannot leave an injected input stuck down.")
    PATH("/v1/machine:reset", "resetMachine", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine was reset.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, machine, reset, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0);
    SubsysResultCode_t retval = cmd->execute();
    if (retval.status == SSRET_OK) {
#if U64
        system_usb_keyboard.restReleaseAll();
        JoystickOutput::instance().releaseAllRest();
        route_input_release_rest_mouse();
#endif
    }
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, machine, reboot,
    TAG("Machine")
    SUMMARY("Reboot the machine")
    CAUTION("machine-state", "Whatever the machine was running is lost, and the cartridge is started from scratch.")
    DESCRIPTION("Resets the C64 and re-initialises the cartridge with it, which is what the "
                "Reboot entry in the menu does. Use this rather than `machine:reset` when a "
                "cartridge has to start from scratch.\n"
                "\n"
                "On Ultimate 64 hardware every key and joystick direction the input API is "
                "holding is released as part of the reboot, and its mouse is detached.")
    PATH("/v1/machine:reboot", "rebootMachine", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine was rebooted.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, machine, reboot, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_REBOOT, 0);
    SubsysResultCode_t retval = cmd->execute();
    if (retval.status == SSRET_OK) {
#if U64
        system_usb_keyboard.restReleaseAll();
        JoystickOutput::instance().releaseAllRest();
        route_input_release_rest_mouse();
#endif
    }
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, machine, pause,
    TAG("Machine")
    SUMMARY("Pause the CPU")
    CAUTION("machine-state", "The machine stays halted until machine:resume. A client that stops without resuming leaves it frozen.")
    DESCRIPTION("Halts the 6510 by holding DMA. The video output freezes on the frame that was "
                "being drawn and the machine stays halted until `machine:resume`. Memory can "
                "still be read and written while paused, which is what makes this useful before "
                "a large `machine:readmem`. While the on-device menu holds the machine, it is "
                "halted already and this does nothing; the machine runs again when the menu "
                "closes, so pause after that to keep it halted.")
    PATH("/v1/machine:pause", "pauseMachine", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The CPU is halted.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, machine, pause, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_PAUSE, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, machine, resume,
    TAG("Machine")
    SUMMARY("Resume the CPU")
    DESCRIPTION("Releases the DMA hold that `machine:pause` applied. Calling it on a machine that "
                "is not paused does nothing and is not an error. While the on-device menu holds the "
                "machine it stays halted, and runs again when the menu closes.")
    PATH("/v1/machine:resume", "resumeMachine", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The CPU is running.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, machine, resume, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESUME, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

// Split the same way the subsystem is: MENU_C64_POWEROFF reaches the power
// register on an Ultimate 64 and returns SSRET_NOT_IMPLEMENTED on a cartridge,
// so on a cartridge this call can only ever refuse.
#if U64
API_DOC(PUT, machine, poweroff,
    TAG("Machine")
    SUMMARY("Power the machine off")
    CAUTION("power", "There is no call that turns the machine back on. It has to be done at the machine.")
    DESCRIPTION("Turns the C64 off. The Ultimate 64 writes its power register directly; the "
                "Ultimate 64 Elite II asks the controller that owns the power rail. The "
                "machine can only be turned back on at the machine.")
    PATH("/v1/machine:poweroff", "powerOffMachine", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine is powering down.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
#else
API_DOC(PUT, machine, poweroff,
    TAG("Machine")
    SUMMARY("Power the machine off")
    DESCRIPTION("Not implemented on this product. The call is registered so that a client "
                "is told why, rather than being left to read a 404 as a wrong URL, and it "
                "always answers 501 here. The Ultimate 64 document describes what it does "
                "on hardware that has it.")
    PATH("/v1/machine:poweroff", "powerOffMachine", "")
    RESPONSE_ERROR("501", "This command is not supported on this architecture", "")
)
#endif
API_CALL(PUT, machine, poweroff, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_POWEROFF, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, machine, writemem,
    TAG("Machine")
    SUMMARY("Write bytes to C64 memory")
    CAUTION("destructive,idempotent", "Overwrites whatever the running program had at those addresses. Writing the same bytes again changes nothing further.")
    DESCRIPTION("Performs a DMA write on the cartridge bus. `data` carries the bytes as "
                "hexadecimal, two characters per byte, and at most 128 bytes fit here; use the "
                "POST form for more. The write may not pass $FFFF.\n"
                "\n"
                "The write is decoded through the bank configuration that is in force at the "
                "time, the same way a write by the CPU would be, so a write to $D020 reaches the "
                "VIC register while I/O is mapped in.")
    PATH("/v1/machine:writemem", "writeMemory", "")
    PARAM("address", "string", "Start address in hexadecimal, 0000 to FFFF.", "", "D020")
    PARAM("data", "string", "Bytes to write, hexadecimal, two characters each, 1 to 128 bytes.", "", "0006")
    RESPONSE("200", "application/json", "MemoryWriteResponse", "The range that was written.", "")
    RESPONSE_EXAMPLE("200", "Border and background", "{\n  \"address\" : \"d020-d021\",\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("400", "Invalid address", "")
    RESPONSE_ERROR("400", "Maximum length of 128 bytes exceeded. Consider using POST method with attachment.", "")
    RESPONSE_ERROR("400", "Use this API call to write at least one byte!", "")
    RESPONSE_ERROR("400", "Memory write exceeds location $FFFF", "")
)
API_CALL(PUT, machine, writemem, NULL, ARRAY( { {"address", P_REQUIRED}, {"data", P_REQUIRED} }))
{
    int address;
    if (!parse_hex(args["address"], 0xFFFF, address)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    const char *data = args["data"];
    int datalen = strlen(data) >> 1;
    if (datalen > 128) {
        resp->error("Maximum length of 128 bytes exceeded. Consider using POST method with attachment.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (datalen < 1) {
        resp->error("Use this API call to write at least one byte!");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (address + datalen > 65536) {
        resp->error("Memory write exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    uint8_t buf[128];
    for (int i=0;i<datalen; i++) {
        uint8_t b1 = chartohex(data[0]);
        if (b1 == 0xFF) {
            resp->error("Invalid char '%c' at position %d.", data[0], 2*i);
            resp->json_response(HTTP_BAD_REQUEST);
            return;
        }
        uint8_t b2 = chartohex(data[1]);
        if (b2 == 0xFF) {
            resp->error("Invalid char '%c' at position %d.", data[1], 2*i+1);
            resp->json_response(HTTP_BAD_REQUEST);
            return;
        }
        buf[i] = (b1 << 4)|b2;
        data += 2;
    }

    char msgbuf[16];
    sprintf(msgbuf, "%04x-%04x", address, address + datalen - 1);
    resp->json->add("address", msgbuf);

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, address, buf, datalen);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(POST, machine, writemem,
    TAG("Machine")
    SUMMARY("Upload bytes into C64 memory")
    CAUTION("destructive,idempotent", "Overwrites whatever the running program had at those addresses. Writing the same bytes again changes nothing further.")
    DESCRIPTION("The same DMA write as the PUT form, with the bytes in the request body instead "
                "of the URL, which raises the limit from 128 bytes to 65536. The body may be sent "
                "raw or as a multipart file part. The write may not pass $FFFF.")
    PATH("/v1/machine:writemem", "uploadMemory", "")
    PARAM("address", "string", "Start address in hexadecimal, 0000 to FFFF.", "", "0801")
    BODY("application/octet-stream", "", "The bytes to write.")
    BODY("multipart/form-data", "FileUpload", "The bytes to write, as a file part.")
    RESPONSE("200", "application/json", "MemoryWriteResponse", "The range that was written.", "")
    RESPONSE_ERROR("400", "Invalid address", "")
    RESPONSE_ERROR("400", "Memory write exceeds location $FFFF", "")
    RESPONSE_ERROR("404", "Could not read data from attachment", "")
    RESPONSE_ERROR("500", "Out of memory", "")
)
API_CALL(POST, machine, writemem, &attachment_writer, ARRAY( { {"address", P_REQUIRED} }))
{
    int address;
    if (!parse_hex(args["address"], 0xFFFF, address)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    TempfileWriter *handler = (TempfileWriter *)body;
    // Use malloc (not new): operator new panics on OOM on this target, so a new[]
    // result can never be NULL. malloc returns NULL, so this 64 KB request can fail
    // cleanly with HTTP 500 instead of taking the device down.
    uint8_t *buffer = (uint8_t *)malloc(65536);
    if (!buffer) {
        resp->error("Out of memory");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    uint32_t datalen = 0;
    FRESULT fres = FileManager::getFileManager()->load_file("", handler->get_filename(0), buffer, 65536, &datalen);
    if (fres != FR_OK) {
        resp->error("Could not read data from attachment");
        resp->json_response(HTTP_NOT_FOUND);
        free(buffer);
        return;
    }

    if (address + datalen > 65536) {
        resp->error("Memory write exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        free(buffer);
        return;
    }

    char msgbuf[16];
    sprintf(msgbuf, "%04x-%04x", address, address + datalen - 1);
    resp->json->add("address", msgbuf);

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, address, buffer, datalen);
    SubsysResultCode_t retval = cmd->execute();
    free(buffer);
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(GET, machine, readmem,
    TAG("Machine")
    SUMMARY("Read C64 memory")
    DESCRIPTION("Performs a DMA read on the cartridge bus and returns the bytes as a binary "
                "attachment. The read may not pass $FFFF.\n"
                "\n"
                "What comes back for an address that a ROM or an I/O register also occupies "
                "depends on the bank configuration at the moment of the read, so a read at $E000 "
                "returns the KERNAL or the RAM underneath it depending on the processor port. "
                "Pause the machine first if the values have to be consistent with each other.")
    PATH("/v1/machine:readmem", "readMemory", "")
    PARAM("address", "string", "Start address in hexadecimal, 0000 to FFFF.", "", "D020")
    PARAM("length", "integer(1..65536)", "Number of bytes to read.", "256", "2")
    RESPONSE("200", "application/octet-stream", "", "The bytes read.", "")
    RESPONSE_ERROR("400", "Invalid address", "")
    RESPONSE_ERROR("400", "Invalid length", "")
    RESPONSE_ERROR("400", "Memory read exceeds location $FFFF", "")
    RESPONSE_ERROR("500", "Out of memory", "")
)
API_CALL(GET, machine, readmem, NULL, ARRAY( { {"address", P_REQUIRED}, {"length", P_OPTIONAL} }))
{
    int address;
    if (!parse_hex(args["address"], 0xFFFF, address)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    int datalen = args.get_int("length", 256);
    if ((datalen < 1) || (datalen > 65536)) {
        resp->error("Invalid length");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    if (address + datalen > 65536) {
        resp->error("Memory read exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    // Use malloc (not new): operator new panics and spins forever on OOM, so a
    // new[] result can never be NULL. malloc returns NULL, so this caller-sized
    // request can fail cleanly with HTTP 500 instead of hanging the device.
    uint8_t *buffer = (uint8_t *)malloc(datalen);
    if (!buffer) {
        resp->error("Out of memory");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, address, buffer, datalen);
    SubsysResultCode_t retval = cmd->execute();
    if (retval.status == SSRET_OK) {
        // output result in JSON format
        StreamRamFile *rf = resp->add_attachment();
        rf->write(buffer, datalen);
        resp->binary_response();
    } else {
        resp->error(SubsysCommand::error_string(retval.status));
        resp->json_response(SubsysCommand::http_response_map(retval.status));
    }
    free(buffer);
}

API_DOC(GET, machine, menu_screen,
    TAG("Machine")
    SUMMARY("Read the Ultimate menu screen")
    DESCRIPTION("Returns what the Ultimate menu is drawing, as a binary attachment of exactly "
                "2000 bytes: 1000 screen codes for a 40 by 25 screen in reading order, followed "
                "by the 1000 colour values for the same cells. This is the screen the menu owns, "
                "not the screen of the running C64 program.\n"
                "\n"
                "When the menu is not on screen there is nothing to return and the call answers "
                "404, which is also the cheapest way to ask whether the menu is open.")
    PATH("/v1/machine:menu_screen", "getMenuScreen", "")
    RESPONSE("200", "application/octet-stream", "", "2000 bytes: 1000 screen codes followed by 1000 colour values.", "")
    RESPONSE_ERROR("404", "Menu screen unavailable.", "")
)
API_CALL(GET, machine, menu_screen, NULL, ARRAY( {  }))
{
    const int screen_size = UserInterface::ACTIVE_SCREEN_MATRIX_BYTES;
    uint8_t *buffer = new uint8_t[screen_size];

    if (UserInterface::copy_active_screen_matrix(buffer, screen_size)) {
        StreamRamFile *rf = resp->add_attachment();
        rf->write(buffer, screen_size);
        delete[] buffer;
        resp->binary_response();
    } else {
        resp->error("Menu screen unavailable.");
        resp->json_response(HTTP_NOT_FOUND);
        delete[] buffer;
    }
}

API_DOC(GET, machine, screenshot,
    TAG("Machine")
    SUMMARY("Capture a screenshot of the C64 screen")
    DESCRIPTION("Returns a single-frame screenshot of the screen as a 16-colour BMP image "
                "(384x272 PAL frame with borders). If the Ultimate menu is active, the menu screen "
                "is captured; otherwise, the running C64 screen (character, multicolor, or bitmap mode) "
                "and active VIC-II hardware sprites are rendered.\n"
                "\n"
                "If `format=raw` is specified, returns the raw 4-bpp palette-indexed pixel buffer (52224 bytes).")
    PATH("/v1/machine:screenshot", "getScreenshot", "")
    PARAM("format", "string", "Image format: 'bmp' (default) or 'raw'.", "bmp", "bmp")
    PARAM_ENUM("format", "bmp,raw")
    RESPONSE("200", "image/bmp", "", "A 16-colour BMP image of the screen (or raw bytes if format=raw).", "")
    RESPONSE_ERROR("400", "Invalid format", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("500", "Out of memory", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
extern uint8_t _chars_bin_start[] __attribute__((weak));
#if U64
extern uint8_t _default_chars_bin_start[] __attribute__((weak));

namespace {
alignas(4) static uint8_t s_char_rom_cache[4096];
static char s_cached_char_filename[64] = "";
static bool s_char_rom_cached = false;

static const uint8_t *get_char_rom_cache(void)
{
    C64 *c64 = C64::getMachine();
    const char *char_file = c64 ? c64->get_cfg_string(CFG_C64_CHARFILE) : "";
    if (!char_file) {
        char_file = "";
    }

    if (!s_char_rom_cached || strcmp(s_cached_char_filename, char_file) != 0) {
        FRESULT fres = FR_NOT_READY;
        if (*char_file) {
            fres = FileManager::getFileManager()->load_file(ROMS_DIRECTORY, char_file, s_char_rom_cache, sizeof(s_char_rom_cache), NULL);
        }
        if (fres == FR_OK) {
            strncpy(s_cached_char_filename, char_file, sizeof(s_cached_char_filename) - 1);
            s_cached_char_filename[sizeof(s_cached_char_filename) - 1] = '\0';
            s_char_rom_cached = true;
        } else if (_default_chars_bin_start) {
            memcpy(s_char_rom_cache, _default_chars_bin_start, sizeof(s_char_rom_cache));
            s_cached_char_filename[0] = '\0';
            s_char_rom_cached = true;
        }
    }
    return s_char_rom_cache;
}
}
#endif

API_CALL(GET, machine, screenshot, NULL, ARRAY( { {"format", P_OPTIONAL} } ))
{
    const char *fmt = args["format"];
    bool raw = false;
    if (fmt && *fmt) {
        if (strcmp(fmt, "raw") == 0) {
            raw = true;
        } else if (strcmp(fmt, "bmp") != 0) {
            resp->error("Invalid format");
            resp->json_response(HTTP_BAD_REQUEST);
            return;
        }
    }

    const int frame_width = 384;
    const int frame_height = 272;
    const int row_bytes = frame_width / 2; // 192 bytes
    const int total_pixels = row_bytes * frame_height; // 52224 bytes

    // Use malloc (not new): operator new panics on OOM on this target, whereas
    // malloc returns NULL so the request can fail cleanly with HTTP 500.
    uint8_t *pixel_rows = (uint8_t *)malloc(total_pixels);
    if (!pixel_rows) {
        resp->error("Out of memory");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    // 1024 bytes for screen matrix + sprite pointers ($03F8-$03FF), plus 1000 bytes for color RAM
    uint8_t *screen_buf = (uint8_t *)malloc(1024 + UserInterface::ACTIVE_SCREEN_MATRIX_CELLS);
    if (!screen_buf) {
        free(pixel_rows);
        resp->error("Out of memory");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    uint8_t *chars = screen_buf;
    uint8_t *colors = screen_buf + UserInterface::ACTIVE_SCREEN_MATRIX_CELLS;

    uint8_t border_color = 0x06; // Blue default
    uint8_t bg_color = 0x06;
    uint8_t vic_regs[48];
    memset(vic_regs, 0, sizeof(vic_regs));
    bool is_menu = false;
    bool is_bitmap = false;
    uint8_t *bitmap_buf = NULL;
    uint8_t *custom_chars = NULL;
    uint8_t *sprite_data = NULL;
    uint8_t *fg_mask = NULL;
    int rom_bank_offset = 0;
    bool use_rom = true;

    // Check if Ultimate menu is currently open
    int menu_border = 0, menu_bg = 0;
    if (UserInterface::copy_active_screen_matrix(screen_buf, UserInterface::ACTIVE_SCREEN_MATRIX_BYTES, &menu_border, &menu_bg)) {
        is_menu = true;
        border_color = menu_border & 0x0F;
        bg_color = menu_bg & 0x0F;
    } else {
        colors = screen_buf + 1024;
        // Read C64 VIC-II registers ($D000..$D02F)
        SubsysCommand *cmd_vic = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, 0xD000, vic_regs, 48);
        SubsysResultCode_t retval = cmd_vic->execute();
        if (retval.status != SSRET_OK) {
            free(screen_buf);
            free(pixel_rows);
            resp->error(SubsysCommand::error_string(retval.status));
            resp->json_response(SubsysCommand::http_response_map(retval.status));
            return;
        }
        border_color = vic_regs[0x20] & 0x0F;
        bg_color = vic_regs[0x21] & 0x0F;
        uint8_t d011 = vic_regs[0x11];
        uint8_t d018 = vic_regs[0x18];

        // CIA2 Port A for VIC bank
        uint8_t cia2_byte = 0;
        SubsysCommand *cmd_cia = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, 0xDD00, &cia2_byte, 1);
        cmd_cia->execute();
        uint16_t vic_bank = (3 - (cia2_byte & 0x03)) * 0x4000;
        uint16_t screen_addr = vic_bank + ((d018 & 0xF0) << 6);

        // Read 1024 bytes so sprite pointers at screen_addr + 0x03F8..0x03FF are included
        SubsysCommand *cmd_scr = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, screen_addr, chars, 1024);
        cmd_scr->execute();

        SubsysCommand *cmd_col = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, 0xD800, colors, 1000);
        cmd_col->execute();

        // Check for Bitmap Mode (BMM = $D011 bit 5)
        if (d011 & 0x20) {
            is_bitmap = true;
            uint16_t bitmap_addr = vic_bank + ((d018 & 0x08) ? 0x2000 : 0x0000);
            bitmap_buf = (uint8_t *)malloc(8000);
            if (!bitmap_buf) {
                free(screen_buf);
                free(pixel_rows);
                resp->error("Out of memory");
                resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            SubsysCommand *cmd_bmp = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, bitmap_addr, bitmap_buf, 8000);
            cmd_bmp->execute();
        } else {
            // Character Mode
            uint16_t char_offset = (d018 & 0x0E) << 10;
            // VIC-II reads Character ROM in Bank 0 and Bank 2 at offsets 0x1000 and 0x1800
            if ((vic_bank == 0x0000 || vic_bank == 0x8000) && (char_offset == 0x1000 || char_offset == 0x1800)) {
                use_rom = true;
                rom_bank_offset = (char_offset == 0x1800) ? 2048 : 0;
            } else {
                use_rom = false;
                custom_chars = (uint8_t *)malloc(2048);
                if (!custom_chars) {
                    free(screen_buf);
                    free(pixel_rows);
                    resp->error("Out of memory");
                    resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }
                SubsysCommand *cmd_font = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, vic_bank + char_offset, custom_chars, 2048);
                cmd_font->execute();
            }
        }

        // Read enabled hardware sprites ($D015)
        uint8_t spr_enable = vic_regs[0x15];
        if (spr_enable != 0) {
            sprite_data = (uint8_t *)malloc(8 * 64);
            fg_mask = (uint8_t *)calloc(8000, 1);
            if (sprite_data) {
                memset(sprite_data, 0, 8 * 64);
                for (int s = 0; s < 8; s++) {
                    if (!(spr_enable & (1 << s))) {
                        continue;
                    }
                    uint8_t ptr = chars[0x03F8 + s];
                    bool copied = false;
                    for (int prev = 0; prev < s; prev++) {
                        if ((spr_enable & (1 << prev)) && (chars[0x03F8 + prev] == ptr)) {
                            memcpy(sprite_data + (s * 64), sprite_data + (prev * 64), 63);
                            copied = true;
                            break;
                        }
                    }
                    if (!copied) {
                        uint16_t spr_offset = ((uint16_t)ptr) << 6;
                        SubsysCommand *cmd_spr = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, vic_bank + spr_offset, sprite_data + (s * 64), 63);
                        cmd_spr->execute();
                    }
                }
            }
        }
    }

    // Fill border
    uint8_t border_byte = (border_color << 4) | (border_color & 0x0F);
    memset(pixel_rows, border_byte, total_pixels);

    if (is_bitmap && bitmap_buf) {
        bool is_mc_bitmap = (vic_regs[0x16] & 0x10) != 0;
        for (int row = 0; row < 25; row++) {
            for (int col = 0; col < 40; col++) {
                int cell_idx = row * 40 + col;
                uint8_t screen_byte = chars[cell_idx];

                if (is_mc_bitmap) {
                    uint8_t c0 = bg_color;
                    uint8_t c1 = (screen_byte >> 4) & 0x0F;
                    uint8_t c2 = screen_byte & 0x0F;
                    uint8_t c3 = colors[cell_idx] & 0x0F;

                    for (int ly = 0; ly < 8; ly++) {
                        int frame_y = 36 + (row * 8) + ly;
                        uint8_t bmp_byte = bitmap_buf[(cell_idx * 8) + ly];
                        uint8_t fg_byte = 0;

                        for (int p = 0; p < 4; p++) {
                            uint8_t pair = (bmp_byte >> (6 - 2 * p)) & 0x03;
                            uint8_t c_pixel;
                            switch (pair) {
                                case 0: c_pixel = c0; break;
                                case 1: c_pixel = c1; break;
                                case 2: c_pixel = c2; fg_byte |= (0xC0 >> (2 * p)); break;
                                default: c_pixel = c3; fg_byte |= (0xC0 >> (2 * p)); break;
                            }
                            int row_byte_idx = 16 + (col * 4) + p;
                            pixel_rows[frame_y * row_bytes + row_byte_idx] = (c_pixel << 4) | (c_pixel & 0x0F);
                        }
                        if (fg_mask) {
                            fg_mask[(row * 8 + ly) * 40 + col] = fg_byte;
                        }
                    }
                } else {
                    uint8_t fg = (screen_byte >> 4) & 0x0F;
                    uint8_t bg = screen_byte & 0x0F;

                    for (int ly = 0; ly < 8; ly++) {
                        int frame_y = 36 + (row * 8) + ly;
                        uint8_t bmp_byte = bitmap_buf[(cell_idx * 8) + ly];

                        for (int p = 0; p < 8; p += 2) {
                            uint8_t p0 = (bmp_byte & (0x80 >> p)) ? fg : bg;
                            uint8_t p1 = (bmp_byte & (0x80 >> (p + 1))) ? fg : bg;
                            int row_byte_idx = 16 + (col * 4) + (p / 2);
                            pixel_rows[frame_y * row_bytes + row_byte_idx] = (p0 << 4) | (p1 & 0x0F);
                        }
                        if (fg_mask) {
                            fg_mask[(row * 8 + ly) * 40 + col] = bmp_byte;
                        }
                    }
                }
            }
        }
        free(bitmap_buf);
    } else {
        const uint8_t *font_ptr = NULL;
        bool font_is_7bit = false;
        if (is_menu) {
            font_ptr = _chars_bin_start;
            font_is_7bit = true;
        } else if (use_rom) {
#if U64
            const uint8_t *rom = get_char_rom_cache();
            font_ptr = rom ? (rom + rom_bank_offset) : NULL;
#else
            font_ptr = _chars_bin_start;
            font_is_7bit = true;
#endif
        } else {
            font_ptr = custom_chars;
        }

        uint8_t bg1_color = vic_regs[0x22] & 0x0F;
        uint8_t bg2_color = vic_regs[0x23] & 0x0F;
        bool is_mc_text = !is_menu && ((vic_regs[0x16] & 0x10) != 0);

        for (int row = 0; row < 25; row++) {
            for (int col = 0; col < 40; col++) {
                int cell_idx = row * 40 + col;
                uint8_t char_code = chars[cell_idx];
                uint8_t color_byte = colors[cell_idx];

                if (is_menu) {
                    uint8_t fg = color_byte & 0x0F;
#if U64 == 2
                    uint8_t bg = (color_byte >> 4) & 0x0F;
#else
                    uint8_t bg = bg_color;
#endif
                    if (char_code & 0x80) {
                        uint8_t tmp = fg;
                        fg = bg;
                        bg = tmp;
                        char_code &= 0x7F;
                    }
                    for (int ly = 0; ly < 8; ly++) {
                        int frame_y = 36 + (row * 8) + ly;
                        uint8_t glyph = font_ptr ? font_ptr[(char_code * 8) + ly] : 0;
                        for (int p = 0; p < 8; p += 2) {
                            uint8_t p0 = (glyph & (0x80 >> p)) ? fg : bg;
                            uint8_t p1 = (glyph & (0x80 >> (p + 1))) ? fg : bg;
                            int row_byte_idx = 16 + (col * 4) + (p / 2);
                            pixel_rows[frame_y * row_bytes + row_byte_idx] = (p0 << 4) | (p1 & 0x0F);
                        }
                    }
                } else if (is_mc_text && (color_byte & 0x08)) {
                    // Multicolor character cell: 4 double-width pixels
                    uint8_t fg = color_byte & 0x07;
                    uint8_t glyph_idx = font_is_7bit ? (char_code & 0x7F) : char_code;
                    for (int ly = 0; ly < 8; ly++) {
                        int frame_y = 36 + (row * 8) + ly;
                        uint8_t glyph = font_ptr ? font_ptr[(glyph_idx * 8) + ly] : 0;
                        uint8_t fg_byte = 0;
                        for (int p = 0; p < 4; p++) {
                            uint8_t pair = (glyph >> (6 - 2 * p)) & 0x03;
                            uint8_t c_pixel;
                            switch (pair) {
                                case 0: c_pixel = bg_color; break;
                                case 1: c_pixel = bg1_color; break;
                                case 2: c_pixel = bg2_color; fg_byte |= (0xC0 >> (2 * p)); break;
                                default: c_pixel = fg; fg_byte |= (0xC0 >> (2 * p)); break;
                            }
                            int row_byte_idx = 16 + (col * 4) + p;
                            pixel_rows[frame_y * row_bytes + row_byte_idx] = (c_pixel << 4) | (c_pixel & 0x0F);
                        }
                        if (fg_mask) {
                            fg_mask[(row * 8 + ly) * 40 + col] = fg_byte;
                        }
                    }
                } else {
                    // Standard High-Resolution Character cell (ROM or RAM font)
                    uint8_t fg = is_mc_text ? (color_byte & 0x07) : (color_byte & 0x0F);
                    uint8_t bg = bg_color;
                    bool inverted = false;
                    if (font_is_7bit && (char_code & 0x80)) {
                        uint8_t tmp = fg;
                        fg = bg;
                        bg = tmp;
                        char_code &= 0x7F;
                        inverted = true;
                    }

                    for (int ly = 0; ly < 8; ly++) {
                        int frame_y = 36 + (row * 8) + ly;
                        uint8_t glyph = font_ptr ? font_ptr[(char_code * 8) + ly] : 0;
                        for (int p = 0; p < 8; p += 2) {
                            uint8_t p0 = (glyph & (0x80 >> p)) ? fg : bg;
                            uint8_t p1 = (glyph & (0x80 >> (p + 1))) ? fg : bg;
                            int row_byte_idx = 16 + (col * 4) + (p / 2);
                            pixel_rows[frame_y * row_bytes + row_byte_idx] = (p0 << 4) | (p1 & 0x0F);
                        }
                        if (fg_mask) {
                            fg_mask[(row * 8 + ly) * 40 + col] = inverted ? (uint8_t)~glyph : glyph;
                        }
                    }
                }
            }
        }
        if (custom_chars) {
            free(custom_chars);
        }
    }

    // Composite active VIC-II hardware sprites ($D015) in priority order (Sprite 0 highest .. Sprite 7 lowest)
    uint8_t spr_enable = vic_regs[0x15];
    if (!is_menu && (spr_enable != 0) && sprite_data) {
        uint8_t spr_msb_x = vic_regs[0x10];
        uint8_t spr_y_exp = vic_regs[0x17];
        uint8_t spr_bg_prio = vic_regs[0x1B];
        uint8_t spr_mc = vic_regs[0x1C];
        uint8_t spr_x_exp = vic_regs[0x1D];
        uint8_t spr_mc0 = vic_regs[0x25] & 0x0F;
        uint8_t spr_mc1 = vic_regs[0x26] & 0x0F;

        int sx[8], sy[8], sw[8], sh[8];
        for (int s = 0; s < 8; s++) {
            sx[s] = ((int)vic_regs[s * 2] | ((spr_msb_x & (1 << s)) ? 0x100 : 0)) - 24;
            sy[s] = (int)vic_regs[s * 2 + 1] - 50;
            sw[s] = (spr_x_exp & (1 << s)) ? 48 : 24;
            sh[s] = (spr_y_exp & (1 << s)) ? 42 : 21;
        }

        for (int active_y = 0; active_y < 200; active_y++) {
            uint8_t row_sprites = 0;
            for (int s = 0; s < 8; s++) {
                if ((spr_enable & (1 << s)) && (active_y >= sy[s]) && (active_y < sy[s] + sh[s])) {
                    row_sprites |= (1 << s);
                }
            }
            if (!row_sprites) {
                continue;
            }

            int frame_y = 36 + active_y;
            for (int active_x = 0; active_x < 320; active_x++) {
                for (int s = 0; s < 8; s++) {
                    if (!(row_sprites & (1 << s))) {
                        continue;
                    }
                    if ((active_x < sx[s]) || (active_x >= sx[s] + sw[s])) {
                        continue;
                    }
                    int dx = active_x - sx[s];
                    int dy = active_y - sy[s];
                    int src_x = (spr_x_exp & (1 << s)) ? (dx >> 1) : dx;
                    int src_y = (spr_y_exp & (1 << s)) ? (dy >> 1) : dy;
                    const uint8_t *rptr = sprite_data + (s * 64) + (src_y * 3);
                    uint32_t row_bits = ((uint32_t)rptr[0] << 16) | ((uint32_t)rptr[1] << 8) | (uint32_t)rptr[2];

                    uint8_t c_pixel = 0xFF;
                    if (spr_mc & (1 << s)) {
                        uint8_t pair = (row_bits >> (22 - (src_x & ~1))) & 0x03;
                        if (pair == 1) {
                            c_pixel = spr_mc0;
                        } else if (pair == 2) {
                            c_pixel = vic_regs[0x27 + s] & 0x0F;
                        } else if (pair == 3) {
                            c_pixel = spr_mc1;
                        }
                    } else {
                        if ((row_bits >> (23 - src_x)) & 1) {
                            c_pixel = vic_regs[0x27 + s] & 0x0F;
                        }
                    }

                    if (c_pixel != 0xFF) {
                        bool is_fg = fg_mask && ((fg_mask[active_y * 40 + (active_x >> 3)] & (0x80 >> (active_x & 7))) != 0);
                        if (!((spr_bg_prio & (1 << s)) && is_fg)) {
                            int byte_idx = frame_y * row_bytes + 16 + (active_x >> 1);
                            if ((active_x & 1) == 0) {
                                pixel_rows[byte_idx] = (pixel_rows[byte_idx] & 0x0F) | (c_pixel << 4);
                            } else {
                                pixel_rows[byte_idx] = (pixel_rows[byte_idx] & 0xF0) | (c_pixel & 0x0F);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    if (sprite_data) {
        free(sprite_data);
    }
    if (fg_mask) {
        free(fg_mask);
    }
    free(screen_buf);

    StreamRamFile *rf = resp->add_attachment();
    if (raw) {
        rf->write(pixel_rows, total_pixels);
        free(pixel_rows);
        resp->binary_response("application/octet-stream", "screenshot.raw");
    } else {
        // Build 118-byte BMP header
        uint8_t bmp_hdr[118];
        memset(bmp_hdr, 0, 118);

        // BITMAPFILEHEADER (14 bytes)
        bmp_hdr[0] = 0x42; // 'B'
        bmp_hdr[1] = 0x4D; // 'M'
        uint32_t file_size = 118 + total_pixels;
        bmp_hdr[2] = file_size & 0xFF;
        bmp_hdr[3] = (file_size >> 8) & 0xFF;
        bmp_hdr[4] = (file_size >> 16) & 0xFF;
        bmp_hdr[5] = (file_size >> 24) & 0xFF;
        bmp_hdr[10] = 118; // offset to pixel bits

        // BITMAPINFOHEADER (40 bytes)
        bmp_hdr[14] = 40; // biSize
        bmp_hdr[18] = frame_width & 0xFF;
        bmp_hdr[19] = (frame_width >> 8) & 0xFF;
        bmp_hdr[22] = frame_height & 0xFF;
        bmp_hdr[23] = (frame_height >> 8) & 0xFF;
        bmp_hdr[26] = 1;  // biPlanes
        bmp_hdr[28] = 4;  // biBitCount (4 bpp)
        bmp_hdr[34] = total_pixels & 0xFF;
        bmp_hdr[35] = (total_pixels >> 8) & 0xFF;
        bmp_hdr[36] = (total_pixels >> 16) & 0xFF;
        bmp_hdr[37] = (total_pixels >> 24) & 0xFF;
        bmp_hdr[38] = 0x13; bmp_hdr[39] = 0x0B; // 2835 ppm (~72 DPI)
        bmp_hdr[42] = 0x13; bmp_hdr[43] = 0x0B;
        bmp_hdr[46] = 16; // biClrUsed
        bmp_hdr[50] = 16; // biClrImportant

        // 16-color C64 RGB palette matching U64 default_colors (R, G, B)
        uint8_t c64_rgb[16][3] = {
            { 0x00, 0x00, 0x00 }, // 0: Black
            { 0xF7, 0xF7, 0xF7 }, // 1: White
            { 0x8D, 0x2F, 0x34 }, // 2: Red
            { 0x6A, 0xD4, 0xCD }, // 3: Cyan
            { 0x98, 0x35, 0xA4 }, // 4: Purple
            { 0x4C, 0xB4, 0x42 }, // 5: Green
            { 0x2C, 0x29, 0xB1 }, // 6: Blue
            { 0xEF, 0xEF, 0x5D }, // 7: Yellow
            { 0x98, 0x4E, 0x20 }, // 8: Orange
            { 0x5B, 0x38, 0x00 }, // 9: Brown
            { 0xD1, 0x67, 0x6D }, // 10: Light Red
            { 0x4A, 0x4A, 0x4A }, // 11: Dark Grey
            { 0x7B, 0x7B, 0x7B }, // 12: Grey
            { 0x9F, 0xEF, 0x93 }, // 13: Light Green
            { 0x6D, 0x6A, 0xEF }, // 14: Light Blue
            { 0xB2, 0xB2, 0xB2 }  // 15: Light Grey
        };
#if U64 && !defined(RECOVERYAPP)
        U64Config::get_palette_rgb(c64_rgb);
#endif

        for (int i = 0; i < 16; i++) {
            bmp_hdr[54 + i * 4 + 0] = c64_rgb[i][2]; // Blue
            bmp_hdr[54 + i * 4 + 1] = c64_rgb[i][1]; // Green
            bmp_hdr[54 + i * 4 + 2] = c64_rgb[i][0]; // Red
            bmp_hdr[54 + i * 4 + 3] = 0;
        }

        rf->write(bmp_hdr, 118);

        // BMP scanlines are stored bottom-to-top
        for (int y = frame_height - 1; y >= 0; y--) {
            rf->write(&pixel_rows[y * row_bytes], row_bytes);
        }
        free(pixel_rows);
        resp->binary_response("image/bmp", "screenshot.bmp");
    }
}

#if U64
#include "u64.h"
API_DOC(GET, machine, debugreg,
    TAG("Diagnostics")
    SUMMARY("Read the debug register")
    CAUTION("diagnostic", "An FPGA debug facility, not part of the C64 memory map.")
    DESCRIPTION("Reads the Ultimate 64 debug register at $D7FF and returns it as two hexadecimal "
                "digits. The register controls FPGA debug facilities and is not part of the C64 "
                "memory map that `machine:readmem` reaches.")
    PATH("/v1/machine:debugreg", "readDebugRegister", "")
    RESPONSE("200", "application/json", "DebugRegisterResponse", "The contents of the register.", "")
)
API_CALL(GET, machine, debugreg, NULL, ARRAY( {  }))
{
    char buf[4];
    sprintf(buf, "%02X", U64_DEBUG_REGISTER);
    resp->json->add("value", buf);
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, machine, debugreg,
    TAG("Diagnostics")
    SUMMARY("Write the debug register")
    CAUTION("diagnostic", "An FPGA debug facility, not part of the C64 memory map.")
    DESCRIPTION("Writes `value` to the debug register at $D7FF and returns what the register "
                "reads back afterwards, which is not necessarily what was written: some bits are "
                "driven by the hardware.")
    PATH("/v1/machine:debugreg", "writeDebugRegister", "")
    PARAM("value", "string", "Byte to write, in hexadecimal, 00 to FF.", "", "1F")
    RESPONSE("200", "application/json", "DebugRegisterResponse", "The register after the write.", "")
    RESPONSE_ERROR("400", "Invalid value", "")
)
API_CALL(PUT, machine, debugreg, NULL, ARRAY( { { "value", P_REQUIRED } }))
{
    int value;
    if (!parse_hex(args["value"], 0xFF, value)) {
        resp->error("Invalid value");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    U64_DEBUG_REGISTER = (uint8_t)value;

    char buf[4];
    sprintf(buf, "%02X", U64_DEBUG_REGISTER);
    resp->json->add("value", buf);
    resp->json_response(HTTP_OK);
}
#endif

#define NUM_SIGNALS 10

static char *bin(uint64_t val, int bits, char *buffer)
{
    int bit;
    int leading = 1;
    int i = 0;
    while (--bits >= 0) {
        bit = ((val & (1LL << bits)) != 0LL);
        if (leading && (bits != 0) && !bit)
            continue;
        leading = 0;
        buffer[i++] = '0' + bit;
    }
    buffer[i] = 0;
    return buffer;
}

static void make_vcd(StreamRamFile *d, uint32_t *values, int count, const char *step)
{
    typedef struct {
        const char *name;
        int shift;
        int width;
    } t_signals;

    const t_signals signals[NUM_SIGNALS] = {
        { "DotClk", 31, 1 },
        { "PHI2", 30, 1 },
        { "PHI2_recovered", 29, 1 },
        { "DMA_Data_Out", 28, 1 },
        { "Drive_Data", 27, 1 },
        { "Addr_Tri_L", 26, 1 },
        { "Addr_Tri_H", 25, 1 },
        { "Addr", 0, 16 },
        { "Data", 16, 8 },
        { "R_Wn", 24, 1 },
    };

    const char vcd_header[] = "$timescale\n %s\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

    d->format(vcd_header, step);
    for(int n=0; n<NUM_SIGNALS; n++) {
        d->format("$var wire %d %c %s $end\n", signals[n].width, 97+n, signals[n].name);
    }
    d->format(vcd_middle);

    uint32_t prev = values[0] ^ 0xFFFFFFFF; // inverse of first value
    char binbuf[36];
    for(int i=0;i<count;i++) {
        if (prev == values[i]) {
            continue;
        }
        d->format("#%d\n", i);
        for(int n=0; n<NUM_SIGNALS; n++) {
            // isolate value
            uint32_t mask = (1 << signals[n].width)-1;
            uint32_t pr = (prev >> signals[n].shift) & mask;
            uint32_t cur = (values[i] >> signals[n].shift) & mask;
            if (pr != cur) {
                if (signals[n].width == 1) {
                    d->format("%d%c\n", cur, 97+n);
                } else {
                    d->format("b%s %c\n", bin(cur, signals[n].width, binbuf), 97+n);
                }
            }
        }
        prev = values[i];
    }
}

API_DOC(GET, machine, measure,
    TAG("Diagnostics")
    SUMMARY("Capture cartridge bus timing")
    CAUTION("diagnostic", "A hardware capture for diagnosing the cartridge bus.")
    DESCRIPTION("Samples the cartridge bus and returns the capture as a VCD file, which any "
                "waveform viewer opens. The traces are the dot clock, PHI2 and the copy the FPGA "
                "recovers from it, the address and data buses, the read/write line and the "
                "tri-state controls. The sample interval follows the FPGA clock of the product, "
                "so it is 20 ns, 16 ns or 15 ns depending on which one this is.\n"
                "\n"
                "The measurement block is an optional part of the FPGA build. Where it is absent, "
                "and it usually is on the cartridges, the call answers 501.")
    PATH("/v1/machine:measure", "measureBusTiming", "")
    RESPONSE("200", "application/octet-stream", "", "A VCD capture, offered as bus_measurement.vcd.", "")
    RESPONSE_ERROR("501", "The current FPGA build does not support timing measurement of the cartridge bus.", "")
    RESPONSE_ERROR("500", "Out of memory", "")
)
API_CALL(GET, machine, measure, NULL, ARRAY( {  }))
{
    // Capability check before the allocation: it used to run after, and returned
    // without freeing, leaking 64 KB per call. Bus measurement is off by default
    // on U2 builds, so that early return is the common path, and the leak is what
    // drives the heap towards the OOM the readmem path guards against.
    if (!(getFpgaCapabilities() & CAPAB_BUS_MEASURE)) {
        resp->error("The current FPGA build does not support timing measurement of the cartridge bus.");
        resp->json_response(HTTP_NOT_IMPLEMENTED);
        return;
    }

    uint8_t *buffer = (uint8_t *)malloc(64*1024);
    if (!buffer) {
        resp->error("Out of memory");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_MEASURE_TIMING_API, 0, buffer, 64*1024);
    SubsysResultCode_t retval = cmd->execute();

    if (retval.status == SSRET_OK) {
        StreamRamFile *rf = resp->add_attachment();
        rf->setFileName("bus_measurement.vcd");
        //rf->write(buffer, 48*1024);
#if CLOCK_FREQ == 50000000        
        make_vcd(rf, (uint32_t*)buffer, 64*256, "20 ns");
#elif CLOCK_FREQ == 62500000
        make_vcd(rf, (uint32_t*)buffer, 64*256, "16 ns");
#else
        make_vcd(rf, (uint32_t*)buffer, 64*256, "15 ns");
#endif
        resp->binary_response();
    } else {
        resp->error(SubsysCommand::error_string(retval.status));
        resp->json_response(SubsysCommand::http_response_map(retval.status));
    }
    free(buffer);
}

// Heap statistics. Every dynamic allocation on this target reaches the FreeRTOS
// heap -- operator new goes through get_mem() and malloc through __wrap_malloc,
// and both call pvPortMalloc (software/system/memory_wrap.cc). So one free-bytes
// figure accounts for all of it, which is what makes a leak visible from outside:
// sample before and after a body of work, then compare.
//
// "free" is the number to diff. "min_ever_free" is the low-water mark since boot,
// useful for headroom but not for leaks: it never recovers, so it cannot tell a
// leak from a transient peak.
API_DOC(GET, machine, heap,
    TAG("Diagnostics")
    SUMMARY("Read heap statistics")
    CAUTION("diagnostic", "Firmware internals, for finding a leak rather than for driving the machine.")
    DESCRIPTION("Reports the FreeRTOS heap. Every dynamic allocation in the firmware ends up "
                "there, `new` and `malloc` alike, so one free figure accounts for all of it.\n"
                "\n"
                "`free` is the number to diff: sample it, do a body of work, sample it again, and "
                "a difference that does not come back is a leak. `min_ever_free` is the low water "
                "mark since boot. It never recovers, so it shows how much headroom there has ever "
                "been but cannot tell a leak from a transient peak.")
    PATH("/v1/machine:heap", "getHeapStatistics", "")
    RESPONSE("200", "application/json", "HeapResponse", "The state of the heap.", "")
    RESPONSE_EXAMPLE("200", "Heap", "{\n  \"free\" : 1583280,\n  \"min_ever_free\" : 1502864,\n  \"total\" : 2097152,\n  \"errors\" : []\n}", "")
)
API_CALL(GET, machine, heap, NULL, ARRAY( {  }))
{
    resp->json->add("free", (int)xPortGetFreeHeapSize());
    resp->json->add("min_ever_free", (int)xPortGetMinimumEverFreeHeapSize());
    resp->json->add("total", (int)configTOTAL_HEAP_SIZE);
    resp->json_response(HTTP_OK);
}
