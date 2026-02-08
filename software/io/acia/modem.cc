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
#define CFG_MODEM_QUIRKS      0x0D
#define CFG_MODEM_TCPNODELAY  0x0E
#define CFG_MODEM_LOOPDELAY   0x0F
#define CFG_MODEM_RTS         0x10
#define CFG_MODEM_HARDWARE    0x11
#define CFG_MODEM_RXPB        0x13

#define RESP_OK				0
#define RESP_CONNECT		1
#define RESP_RING			2
#define RESP_NO_CARRIER		3
#define RESP_ERROR			4
#define RESP_CONNECT_1200	5
#define RESP_NO_DIALTONE	6
#define RESP_BUSY			7
#define RESP_NO_ANSWER		8
#define RESP_CONNECT_2400	9
#define RESP_CONNECT_4800	10
#define RESP_CONNECT_9600	11
#define RESP_CONNECT_14400	12
#define RESP_CONNECT_19200	13
#define RESP_CONNECT_38400	14

static const char *interfaces[] = { "ACIA / SwiftLink" };
static const char *acia_mode[] = { "Off", "DE00/IRQ", "DE00/NMI", "DF00/IRQ", "DF00/NMI", "DF80/IRQ", "DF80/NMI" };
static const char *hw_mode[] = { "SwiftLink", "Turbo232" };
static const char *dcd_dsr[] = { "Active (Low)", "Active when connected", "Inactive when connected", "Inactive (High)", "Act. when connecting", "Inact. when connecting" };
static const int acia_base[] = { 0, 0xDE00, 0xDE01, 0xDF00, 0xDF01, 0xDF80, 0xDF81 };
static const char *responseCode[] = {"\r0\r","\r1\r","\r2\r","\r3\r","\r4\r","\r5\r","\r6\r","\r7\r","\r8\r",
								"\r10\r","\r11\r","\r12\r","\r13\r","\r14\r","\r28\r"};
static const char *responseText[] = {"\rOK\r","\rCONNECT\r","\rRING\r","\rNO CARRIER\r","\rERROR\r","\rCONNECT 1200\r",
								"\rNO DIALTONE\r","\rBUSY\r","\rNO ANSWER\r","\rCONNECT 2400\r","\rCONNECT 4800\r",
								"\rCONNECT 9600\r","\rCONNECT 14400\r","\rCONNECT 19200\r","\rCONNECT 38400\r"};


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
    { CFG_MODEM_ACIA,          CFG_TYPE_ENUM,   "ACIA (6551) Mapping",           "%s", acia_mode,    0,  6, 0 },
    { CFG_MODEM_HARDWARE,      CFG_TYPE_ENUM,   "Hardware Mode",                 "%s", hw_mode,      0,  1, 0 },
    { CFG_MODEM_LISTEN_PORT,   CFG_TYPE_STRING, "Listening Port",                "%s", NULL,         2,  8, (int)"3000" },
    { 0xFE,                    CFG_TYPE_SEP,    "",                              "",   NULL,         0,  0, 0 },
    { 0xFE,                    CFG_TYPE_SEP,    "Handshaking",                   "",   NULL,         0,  0, 0 },
    { CFG_MODEM_LISTEN_RING,   CFG_TYPE_ENUM,   "Do RING sequence (incoming)",   "%s", en_dis,       0,  1, 1 },
    { CFG_MODEM_DTRDROP,       CFG_TYPE_ENUM,   "Drop connection on DTR low",    "%s", en_dis,       0,  1, 1 },
    { CFG_MODEM_RTS,           CFG_TYPE_ENUM,   "RTS Handshake (Rx)",            "%s", en_dis,       0,  1, 1 },
    { CFG_MODEM_CTS,           CFG_TYPE_ENUM,   "CTS Behavior",                  "%s", dcd_dsr,      0,  5, 0 },
    { CFG_MODEM_DCD,           CFG_TYPE_ENUM,   "DCD Behavior",                  "%s", dcd_dsr,      0,  5, 0 },
    { CFG_MODEM_DSR,           CFG_TYPE_ENUM,   "DSR Behavior",                  "%s", dcd_dsr,      0,  5, 1 },
    { CFG_MODEM_RXPB,          CFG_TYPE_ENUM,   "Automatic Rx Pushback",         "%s", en_dis,       0,  1, 0 },
    { 0xFE,                    CFG_TYPE_SEP,    "",                              "",   NULL,         0,  0, 0 },
    { 0xFE,                    CFG_TYPE_SEP,    "Automated Responses",           "",   NULL,         0,  0, 0 },
    { CFG_MODEM_OFFLINEFILE,   CFG_TYPE_STRING, "Modem Offline Text",            "%s", NULL,         0, 30, (int)"/flash/offline.txt" },
    { CFG_MODEM_CONNFILE,      CFG_TYPE_STRING, "Modem Connect Text",            "%s", NULL,         0, 30, (int)"/flash/welcome.txt" },
    { CFG_MODEM_BUSYFILE,      CFG_TYPE_STRING, "Modem Busy Text",               "%s", NULL,         0, 30, (int)"/flash/busy.txt" },
    { 0xFE,                    CFG_TYPE_SEP,    "",                              "",   NULL,         0,  0, 0 },
    { 0xFE,                    CFG_TYPE_SEP,    "Tweaks",                        "",   NULL,         0,  0, 0 },
    { CFG_MODEM_TCPNODELAY,    CFG_TYPE_ENUM,   "Set Socket Opt TCP_NODELAY",    "%s", en_dis,       0,  1, 0 },
    { CFG_MODEM_LOOPDELAY,     CFG_TYPE_VALUE,  "Loop Delay",                    "%d0 ms", NULL,     1, 20, 2 },
    { CFG_TYPE_END,            CFG_TYPE_END,    "",                              "",   NULL,         0,  0, 0 } };


Modem :: Modem()
{
    register_store(0x4D4F444D, "Modem Settings", modem_cfg);

    aciaQueue = xQueueCreate(16, sizeof(AciaMessage_t));
    aciaTxBuffer = new DataBuffer(2048); // 2K transmit buffer (From C64)

    commandQueue = xQueueCreate(8, sizeof(ModemCommand_t));
    connectQueue = xQueueCreate(2, sizeof(ModemCommand_t));

    connectionLock = xSemaphoreCreateMutex();
    keepConnection = false;
    busyMode = false;
    commandMode = true;
    baudRate = 0;
    dropOnDTR = true;
    lastHandshake = 0;
    verbose = true;
    echo = true;
    current_iobase = 0;
    listenerSocket = NULL;
    ResetRegisters();
}

void Modem :: start()
{
    listenerSocket = new ListenerSocket("Modem Listener", Modem :: listenerTask, "Modem External Connection");
    xTaskCreate( Modem :: task, "Modem Task", configMINIMAL_STACK_SIZE, this, PRIO_HW_SERVICE, NULL );
    xTaskCreate( Modem :: callerTask, "Outgoing Caller", configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, NULL );
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

    modem->IncomingConnection(socketNumber);

    closesocket(socketNumber);
    vTaskDelete(NULL);
}

void Modem :: RunRelay(int socket)
{
    int loopDelay = 10 * cfg->get_value(CFG_MODEM_LOOPDELAY); // steps of 10 ms
    printf("Using loopDelay = %d\n", loopDelay);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = loopDelay * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

    if (cfg->get_value(CFG_MODEM_TCPNODELAY)) {
        int flags =1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
    }

    ModemCommand_t modemCommand;

    printf("Modem: Running Relay to socket %d.\n", socket);
    int ret;
    SetHandshakes(true, false);
    commandMode = false;
    uint8_t escape = registerValues[MODEM_REG_ESCAPE];
    int escapeTime = 4 * (int)registerValues[MODEM_REG_ESCAPETIME]; // Ultimate Timer is 200 Hz, hence *4, register specifies fiftieths of seconds
    int escapeCount = 0;
    TickType_t escapeDetectTime = 0;

    tcp_buffer_valid = 0;
    tcp_buffer_offset = 0;

    while(keepConnection) {
        // Do we have space for TCP data? Then lets try to receive some.
        if (!tcp_buffer_valid) {
            ret = recv(socket, tcp_receive_buffer, 512, 0);
            if (ret > 0) {
                //printf("[%d] TCP: %d\n", xTaskGetTickCount(), ret);
                tcp_buffer_valid = ret;
                tcp_buffer_offset= 0;
            } else if(ret == 0) {
                printf("Receive returned 0. Exiting\n");
                break;
            }
        } else {
            // we did not wait for tcp, so we just wait a little
            vTaskDelay(loopDelay);
        }

        // Do we have TCP data to send?
        if (tcp_buffer_valid) {
            int space = acia.GetRxSpace();
            // Is there room in the ACIA Rx buffer?
            if (space > 0) {
                volatile uint8_t *dest = acia.GetRxPointer();
                if (tcp_buffer_valid > space) {
                    memcpy((void *)dest, tcp_receive_buffer + tcp_buffer_offset, space);
                    acia.AdvanceRx(space);
                    //printf("[%d] Rx: Got=%d. Push:%d.\n", xTaskGetTickCount(), tcp_buffer_valid, space);
                    tcp_buffer_valid -= space;
                    tcp_buffer_offset += space;
                } else {
                    memcpy((void *)dest, tcp_receive_buffer + tcp_buffer_offset, tcp_buffer_valid);
                    acia.AdvanceRx(tcp_buffer_valid);
                    //printf("[%d] Rx: Got=%d. Push: All.\n", xTaskGetTickCount(), tcp_buffer_valid);
                    tcp_buffer_valid = 0;
                }
            }
        }

        // Check for escapes to see if we can get out of data mode
        if (escapeCount == 3) {
            TickType_t since = xTaskGetTickCount() - escapeDetectTime;
            if (since >= escapeTime) {
                printf("Modem Escape detected!\n");
                escapeCount = 0;
                commandMode = true;

                responseString = (verbose==TRUE ? responseText[RESP_OK] : responseCode[RESP_OK]);
                responseLen=strlen(responseString);
                acia.SendToRx((uint8_t*)responseString, responseLen);
            }
        }

        // As long as we are in data mode, all data written to the ACIA shall be
        // forwarded to TCP.
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
                //printf("[%d] Tx: Got=%d. Sent:%d\n", xTaskGetTickCount(), avail, ret);
                if (ret > 0) {
                    aciaTxBuffer->AdvanceReadPointer(ret);
                } else if(ret < 0) {
                    printf("Error writing to socket. Exiting\n");
                    break;
                }
            }
        }

        // Maybe there are any commands that need to be executed?
        BaseType_t cmdAvailable = xQueueReceive(commandQueue, &modemCommand, 0);
        if (cmdAvailable) {
            ExecuteCommand(&modemCommand);
            escape = registerValues[MODEM_REG_ESCAPE];
            escapeTime = 4 * (int)registerValues[MODEM_REG_ESCAPETIME]; // Ultimate Timer is 200 Hz, hence *4, register specifies fiftieths of seconds
        }
    }
    commandMode = true;
    SetHandshakes(false, false);
}

void Modem :: IncomingConnection(int socket)
{
    char buffer[64];
    if (busyMode || xSemaphoreTake(connectionLock, 0) != pdTRUE) {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_BUSYFILE), socket, "The modem you are connecting to is currently busy.\n");
        return;
    } else if(!(lastHandshake & ACIA_HANDSH_DTR)) {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_OFFLINEFILE), socket, "Modem Software is currently not running...\n");
        xSemaphoreGive(connectionLock);
        return;
    } else {
        RelayFileToSocket(cfg->get_string(CFG_MODEM_CONNFILE), socket, "Welcome to the Modem Emulation Layer of the Ultimate!\n");
    }

    SetHandshakes(false, true);
    int connChange;
    if (cfg->get_value(CFG_MODEM_LISTEN_RING)) {
        ModemCommand_t modemCommand;
        registerValues[MODEM_REG_RINGCOUNTER] = 0;
        for(int rings=0; rings < 10; rings++) {

        	responseString = (verbose==TRUE ? responseText[RESP_RING] : responseCode[RESP_RING]);
        	responseLen = strlen(responseString);
        	acia.SendToRx((uint8_t *)responseString, responseLen);

            registerValues[MODEM_REG_RINGCOUNTER]++;

            // this is removed because we dont want to send a response to the user
            // while they are dialing.  if they dial a number and they see "RING  RING"
            // it could be confused with an incoming RING. dialing out doesnt display anything
            // until either gives up or connected
            //
            //if (send(socket, responseString, responseLen, 0) <= 0) {
            //    break;
            //}
            if (registerValues[MODEM_REG_AUTOANSWER]) {
                if (registerValues[MODEM_REG_RINGCOUNTER] >= registerValues[MODEM_REG_AUTOANSWER]) {
                    keepConnection = true;
                    break;
                }
            }
            BaseType_t cmdAvailable = xQueueReceive(commandQueue, &modemCommand, 600); // wait max 3 seconds for a command
            if (cmdAvailable) {
                connChange = ExecuteCommand(&modemCommand);
                if (connChange) {
                    if (connChange == 3) { // ATH1
                        RelayFileToSocket(cfg->get_string(CFG_MODEM_BUSYFILE), socket, "The modem you are connecting to is currently busy.\n");
                    }
                    break;
                }
            }
        }

        if (keepConnection) {

        	uint8_t tmp = RESP_CONNECT;

        	switch(baudRate)
        	{
        		case 1200: { tmp=RESP_CONNECT_1200; break; }
        		case 2400: { tmp=RESP_CONNECT_2400; break; }
        		case 4800: { tmp=RESP_CONNECT_4800; break; }
        		case 9600: { tmp=RESP_CONNECT_9600; break; }
        		case 14400: { tmp=RESP_CONNECT_14400; break; }
        		case 19200: { tmp=RESP_CONNECT_19200; break; }
        		case 38400: { tmp=RESP_CONNECT_38400; break; }
        	}

        	if(verbose == TRUE) {
        		responseString = responseText[tmp];
        		responseLen = strlen(responseString);
        	} else {
        		responseString = responseCode[tmp];
        		responseLen = strlen(responseString);
        	}

            acia.SendToRx((uint8_t*)responseString, responseLen);

            // I dont believe this should be delivered locally
            // per usage of other modems
            //buffer[len-1] = 0x0a; // Use Newline instead of carriage return for the socket
            //send(socket, buffer, len, 0);

            RunRelay(socket);

            responseString = (verbose==TRUE ? responseText[RESP_NO_CARRIER] : responseCode[RESP_NO_CARRIER]);
            responseLen=strlen(responseString);
            acia.SendToRx((uint8_t*)responseString, responseLen);
        } else {
        	responseString = (verbose==TRUE ? responseText[RESP_NO_ANSWER] : responseCode[RESP_NO_ANSWER]);
        	responseLen=strlen(responseString);
            send(socket, responseString, responseLen, 0);
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

void Modem :: SetHandshakes(bool connected, bool connecting)
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
    case 4: // active when connecting OR connected
        handshakes |= (connected || connecting) ? ACIA_HANDSH_CTS : 0;
        break;
    case 5: // inactive when connecting OR connected
        handshakes |= (connected || connecting) ? 0 : ACIA_HANDSH_CTS;
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
    case 4: // active when connecting OR connected
        handshakes |= (connected || connecting) ? ACIA_HANDSH_DCD : 0;
        break;
    case 5: // inactive when connecting OR connected
        handshakes |= (connected || connecting) ? 0 : ACIA_HANDSH_DCD;
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
    case 4: // active when connecting OR connected
        handshakes |= (connected || connecting) ? ACIA_HANDSH_DSR : 0;
        break;
    case 5: // inactive when connecting OR connected
        handshakes |= (connected || connecting) ? 0 : ACIA_HANDSH_DSR;
        break;
    default:
        break;
    }

    if (rtsMode) {
        handshakes &= ~ACIA_HANDSH_RTSDIS;
    } else {
        handshakes |= ACIA_HANDSH_RTSDIS;
    }

    if (pushbackMode) {
        handshakes |= ACIA_HANDSH_RXPB;
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
        	responseString = (verbose==TRUE ? responseText[RESP_BUSY] : responseCode[RESP_BUSY]);
        	responseLen=strlen(responseString);
            acia.SendToRx((uint8_t *)responseString, responseLen );
            continue;
        }

        char *c = cmd.command;
        int len = cmd.length;
        portString = NULL;

        while(*c == 0x20) {
            c++;
            len--;
        }
        for(int i=2;i < len;i++) {
            if (c[i] == ':') {
                c[i] = 0;
                portString = &c[i + 1];
                break;
            }
        }
        // setup the connection
        int result = gethostbyname_r(c, &my_host, buffer, 128, &ret_host, &error);

        if (!ret_host) {
        	responseString = (verbose==TRUE ? responseText[RESP_NO_ANSWER] : responseCode[RESP_NO_ANSWER]);
        	responseLen=strlen(responseString);
            acia.SendToRx((uint8_t *)responseString, responseLen);
            xSemaphoreGive(connectionLock);
            continue;
        }

        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
        	responseString = (verbose==TRUE ? responseText[RESP_ERROR] : responseCode[RESP_ERROR]);
        	responseLen=strlen(responseString);
        	acia.SendToRx((uint8_t *)responseString, responseLen);
            xSemaphoreGive(connectionLock);
            continue;
        }

        int portno = 80;
        if(portString) {
            sscanf(portString, "%d", &portno);
        }
        memset((char *) &serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, ret_host->h_addr, ret_host->h_length);
        serv_addr.sin_port = htons(portno);

        if (connect(sock_fd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
        	responseString = (verbose==TRUE ? responseText[RESP_NO_CARRIER] : responseCode[RESP_NO_CARRIER]);
        	responseLen=strlen(responseString);
        	acia.SendToRx((uint8_t *)responseString, responseLen);
            xSemaphoreGive(connectionLock);
            continue;
        }
        //recv(sock_fd, (void *)buffer, 128, 0);
        //dump_hex(buffer, 128);
        // run the relay

        responseString = (verbose==TRUE ? responseText[RESP_CONNECT] : responseCode[RESP_CONNECT]);
		responseLen=strlen(responseString);
		acia.SendToRx((uint8_t *)responseString, responseLen);
        keepConnection = true;
        RunRelay(sock_fd);
        closesocket(sock_fd);
        responseString = (verbose==TRUE ? responseText[RESP_NO_CARRIER] : responseCode[RESP_NO_CARRIER]);
        responseLen=strlen(responseString);
        acia.SendToRx((uint8_t *)responseString, responseLen);
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

int Modem :: ExecuteCommand(ModemCommand_t *cmd)
{
    int connectionStateChange = 0;
    char *c;
    const char *response;
    char responseBuffer[16];
    int registerValue, temp;
    bool doesResponse = true;

    response = (verbose==TRUE ? responseText[RESP_OK] : responseCode[RESP_OK]);

    printf("MODEM COMMAND: '%s'\n", cmd->command);

    // Parse Command string
    for(int i=0; i < cmd->length; i++) {
        temp = 0;
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
            response = "ULTIMATE-II MODEM EMULATION LAYER\rMADE BY GIDEON\rVERSION V1.1\r";
            break;
        case 'Z': // reset
            ResetRegisters();
            keepConnection = false;
            connectionStateChange = 1;
            break;
        case 'H': // hang up
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;
            busyMode = (temp > 0);
            keepConnection = false;
            connectionStateChange = 1 + (busyMode) ? 2 : 0; // if busy mode is selected, return 3
            break;
        case 'A': // answer
            if (keepConnection) {
                commandMode = false;
            }
            keepConnection = true;
            connectionStateChange = 2;
            doesResponse=false;
            break;
        case 'O': // Return Online
            if (keepConnection) {
                commandMode = false;
                response = "";
            } else {
            	response = (verbose==TRUE ? responseText[RESP_NO_CARRIER] : responseCode[RESP_NO_CARRIER]);
            }
            break;
        case 'V': // Verbose mode
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;

            if(temp>1)
            {
            	response = (verbose==TRUE ? responseText[RESP_ERROR] : responseCode[RESP_ERROR]);
            }
            else
            {
            	if(temp == 1)
            		verbose=TRUE;
            	else
            		verbose=FALSE;

            	response = (verbose==TRUE ? responseText[RESP_OK] : responseCode[RESP_OK]);
            }
            break;
        case 'E':
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;

            if(temp>1)
            {
            	response = (verbose==TRUE ? responseText[RESP_ERROR] : responseCode[RESP_ERROR]);
            }
            else
            {
            	if(temp == 1)
            		echo=TRUE;
            	else
            		echo=FALSE;

            	response = (verbose==TRUE ? responseText[RESP_OK] : responseCode[RESP_OK]);
            }
            break;
        case 'M':
            sscanf(cmd->command + i + 1, "%d", &temp);
            while(i < cmd->length && (isdigit(cmd->command[i+1])))
                i++;

            if(temp>1)
            {
            	response = (verbose==TRUE ? responseText[RESP_ERROR] : responseCode[RESP_ERROR]);
            }
            else
            {
            	response = (verbose==TRUE ? responseText[RESP_OK] : responseCode[RESP_OK]);
            }
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
        	response = (verbose==TRUE ? responseText[RESP_ERROR] : responseCode[RESP_ERROR]);
            i = cmd->length; // break outer loop
            break;
        }
    }

    //ATA command does not return a response (except for CONNECT response)
    if(doesResponse)
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

    AciaMessage_t message;
    char outbuf[32];
    uint8_t txbuf[32];
    int len;
    int newRate = 0;

    ModemCommand_t modemCommand;
    modemCommand.length = 0;
    modemCommand.state = 0;

    // first time configuration
    cfg->effectuate();
    SetHandshakes(false, false);
    baudRate = baudRates[8];

    while(1) {
        xQueueReceive(aciaQueue, &message, portMAX_DELAY);
        //printf("Message type: %d. Value: %b. DataLen: %d Buffer: %d\n", message.messageType, message.smallValue, message.dataLength, aciaTxBuffer->AvailableData());
        switch(message.messageType) {
        case ACIA_MSG_CONTROL:
            newRate = baudRates[message.smallValue & 0x0F];
            if (newRate != baudRate) {
                printf("BAUD=%d\n", newRate);
                baudRate = newRate;
            }
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
            printf("Handshake bits set to %b\n", message.smallValue);
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
                if (echo) {
                    acia.SendToRx(txbuf, len); // local echo
                }
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
        if (cfg->get_value(CFG_MODEM_HARDWARE) == 1) {
            base |= 4; // set the hardware turbo enable bit (hacky hacky)
        }
    }
    current_iobase = base & 0xFFFE;

    dropOnDTR = cfg->get_value(CFG_MODEM_DTRDROP);
    ctsMode = cfg->get_value(CFG_MODEM_CTS);
    dsrMode = cfg->get_value(CFG_MODEM_DSR);
    dcdMode = cfg->get_value(CFG_MODEM_DCD);
    rtsMode = cfg->get_value(CFG_MODEM_RTS);
    pushbackMode = cfg->get_value(CFG_MODEM_RXPB);

    SetHandshakes(false, false);

    // Turn on after the default handshakes have been set correctly
    if (base) {
        acia.init(base & 0xFFFE, base & 1, aciaQueue, aciaQueue, aciaTxBuffer);
    }

    listenerSocket->Start(newPort);
}

void Modem :: reinit_acia(uint16_t base)
{
    if (base == 0xFFFF) {
        int basecfg = acia_base[cfg->get_value(CFG_MODEM_ACIA)];
        if (!basecfg) {
            acia.deinit();
            current_iobase = 0;
        } else {
            acia.init(basecfg & 0xFFFE, basecfg & 1, aciaQueue, aciaQueue, aciaTxBuffer);
            current_iobase = basecfg & 0xFFFE;
        }
    } else {
        acia.init(base & 0xFFFE, base & 1, aciaQueue, aciaQueue, aciaTxBuffer);
        current_iobase = base & 0xFFFE;
    }
}

bool Modem :: prohibit_acia(uint16_t base)
{
    if (base == current_iobase) {
        acia.deinit();
        return true;
    }
    return false;
}

#include "init_function.h"
Modem *modem = NULL;
InitFunction init_modem("Modem", [](void *_obj, void *_param) {
    if (getFpgaCapabilities() & CAPAB_ACIA) {
        modem = new Modem();
        modem->start();
    }
}, NULL, NULL, 105); // global that causes us to exist
