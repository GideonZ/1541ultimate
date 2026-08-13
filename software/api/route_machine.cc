#include "routes.h"
#include "attachment_writer.h"
#include "pattern.h"
#include "subsys.h"
#include "c64.h"
#include "c64_subsys.h"
#include "userinterface.h"
#if U64
#include "keyboard_usb.h"
#include "joystick_output.h"
extern "C" void route_input_note_menu_button(void);
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
                "holding is released as part of the reset, so a reset cannot leave an injected "
                "key stuck down.")
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
                "holding is released as part of the reboot.")
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
                "a large `machine:readmem`.")
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
                "is not paused does nothing and is not an error.")
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
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
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
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
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
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
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
    PARAM("value", "string", "Byte to write, in hexadecimal.", "", "1F")
    RESPONSE("200", "application/json", "DebugRegisterResponse", "The register after the write.", "")
)
API_CALL(PUT, machine, debugreg, NULL, ARRAY( { { "value", P_REQUIRED } }))
{
    int value = strtol(args["value"], NULL, 16);
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

#ifdef HEAP_TRACK
// Where the leak is, as opposed to whether there is one. Present only in a
// build made with EXTRA_DEFINES=-DHEAP_TRACK=1; see software/system/heap_track.h.
//
// Read these differentially: reset, exercise the device, read, exercise it
// again, read again. An allocation that is live is not one that is leaked, and
// only the growth between two identical rounds separates them.
#include "heap_track.h"

#define HEAP_TRACK_REPORT_CALLERS 48

API_CALL(PUT, machine, heap_allocations_reset, NULL, ARRAY( {  }))
{
    heap_track_reset();
    resp->json_response(HTTP_OK);
}

API_CALL(GET, machine, heap_allocations, NULL, ARRAY( {  }))
{
    static HeapTrackCaller_t callers[HEAP_TRACK_REPORT_CALLERS];
    HeapTrackTotals_t totals;
    char buf[64];

    int n = heap_track_report(callers, HEAP_TRACK_REPORT_CALLERS, &totals);
    for (int i = 0; i < n; i++) {
        // small_printf has no %lu, so every field is printed as %d or %p.
        sprintf(buf, "%d allocations %d bytes ra %p", (int)callers[i].blocks,
                (int)callers[i].bytes, callers[i].caller);
        resp->json->add("caller", buf);
    }
    resp->json->add("live_allocations", (int)totals.live_blocks);
    resp->json->add("live_bytes", (int)totals.live_bytes);
    resp->json->add("distinct_callers", n);
    // Non-zero means the table dropped records and is under-reporting.
    resp->json->add("evicted", (int)totals.evicted);
    resp->json_response(HTTP_OK);
}
#endif
