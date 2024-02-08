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

static int hexascii2buf(const char *data, ResponseWrapper *resp, uint8_t *buf)
{
    int datalen = strlen(data) >> 1;
    if (datalen > 128) {
        resp->error("Maximum length of 128 bytes exceeded. Consider using POST method with attachment.");
        resp->json_response(HTTP_BAD_REQUEST);
        return -1;
    }
    if (datalen < 1) {
        resp->error("Use this API call to write at least one byte!");
        resp->json_response(HTTP_BAD_REQUEST);
        return -1;
    }
    for (int i=0;i<datalen; i++) {
        uint8_t b1 = chartohex(data[0]);
        if (b1 == 0xFF) {
            resp->error("Invalid char '%c' at position %d.", data[0], 2*i);
            resp->json_response(HTTP_BAD_REQUEST);
            return -1;
        }
        uint8_t b2 = chartohex(data[1]);
        if (b2 == 0xFF) {
            resp->error("Invalid char '%c' at position %d.", data[1], 2*i+1);
            resp->json_response(HTTP_BAD_REQUEST);
            return -1;
        }
        buf[i] = (b1 << 4)|b2;
        data += 2;
    }
    return datalen;
}

API_CALL(PUT, machine, writemem, NULL, ARRAY( { {"address", P_REQUIRED}, {"data", P_REQUIRED} }))
{
    int address = strtol(args["address"], NULL, 16);

    if ((address < 0) || (address > 65535)) {
        resp->error("Invalid address");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    uint8_t buf[128];
    const char *data = args["data"];
    int datalen = hexascii2buf(data, resp, buf);

    if (datalen < 0) {
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

static const char *style = "<style> table, th, td { border: 1px solid black; border-collapse: collapse; } </style>\n";

static void output_table(uint8_t *buf, int len, StreamRamFile *log)
{
    int width = (len >= 16)?16:len;
    int rows = (len + width - 1) / width;

    log->format("<table>\n");
    log->format("<tr><th/>");
    for (int i=0;i<width;i++) {
        log->format("<th>0x%1x</th>", i);
    } log->format("</tr>\n");
    for (int y=0;y<rows;y++) {
        log->format("<tr><th>0x%02x</th>", width*y);
        for (int x=0;x<width;x++) {
            if (width*y + x > len) {
                break;
            }
            log->format("<td>%02x</td>", buf[width*y+x]);
        }
        log->format("</tr>\n");
    }
    log->format("</table><br>");
}

API_CALL(GET, i2c, dump, NULL, ARRAY({  {"dev", P_REQUIRED} }))
{
    int dev  = strtol(args["dev"],  NULL, 16);

    I2C_Driver i2c;
    uint8_t buf[256];
    int ret = i2c.i2c_read_block(dev, 0, buf, 256);

    if (!ret) {
        StreamRamFile *log = resp->raw_response(200, "text/html");
        log->format("<html>%s<body><h1>I2C Dump of Device %02x</h1>\n<p>", style, dev);
        output_table(buf, 256, log);
        log->format("</body></html>\r\n\r\n");
    } else {
        resp->json->add("return_code", ret);
        resp->json_response(200);
    }
}

// static int read_824(I2CDriver& i2c, uint8_t addr, uint8_t *buf, int len)
// {
//     uint8_t local[20];
//     while(len) {
//         i2c.i2c_read_block(0xc8, addr, local, 17);

//     }
// }

static void express_pll(StreamRamFile *log, uint8_t *buf, int pll)
{
    
    const char *vco_range[] = { "< 125 MHz", "125...150 MHz", "150...175 MHz", "> 175 MHz"};
    int n, r, q, p, range, div1, div2;

    const char *out_def[] = { 
        "Outputs disabled to high-impedance state (PLL is in power down)",
        "Outputs disabled to high-impedance state (PLL on)",
        "Outputs disabled to low (PLL on)",
        "Outputs enabled (normal operation, PLL on)",
    };
    const char *y1[] = { "Bypass", "Pdiv2"};
    const char *y2[] = { "Bypass", "Pdiv2", "Pdiv3", "Reserved" };
    const char *y3[] = { "Pdiv2", "Pdiv4"};
    const char *y4[] = { "Pdiv2", "Pdiv4", "Pdiv5", "Reserved" };

    log->format("<table>\n");
    log->format("<tr><th>Parameter</th><th>Value</th>\n");

    log->format("<tr><td>State 0</td><td>%s</td></tr>\n", out_def[buf[4] & 3]);
    log->format("<tr><td>State 1</td><td>%s</td></tr>\n", out_def[(buf[4] >> 2) & 3]);
    log->format("<tr><td>PLL</td><td>%s</td></tr>\n", (buf[4] & 0x80)?"Bypass (Off)":"Enabled (On)");

    if (pll) {
        log->format("<tr><td>Output Y3</td><td>%s</td></tr>\n", y3[(buf[4] >> 6) & 1]);
        log->format("<tr><td>Output Y4</td><td>%s</td></tr>\n", y4[(buf[4] >> 4) & 3]);
    } else {
        log->format("<tr><td>Output Y1</td><td>%s</td></tr>\n", y1[(buf[4] >> 6) & 1]);
        log->format("<tr><td>Output Y2</td><td>%s</td></tr>\n", y2[(buf[4] >> 4) & 3]);
    }

    div1 = buf[6] & 0x7F;
    div2 = buf[7] & 0x7F;
    log->format("<tr><td>Pdiv%d</td><td>%d</td></tr>\n", 2+2*pll, div1);
    log->format("<tr><td>Pdiv%d</td><td>%d</td></tr>\n", 3+2*pll, div2);
    if (pll) {
        log->format("<tr><td>Spread</td><td>%s</td></tr>\n", (buf[6] & 0x80)?"Center":"Down");
    }

    n = (buf[8] << 4) | (buf[9] >> 4);
    r = ((buf[9] & 15) << 5) | (buf[10] >> 3);
    q = ((buf[10] & 7) << 3) | (buf[11] >> 5);
    p = ((buf[11] & 0x1c) >> 2);
    range = buf[11] & 3;

    log->format("<tr><td>N(0)</td><td>%d</td></tr>\n", n);
    log->format("<tr><td>R(0)</td><td>%d</td></tr>\n", r);
    log->format("<tr><td>Q(0)</td><td>%d</td></tr>\n", q);
    log->format("<tr><td>P(0)</td><td>%d</td></tr>\n", p);
    log->format("<tr><td>Range(0)</td><td>%s</td></tr>\n", vco_range[range]);

    n = (buf[12] << 4) | (buf[13] >> 4);
    r = ((buf[13] & 15) << 5) | (buf[14] >> 3);
    q = ((buf[14] & 7) << 3) | (buf[15] >> 5);
    p = ((buf[15] & 0x1c) >> 2);
    range = buf[15] & 3;

    log->format("<tr><td>N(1)</td><td>%d</td></tr>\n", n);
    log->format("<tr><td>R(1)</td><td>%d</td></tr>\n", r);
    log->format("<tr><td>Q(1)</td><td>%d</td></tr>\n", q);
    log->format("<tr><td>P(1)</td><td>%d</td></tr>\n", p);
    log->format("<tr><td>Range(1)</td><td>%s</td></tr>\n", vco_range[range]);

    log->format("</table><br>");
}

API_CALL(GET, i2c, read824, NULL, ARRAY({  }))
{
    StreamRamFile *log = resp->raw_response(200, "text/html");
    log->format("<html>%s<body><h1>I2C Dump Clock Device</h1>\n<p>", style);
    uint8_t buf[20];
    int ret;
    I2C_Driver i2c;

    i2c.i2c_write_byte(0xc8, 0x86, 0x0E); // read/write 7 bytes at a time

    ret = i2c.i2c_read_block(0xc8, 0x00, buf, 17);
    if (!ret) {
        log->format("<h2>Generic Configuration</h2>");
        output_table(buf+1, buf[0], log);
    }

    i2c.i2c_write_byte(0xc8, 0x86, 0x20); // read/write 16 bytes at a time

    ret = i2c.i2c_read_block(0xc8, 0x10, buf, 17);
    if (!ret) {
        log->format("<h2>PLL 1</h2>");
        output_table(buf+1, buf[0], log);
    }
    express_pll(log, buf+1, 0);

    ret = i2c.i2c_read_block(0xc8, 0x20, buf, 17);
    if (!ret) {
        log->format("<h2>PLL 2</h2>");
        output_table(buf+1, buf[0], log);
    }
    express_pll(log, buf+1, 1);
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

API_CALL(GET, i2c, bwrite, NULL, ARRAY({  {"dev", P_REQUIRED}, {"addr", P_REQUIRED}, {"data", P_REQUIRED } }))
{
    int dev  = strtol(args["dev"],  NULL, 16);
    int addr = strtol(args["addr"], NULL, 16);

    uint8_t buf[128];
    const char *data = args["data"];
    int datalen = hexascii2buf(data, resp, buf);
    if (datalen < 0) {
        return;
    }

    I2C_Driver i2c;
    int ret = i2c.i2c_write_block(dev, addr, buf, datalen);
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
