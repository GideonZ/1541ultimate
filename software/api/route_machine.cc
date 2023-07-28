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
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, machine, reboot, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_REBOOT, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, machine, pause, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_PAUSE, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, machine, resume, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESUME, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, machine, poweroff, NULL, ARRAY( {  }))
{
    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_POWEROFF, 0);
    SubsysResultCode_t retval = cmd->execute();
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
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
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
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
        return;
    }

    if (address + datalen > 65536) {
        resp->error("Memory write exceeds location $FFFF");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    char msgbuf[16];
    sprintf(msgbuf, "%04x-%04x", address, address + datalen - 1);
    resp->json->add("address", msgbuf);

    SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, address, buffer, datalen);
    SubsysResultCode_t retval = cmd->execute();
    delete[] buffer;
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
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
    if (retval == SSRET_OK) {
        // output result in JSON format
        StreamRamFile *rf = resp->add_attachment();
        rf->write(buffer, datalen);
        delete[] buffer;
        resp->binary_response();
    } else {
        resp->error(SubsysCommand::error_string(retval));
        resp->json_response(SubsysCommand::http_response_map(retval));
        delete[] buffer;
    }
}
