#include "routes.h"
#include "attachment_writer.h"
#include "pattern.h"
#include "subsys.h"
#include "c64.h"
#include "c64_subsys.h"

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

API_CALL(PUT, machine, reset, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(PUT, machine, reboot, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_REBOOT, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(PUT, machine, pause, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_PAUSE, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(PUT, machine, resume, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESUME, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(PUT, machine, poweroff, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_POWEROFF, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

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

API_CALL(POST, machine, writemem, &attachment_writer, ARRAY( { {"address", P_REQUIRED} }))
{
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    TempfileWriter *handler = (TempfileWriter *)body;
    uint8_t *buffer = new uint8_t[65536];
    uint32_t datalen = 0;
    FRESULT fres = FileManager::getFileManager()->load_file("", handler->get_filename(0), buffer, 65536, &datalen);
    if (fres != FR_OK) {
        resp->error("Could not read data from attachment");
        resp->json_response(HTTP_NOT_FOUND);
        delete[] buffer;
        return;
    }

    if (address + datalen > 65536) {
        resp->error("Memory write exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        delete[] buffer;
        return;
    }

    char msgbuf[16];
    sprintf(msgbuf, "%04x-%04x", address, address + datalen - 1);
    resp->json->add("address", msgbuf);

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, address, buffer, datalen);
    SubsysResultCode_t retval = cmd->execute();
    delete[] buffer;
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(GET, machine, readmem, NULL, ARRAY( { {"address", P_REQUIRED}, {"length", P_OPTIONAL} }))
{
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    int datalen = args.get_int("length", 256);
    if ((datalen < 0) || (datalen > 65536)) {
        resp->error("Invalid length");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    if (address + datalen > 65536) {
        resp->error("Memory read exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    uint8_t *buffer = new uint8_t[datalen];
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_READ, address, buffer, datalen);
    SubsysResultCode_t retval = cmd->execute();
    if (retval.status == SSRET_OK) {
        // output result in JSON format
        StreamRamFile *rf = resp->add_attachment();
        rf->write(buffer, datalen);
        delete[] buffer;
        resp->binary_response();
    } else {
        resp->error(SubsysCommand::error_string(retval.status));
        resp->json_response(SubsysCommand::http_response_map(retval.status));
        delete[] buffer;
    }
}

#if U64
#include "u64.h"
API_CALL(GET, machine, debugreg, NULL, ARRAY( {  }))
{
    char buf[4];
    sprintf(buf, "%02X", U64_DEBUG_REGISTER);
    resp->json->add("value", buf);
    resp->json_response(HTTP_OK);
}

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

API_CALL(GET, machine, measure, NULL, ARRAY( {  }))
{
    uint8_t *buffer = new uint8_t[64*1024];

    if (!(getFpgaCapabilities() & CAPAB_BUS_MEASURE)) {
        resp->error("The current FPGA build does not support timing measurement of the cartridge bus.");
        resp->json_response(HTTP_NOT_IMPLEMENTED);
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
        delete[] buffer;
        resp->binary_response();
    } else {
        resp->error(SubsysCommand::error_string(retval.status));
        resp->json_response(SubsysCommand::http_response_map(retval.status));
        delete[] buffer;
    }
}
