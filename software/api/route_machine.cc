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

#include "i2c_drv.h"

API_CALL(GET, i2c, scan, NULL, ARRAY({}))
{
    StreamRamFile *log = resp->raw_response(200, "text/html");

    log->format("<html><body><h1>I2C Address Probe Map</h1>\n<p>");
    I2C_Driver i2c;

    for (int i = 0; i < 255; i += 1) {
        i2c.i2c_start();
        int res = i2c.i2c_send_byte((uint8_t)i);
        log->format("[%2x:%c] ", i, (res < 0) ? '-' : 'X');
        i2c.i2c_stop();
        i2c.i2c_start();
        i2c.i2c_stop();
        if (i % 16 == 15) {
            log->format("<br>\n");
        }
    }
    log->format("</p></body></html>\r\n\r\n");
}

API_CALL(GET, i2c, dump, NULL, ARRAY({  {"dev", P_REQUIRED} }))
{
    int dev  = strtol(args["dev"],  NULL, 16);

    I2C_Driver i2c;
    uint8_t buf[256];
    int ret = i2c.i2c_read_block(dev, 0, buf, 256);
    const char *style = "<style> table, th, td { border: 1px solid black; border-collapse: collapse; } </style>\n";

    if (!ret) {
        StreamRamFile *log = resp->raw_response(200, "text/html");
        log->format("<html>%s<body><h1>I2C Dump of Device %02x</h1>\n<p>", style, dev);
        log->format("<table>\n");
        log->format("<tr><th/>");
        for (int i=0;i<16;i++) {
            log->format("<th>0x%x</th>", i);
        } log->format("</tr>\n");
        for (int y=0;y<16;y++) {
            log->format("<tr><th>0x%x</th>", 16*y);
            for (int x=0;x<16;x++) {
                log->format("<td>%02x</td>", buf[16*y+x]);
            }
            log->format("</tr>\n");
        }
        log->format("</table>");
        log->format("</body></html>\r\n\r\n");
    } else {
        resp->json->add("return_code", ret);
        resp->json_response(200);
    }
}

API_CALL(GET, i2c, write, NULL, ARRAY({  {"dev", P_REQUIRED}, {"addr", P_REQUIRED}, {"data", P_REQUIRED } }))
{
    int dev  = strtol(args["dev"],  NULL, 16);
    int addr = strtol(args["addr"], NULL, 16);
    int data = strtol(args["data"], NULL, 16);

    I2C_Driver i2c;
    int ret = i2c.i2c_write_byte(dev, addr, data);
    resp->json->add("return_code", ret);
    resp->json_response(200);
}

API_CALL(GET, i2c, read, NULL, ARRAY({  {"dev", P_REQUIRED}, {"addr", P_REQUIRED} }))
{
    int dev  = strtol(args["dev"],  NULL, 16);
    int addr = strtol(args["addr"], NULL, 16);

    I2C_Driver i2c;
    int ret;
    uint8_t value = i2c.i2c_read_byte(dev, addr, &ret);
    resp->json->add("return_code", ret);
    resp->json->add("data", ret);
    resp->json_response(200);
}
