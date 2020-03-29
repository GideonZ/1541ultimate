#include "modem.h"
#include "semphr.h"

#include "socket.h"
#include "netdb.h"
#include <ctype.h>

#include "filemanager.h"
#include "dump_hex.h"

#define CFG_MODEM_INTF        0x01
#define CFG_MODEM_ACIA        0x02
#define CFG_MODEM_LISTEN_PORT 0x03
#define CFG_MODEM_DTRDROP     0x05
#define CFG_MODEM_DCD         0x06
#define CFG_MODEM_DSR         0x07
#define CFG_MODEM_CTS         0x08
#define CFG_MODEM_BUSYFILE    0x09
#define CFG_MODEM_CONNFILE    0x0A
#define CFG_MODEM_OFFLINEFILE 0x0B
#define CFG_MODEM_LISTEN_RING 0x0C

static const char *interfaces[] = { "ACIA / SwiftLink" };
static const char *acia_mode[] = { "Off", "DE00/IRQ", "DE00/NMI", "DF00/IRQ", "DF00/NMI", "DF80/IRQ", "DF80/NMI" };
static const char *dcd_dsr[] = { "Active (Low)", "Active when connected", "Inactive when connected", "Inactive (High)" };
static const int acia_base[] = { 0, 0xDE00, 0xDE01, 0xDF00, 0xDF01, 0xDF80, 0xDF81 };

/*
static const AciaMessage_t setDSR = { ACIA_MSG_DSR, 1, 0 };
static const AciaMessage_t clrDSR = { ACIA_MSG_DSR, 0, 0 };
static const AciaMessage_t setDCD = { ACIA_MSG_DCD, 1, 0 };
static const AciaMessage_t clrDCD = { ACIA_MSG_DCD, 0, 0 };
static const AciaMessage_t setCTS = { ACIA_MSG_CTS, 1, 0 };
static const AciaMessage_t clrCTS = { ACIA_MSG_CTS, 0, 0 };
static const AciaMessage_t rxData = { ACIA_MSG_RXDATA, 0, 0 };
*/

struct t_cfg_definition modem_cfg[] = {
    { CFG_MODEM_INTF,          CFG_TYPE_ENUM,   "Modem Interface",               "%s", interfaces,   0,  0, 0 },
    { CFG_MODEM_ACIA,          CFG_TYPE_ENUM,   "ACIA (6551) Mode",              "%s", acia_mode,    0,  6, 0 },
    { CFG_MODEM_LISTEN_PORT,   CFG_TYPE_STRING, "Listening Port",                "%s", NULL,         2,  8, (int)"3000" },
    { CFG_MODEM_LISTEN_RING,   CFG_TYPE_ENUM,   "Do RING sequence (incoming)",   "%s", en_dis,       0,  1, 1 },
    { CFG_MODEM_DTRDROP,       CFG_TYPE_ENUM,   "Drop connection on DTR=0",      "%s", en_dis,       0,  1, 1 },
    { CFG_MODEM_CTS,           CFG_TYPE_ENUM,   "CTS Behavior",                  "%s", dcd_dsr,      0,  3, 0 },
    { CFG_MODEM_DCD,           CFG_TYPE_ENUM,   "DCD Behavior",                  "%s", dcd_dsr,      0,  3, 1 },
    { CFG_MODEM_DSR,           CFG_TYPE_ENUM,   "DSR Behavior",                  "%s", dcd_dsr,      0,  3, 1 },
    { CFG_MODEM_OFFLINEFILE,   CFG_TYPE_STRING, "Modem Offline Text",            "%s", NULL,         0, 30, (int)"/Usb0/offline.txt" },
    { CFG_MODEM_CONNFILE,      CFG_TYPE_STRING, "Modem Connect Text",            "%s", NULL,         0, 30, (int)"/Usb0/welcome.txt" },
    { CFG_MODEM_BUSYFILE,      CFG_TYPE_STRING, "Modem Busy Text",               "%s", NULL,         0, 30, (int)"/Usb0/busy.txt" },
    { CFG_TYPE_END,            CFG_TYPE_END,    "",                              "",   NULL,         0,  0, 0 } };


Modem :: Modem()
{
    if (!(getFpgaCapabilities() & CAPAB_ACIA)) {
        return;
    }
    register_store(0x4D4F444D, "Modem Settings", modem_cfg);

    aciaQueue = xQueueCreate(16, sizeof(AciaMessage_t));
    aciaTxBuffer = new DataBuffer(2048); // 2K transmit buffer (From C64)

    commandQueue = xQueueCreate(8, sizeof(ModemCommand_t));
    connectQueue = xQueueCreate(2, sizeof(ModemCommand_t));

    connectionLock = xSemaphoreCreateMutex();
    xTaskCreate( Modem :: task, "Modem Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    xTaskCreate( Modem :: callerTask, "Outgoing Caller", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    listenerSocket = new ListenerSocket("Modem Listener", Modem :: listenerTask, "Modem External Connection");
    keepConnection = false;
    commandMode = true;
    baudRate = 0;
    dropOnDTR = true;
    lastHandshake = 0;
    ResetRegisters();
}

/*
Modem :: ~Modem()
{

}
*/

void Modem :: task(void *a)
{
    Modem *obj = (Modem *)a;
    obj->ModemTask();
}

void Modem :: callerTask(void *a)
{
    Modem *obj = (Modem *)a;
    obj->Caller();
}

void Modem :: listenerTask(void *a)
{
    int socketNumber = (int)a;
    char buffer[64];
    //int len = sprintf(buffer, "You are connected to the modem!\n");
    //send(socketNumber, buffer, len, 0);

    modem.IncomingConnection(socketNumber);

    lwip_close(socketNumber);
    vTaskDelete(NULL);
}

void Modem :: RunRelay(int socket)
{
    struct timeval tv;
    tv.tv_sec = 10; // bug in lwip; this is just used directly as tick value
    tv.tv_usec = 10;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    //currentRelaySocket = socket;
    ModemCommand_t modemCommand;

    printf("Modem: Running Relay to socket %d.\n", socket);
    int ret;
    SetHandshakes(true);
    commandMode = false;
    uint8_t escape = registerValues[MODEM_REG_ESCAPE];
    int escapeTime = 4 * (int)registerValues[MODEM_REG_ESCAPETIME]; // Ultimate Timer is 200 Hz, hence *4, register specifies fiftieths of seconds
    int escapeCount = 0;
    TickType_t escapeDetectTime = 0;

    while(keepConnection) {
        int space = acia.GetRxSpace();
        volatile uint8_t *dest = acia.GetRxPointer();
        if (space > 0) {
            //printf("RELAY: Recv %p %d ", dest, space);
            ret = recv(socket, (void *)dest, space, 0);
            if (ret > 0) {
                acia.AdvanceRx(ret);
                //printf("%d\n", ret);
            } else if(ret == 0) {
                printf("Receive returned 0. Exiting\n");
                break;
            }
        } else {
            vTaskDelay(10);
        }
        if (escapeCount == 3) {
            TickType_t since = xTaskGetTickCount() - escapeDetectTime;
            if (since >= escapeTime) {
                printf("Modem Escape detected!\n");
                escapeCount = 0;
                commandMode = true;
            }
        }
        if (!commandMode) {
            int avail = aciaTxBuffer->AvailableContiguous();
            if (avail > 0) {
                uint8_t *pnt = aciaTxBuffer->GetReadPointer();
                for(int i=0;i<avail;i++) {
                    if (pnt[i] == escape) {
                        escapeCount ++;
                        if (escapeCount == 3) {
                            escapeDetectTime = xTaskGetTickCount();
                        }
                    } else {
                        escapeCount = 0;
                    }
                }
                ret = send(socket, pnt, avail, 0);
                if (ret > 0) {
                    aciaTxBuffer->AdvanceReadPointer(ret);
                } else if(ret < 0) {
                    printf("Error writing to socket. Exiting\n");
                    break;
                }
            }
        }
        BaseType_t cmdAvailable = xQueueReceive(commandQueue, &modemCommand, 0);
        if (cmdAvailable) {
            ExecuteCommand(&modemCommand);
            escape = registerValues[MODEM_REG_ESCAPE];
            escapeTime = 4 * (int)registerValues[MODEM_REG_ESCAPETIME]; // Ultimate Timer is 200 Hz, hence *4, register specifies fiftieths of seconds
        }
    }
    commandMode = true;
    SetHandshakes(false);
}

void Modem :: IncomingConnection(int socket)
{
    char buffer[64];
    if (xSemaphoreTake(connectionLock, 0) != pdTRUE) {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_BUSYFILE), socket, "The modem you are connecting to is currently busy.\n");
        return;
    } else if(!(lastHandshake & ACIA_HANDSH_DTR)) {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_OFFLINEFILE), socket, "Modem Software is currently not running...\n");
        xSemaphoreGive(connectionLock);
        return;
    } else {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_CONNFILE), socket, "Welcome to the Modem Emulation Layer of the Ultimate!\n");
    }

    if (cfg->get_value(CFG_MODEM_LISTEN_RING)) {
        ModemCommand_t modemCommand;
        registerValues[MODEM_REG_RINGCOUNTER] = 0;
        for(int rings=0; rings < 10; rings++) {
            acia.SendToRx((uint8_t *)"RING\r", 5);
            int len = sprintf(buffer, "RING\n");
            registerValues[MODEM_REG_RINGCOUNTER] ++;
            if (send(socket, buffer, len, 0) <= 0) {
                break;
            }
            if (registerValues[MODEM_REG_AUTOANSWER]) {
                if (registerValues[MODEM_REG_RINGCOUNTER] >= registerValues[MODEM_REG_AUTOANSWER]) {
                    keepConnection = true;
                    break;
                }
            }
            BaseType_t cmdAvailable = xQueueReceive(commandQueue, &modemCommand, 600); // wait max 3 seconds for a command
            if (cmdAvailable) {
                if (ExecuteCommand(&modemCommand)) {
                    break;
                }
            }
        }

        if (keepConnection) {
            int len = sprintf(buffer, "CONNECT %d\r", baudRate);
            acia.SendToRx((uint8_t*)buffer, len);
            buffer[len-1] = 0x0a; // Use Newline instead of carriage return for the socket
            send(socket, buffer, len, 0);

            RunRelay(socket);
            len = sprintf(buffer, "NO CARRIER\r");
            acia.SendToRx((uint8_t*)buffer, len);
        } else {
            int len = sprintf(buffer, "NO ANSWER\n");
            send(socket, buffer, len, 0);
        }
    } else {
        // Silent relay
        keepConnection = true;
        RunRelay(socket);
    }
    keepConnection = false;
    xSemaphoreGive(connectionLock);
    commandMode = true;
}

void Modem :: SetHandshakes(bool connected)
{
    uint8_t handshakes = 0;

    switch(ctsMode) {
    case 0: // active
        handshakes |= ACIA_HANDSH_CTS;
        break;
    case 1: // active when connected
        handshakes |= (connected) ? ACIA_HANDSH_CTS : 0;
        break;
    case 2: // inactive when connected
        handshakes |= (connected) ? 0 : ACIA_HANDSH_CTS;
        break;
    default:
        break;
    }

    switch(dcdMode) {
    case 0: // active
        handshakes |= ACIA_HANDSH_DCD;
        break;
    case 1: // active when connected
        handshakes |= (connected) ? ACIA_HANDSH_DCD : 0;
        break;
    case 2: // inactive when connected
        handshakes |= (connected) ? 0 : ACIA_HANDSH_DCD;
        break;
    default:
        break;
    }

    switch(dsrMode) {
    case 0: // active
        handshakes |= ACIA_HANDSH_DSR;
        break;
    case 1: // active when connected
        handshakes |= (connected) ? ACIA_HANDSH_DSR : 0;
        break;
    case 2: // inactive when connected
        handshakes |= (connected) ? 0 : ACIA_HANDSH_DSR;
        break;
    default:
        break;
    }

    AciaMessage_t setHS = { ACIA_MSG_SETHS, 0, 0 };
    setHS.smallValue = handshakes;
    xQueueSend(aciaQueue, &setHS, portMAX_DELAY);
}

void Modem :: Caller()
{
    ModemCommand_t cmd;
    char *portString = NULL;
    int error;
    struct hostent my_host, *ret_host;
    char buffer[128];
    struct sockaddr_in serv_addr;

    while(1) {
        xQueueReceive(connectQueue, &cmd, portMAX_DELAY);
        if (xSemaphoreTake(connectionLock, 50) != pdTRUE) {
            acia.SendToRx((uint8_t *)"MODEM BUSY\r", 11);
            continue;
        }

        for(int i=2;i<cmd.length;i++) {
            if (cmd.command[i] == ':') {
                cmd.command[i] = 0;
                portString = &cmd.command[i + 1];
                break;
            }
        }
        // setup the connection
        int result = gethostbyname_r(cmd.command, &my_host, buffer, 128, &ret_host, &error);

        if (!ret_host) {
            acia.SendToRx((uint8_t *)"RESOLVE ERROR\r", 14);
            xSemaphoreGive(connectionLock);
            continue;
        }

        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            acia.SendToRx((uint8_t *)"NO SOCKET\r", 10);
            xSemaphoreGive(connectionLock);
            continue;
        }

        int portno = 80;
        if(portString) {
            sscanf(portString, "%d", &portno);
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, ret_host->h_addr, ret_host->h_length);
        serv_addr.sin_port = htons(portno);

        if (connect(sock_fd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
            acia.SendToRx((uint8_t *)"NO CARRIER\r", 14);
            xSemaphoreGive(connectionLock);
            continue;
        }
        //recv(sock_fd, (void *)buffer, 128, 0);
        //dump_hex(buffer, 128);
        // run the relay

        acia.SendToRx((uint8_t *)"CONNECTED\r", 10);
        keepConnection = true;
        RunRelay(sock_fd);
        lwip_close(sock_fd);
        acia.SendToRx((uint8_t *)"NO CARRIER\r", 14);
        keepConnection = false;
        xSemaphoreGive(connectionLock);
    }
}

void Modem :: CollectCommand(ModemCommand_t *cmd, char *buf, int len)
{
    for(int i=0; i<len; i++) {
        char c = toupper(buf[i]);

        switch (cmd->state) {
        case 0:
            if (c == 'A') {
                cmd->state = 1;
            }
            break;
        case 1:
            if (c == 'T') {
                cmd->state = 2;
                cmd->length = 0;
            } else {
                cmd->state = 0;
            }
            break;
        case 2:
            if ((c == 0x08) || (c == 0x14)) {
                if (cmd->length > 0) {
                    cmd->length --;
                    cmd->command[cmd->length] = 0;
                }
            } else if (c == 0x0D) {
                cmd->state = 3;
                cmd->command[cmd->length] = 0;
                break;
            } else if(cmd->length < MAX_MODEM_COMMAND_LENGTH) {
                cmd->command[cmd->length ++] = c;
            }
            break;
        default:
            break;
        }
    }
}

bool Modem :: ExecuteCommand(ModemCommand_t *cmd)
{
    bool connectionStateChange = false;
    char *c;
    const char *response = "OK\r";
    char responseBuffer[16];
    int registerValue, temp;

    printf("MODEM COMMAND: '%s'\n", cmd->command);

    // Parse Command string
    for(int i=0; i < cmd->length; i++) {
        switch(cmd->command[i]) {
        case 'D': // Dial
            c = cmd->command; // overwrite current command
            for(int j=i+2; j < cmd->length; j++) {
                *(c++) = cmd->command[j];
            }
            *(c++) = 0;
            printf("I need to make an outgoing call to '%s'!\n", cmd->command);
            xQueueSend(connectQueue, cmd, 10);
            i = cmd->length; // break outer loop
            response = "";
            break;
        case 'I': // identify
            response = "ULTIMATE-II MODEM EMULATION LAYER\rMADE BY GIDEON\rVERSION V1.0\r";
            break;
        case 'Z': // reset
            ResetRegisters();
            keepConnection = false;
            connectionStateChange = true;
            break;
        case 'H': // hang up
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;
            if (temp == 0) {
                keepConnection = false;
            } else {
                if (keepConnection) {
                    response = "";
                } else {
                    response = "NO DIALTONE\r";
                }
            }
            connectionStateChange = true;
            break;
        case 'A': // answer
            if (keepConnection) {
                commandMode = false;
            }
            keepConnection = true;
            connectionStateChange = true;
            break;
        case 'O': // Return Online
            if (keepConnection) {
                commandMode = false;
                response = "";
            } else {
                response = "NO CARRIER\r";
            }
            break;
        case 'V': // Verbose mode
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;
            break;
        case 'S': // register select
            registerSelect = 0;
            sscanf(cmd->command + i + 1, "%d", &registerSelect);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;
            break;
        case '?': // print register value
            sprintf(responseBuffer, "%d\r", ReadRegister());
            response = responseBuffer;
            break;
        case '=':
            registerValue = 0;
            sscanf(cmd->command + i + 1, "%d", &registerValue);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;
            WriteRegister(registerValue);
            break;
        default:
            response = "?\r";
            i = cmd->length; // break outer loop
            break;
        }
    }
    acia.SendToRx((uint8_t *)response, strlen(response));
    return connectionStateChange;
}

void Modem :: ResetRegisters()
{
    const uint8_t defaults[MODEM_NUM_REGS] = { 0, 0, 43, 13, 10, 8, 2, 50, 2, 6, 14, 95, 50, 0, 0, 0 };
    memcpy(registerValues, defaults, MODEM_NUM_REGS);
}

void Modem :: WriteRegister(int value)
{
    printf("MODEM: Write Register %d to %d.\n", registerSelect, value);
    if (registerSelect < MODEM_NUM_REGS) {
        registerValues[registerSelect] = (uint8_t)value;
    }
}

int Modem :: ReadRegister()
{
    if (registerSelect < MODEM_NUM_REGS) {
        return (int)registerValues[registerSelect];
    }
    return 0;
}

void Modem :: ModemTask()
{
    const int baudRates[]      = {   -1,  100,  150,  220, 269,   300,  600, 1200, 2400, 3600, 4800, 7200, 9600, 14400, 19200, 38400 };
    const uint8_t rateValues[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x82, 0x56, 0x41, 0x2B, 0x20, 0x16, 0x10, 0x08 };

    AciaMessage_t message;
    char outbuf[32];
    uint8_t txbuf[32];
    int len;

    ModemCommand_t modemCommand;
    modemCommand.length = 0;
    modemCommand.state = 0;

    // first time configuration
    cfg->effectuate();
    acia.SetRxRate(rateValues[8]);
    baudRate = baudRates[8];

    while(1) {
        xQueueReceive(aciaQueue, &message, portMAX_DELAY);
        //printf("Message type: %d. Value: %b. DataLen: %d Buffer: %d\n", message.messageType, message.smallValue, message.dataLength, aciaTxBuffer->AvailableData());
        switch(message.messageType) {
        case ACIA_MSG_CONTROL:
            printf("BAUD=%d\n", baudRates[message.smallValue & 0x0F]);
            acia.SetRxRate(rateValues[message.smallValue & 0x0F]);
            baudRate = baudRates[message.smallValue & 0x0F];
            //acia.SendToRx((uint8_t *)outbuf, strlen(outbuf));
            break;
/*
        case ACIA_MSG_DCD:
            acia.SetDCD(message.smallValue);
            break;
        case ACIA_MSG_DSR:
            acia.SetDSR(message.smallValue);
            break;
        case ACIA_MSG_CTS:
            acia.SetCTS(message.smallValue);
            break;
*/
        case ACIA_MSG_SETHS:
            acia.SetHS(message.smallValue);
            break;
        case ACIA_MSG_HANDSH:
            //printf("HANDSH=%b\n", message.smallValue);
            lastHandshake = message.smallValue;
            if (!(message.smallValue & ACIA_HANDSH_DTR) && dropOnDTR) {
                keepConnection = false;
                //commandMode = true;
            }
            //acia.SendToRx((uint8_t *)outbuf, strlen(outbuf));
            break;
        case ACIA_MSG_TXDATA:
            if (commandMode) {
                len = aciaTxBuffer->Get(txbuf, 30);
                acia.SendToRx(txbuf, len); // local echo
                txbuf[len] = 0;
                CollectCommand(&modemCommand, (char *)txbuf, len);
                if (modemCommand.state == 3) {
                    // Let's check if the modem is in a call
                    if (xSemaphoreTake(connectionLock, 0) != pdTRUE) {
                        if (xQueueSend(commandQueue, &modemCommand, 10) == pdFALSE) {
                            acia.SendToRx((uint8_t *)"NAK\r", 4);
                        }
                    } else {
                        // we were not in a call, release the semaphore that we got
                        xSemaphoreGive(connectionLock);
                        // Not in a call, we just execute the command right away
                        ExecuteCommand(&modemCommand);
                    }
                    modemCommand.state = 0;
                    modemCommand.length = 0;
                }
            } // else: relay thread will take the data and send it to the socket
            break;
        }
    }
}

void Modem :: RelayFileToSocket(const char *filename, int socket, const char *alt)
{
    // Simply do nothing if filename is empty
    if (strlen(filename) == 0) {
        return;
    }

    FileManager *fm = FileManager :: getFileManager();
    File *f;
    uint32_t tr = 0;
    FRESULT fres = fm->fopen(filename, FA_READ, &f);

    uint8_t *buffer = new uint8_t[512];
    if (fres == FR_OK) {
        do {
            f->read(buffer, 512, &tr);
            if (tr) {
                send(socket, buffer, tr, 0);
            }
        } while(tr == 512);
        fm->fclose(f);
    } else if (alt) {
        send(socket, alt, strlen(alt), 0);
    }
    delete[] buffer;
}

void Modem :: effectuate_settings()
{
    int newPort;
    sscanf(cfg->get_string(CFG_MODEM_LISTEN_PORT), "%d", &newPort);

    int base = acia_base[cfg->get_value(CFG_MODEM_ACIA)];
    if (!base) {
        acia.deinit();
    } else {
        acia.init(base & 0xFFFE, base & 1, aciaQueue, aciaQueue, aciaTxBuffer);
    }

    dropOnDTR = cfg->get_value(CFG_MODEM_DTRDROP);
    ctsMode = cfg->get_value(CFG_MODEM_CTS);
    dsrMode = cfg->get_value(CFG_MODEM_DSR);
    dcdMode = cfg->get_value(CFG_MODEM_DCD);
    listenerSocket->Start(newPort);
}

Modem modem;
