#include "modem.h"
#include "semphr.h"

#include "socket.h"
#include "netdb.h"
#include <ctype.h>

#include "dump_hex.h"

#define CFG_MODEM_INTF        0x01
#define CFG_MODEM_ACIA        0x02
#define CFG_MODEM_LISTEN_PORT 0x03

static const char *interfaces[] = { "ACIA / SwiftLink" };
static const char *acia_mode[] = { "Off", "DE00/IRQ", "DE00/NMI", "DF00/IRQ", "DF00/NMI", "DF80/IRQ", "DF80/NMI" };
static const int acia_base[] = { 0, 0xDE00, 0xDE01, 0xDF00, 0xDF01, 0xDF80, 0xDF81 };

struct t_cfg_definition modem_cfg[] = {
    { CFG_MODEM_INTF,          CFG_TYPE_ENUM,   "Modem Interface",               "%s", interfaces,   0,  0, 0 },
    { CFG_MODEM_ACIA,          CFG_TYPE_ENUM,   "ACIA (6551) Mode",              "%s", acia_mode,    0,  6, 0 },
    { CFG_MODEM_LISTEN_PORT,   CFG_TYPE_STRING, "Listening Port",                "%s", NULL,         2,  8, (int)"3000" },
    { CFG_TYPE_END,            CFG_TYPE_END,    "",                              "",   NULL,         0,  0, 0 } };


Modem :: Modem()
{
    register_store(0x4D4F444D, "Modem Settings", modem_cfg);

    aciaQueue = xQueueCreate(16, sizeof(AciaMessage_t));
    aciaTxBuffer = new DataBuffer(2048); // 2K transmit buffer (From C64)

    commandQueue = xQueueCreate(8, sizeof(ModemCommand_t));
    connectQueue = xQueueCreate(2, sizeof(ModemCommand_t));

    connectionLock = xSemaphoreCreateMutex();
    xTaskCreate( Modem :: task, "Modem Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    xTaskCreate( Modem :: callerTask, "Outgoing Caller", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    listenerSocket = new ListenerSocket("Modem Listener", Modem :: listenerTask, "Modem External Connection");
    connected = false;
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
    int len = sprintf(buffer, "You are connected to the modem!\n");
    send(socketNumber, buffer, len, 0);

    modem.IncomingConnection(socketNumber);

    lwip_close(socketNumber);
    vTaskDelete(NULL);
}

const AciaMessage_t setDCD = { ACIA_MSG_DCD, 1, 0 };
const AciaMessage_t clrDCD = { ACIA_MSG_DCD, 0, 0 };
const AciaMessage_t setCTS = { ACIA_MSG_CTS, 1, 0 };
const AciaMessage_t clrCTS = { ACIA_MSG_CTS, 0, 0 };
const AciaMessage_t rxData = { ACIA_MSG_RXDATA, 0, 0 };

void Modem :: RunRelay(int socket)
{
    struct timeval tv;
    tv.tv_sec = 10; // bug in lwip; this is just used directly as tick value
    tv.tv_usec = 10;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

    printf("Modem: Running Relay to socket %d.\n", socket);
    int ret;
    while(1) {
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
        int avail = aciaTxBuffer->AvailableContiguous();
        if (avail > 0) {
            ret = send(socket, aciaTxBuffer->GetReadPointer(), avail, 0);
            if (ret > 0) {
                aciaTxBuffer->AdvanceReadPointer(ret);
            } else if(ret < 0) {
                printf("Error writing to socket. Exiting\n");
                break;
            }
        }
    }
}

void Modem :: IncomingConnection(int socket)
{
    char buffer[64];
    if (xSemaphoreTake(connectionLock, 0) != pdTRUE) {
        int len = sprintf(buffer, "The modem is already in an active connection. Try again later.\n");
        send(socket, buffer, len, 0);
        return;
    }
    xQueueSend(aciaQueue, &setCTS, portMAX_DELAY);

    ModemCommand_t modemCommand;

    for(int rings=0; rings<5; rings++) {
        acia.SendToRx((uint8_t *)"RING\r", 5);
        int len = sprintf(buffer, "RING\n");
        send(socket, buffer, len, 0);
        BaseType_t cmdAvailable = xQueueReceive(commandQueue, &modemCommand, 600); // wait max 3 seconds for a command
        if (cmdAvailable) {
            modemCommand.command[modemCommand.length] = 0;
            printf("MODEM COMMAND: '%s'\n", modemCommand.command);

            if ((modemCommand.length > 0) && (modemCommand.command[0] == 'A')) {
                acia.SendToRx((uint8_t *)"CONNECT 19200\r", 14);
                int len = sprintf(buffer, "CONNECT 19200\n");
                send(socket, buffer, len, 0);
                connected = true;
                break;
            }
        }
    }

    if (connected) {
        xQueueSend(aciaQueue, &setDCD, portMAX_DELAY);
        RunRelay(socket);
    } else {
        int len = sprintf(buffer, "NO ANSWER\n");
        send(socket, buffer, len, 0);
    }

    xQueueSend(aciaQueue, &clrDCD, portMAX_DELAY);
    xQueueSend(aciaQueue, &clrCTS, portMAX_DELAY);
    connected = false;
    xSemaphoreGive(connectionLock);
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
        int result = gethostbyname_r(&cmd.command[2], &my_host, buffer, 128, &ret_host, &error);

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
            acia.SendToRx((uint8_t *)"CAN'T CONNECT\r", 14);
            xSemaphoreGive(connectionLock);
            continue;
        }
        //recv(sock_fd, (void *)buffer, 128, 0);
        //dump_hex(buffer, 128);
        // run the relay

        acia.SendToRx((uint8_t *)"CONNECTED\r", 10);
        connected = true;
        RunRelay(sock_fd);
        connected = false;
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
            if (c == 0x0D) {
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

void Modem :: ExecuteCommand(ModemCommand_t *cmd)
{
    if(strncmp(cmd->command, "DT", 2) == 0) {
        printf("I need to make an outgoing call to '%s'!\n", &cmd->command[2]);
        xQueueSend(connectQueue, cmd, 10);
    } else {
        acia.SendToRx((uint8_t *)"?\r", 2);
    }
    xSemaphoreGive(connectionLock);
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

    while(1) {
        xQueueReceive(aciaQueue, &message, portMAX_DELAY);
        //printf("Message type: %d. Value: %b. DataLen: %d Buffer: %d\n", message.messageType, message.smallValue, message.dataLength, aciaTxBuffer->AvailableData());
        switch(message.messageType) {
        case ACIA_MSG_CONTROL:
            printf("BAUD=%d\n", baudRates[message.smallValue & 0x0F]);
            acia.SetRxRate(rateValues[message.smallValue & 0x0F]);
            //acia.SendToRx((uint8_t *)outbuf, strlen(outbuf));
            break;
        case ACIA_MSG_DCD:
            acia.SetDCD(message.smallValue);
            break;
        case ACIA_MSG_DSR:
            acia.SetDSR(message.smallValue);
            break;
        case ACIA_MSG_CTS:
            acia.SetCTS(message.smallValue);
            break;
        case ACIA_MSG_HANDSH:
            printf("HANDSH=%b\n", message.smallValue);
            //acia.SendToRx((uint8_t *)outbuf, strlen(outbuf));
            break;
        case ACIA_MSG_TXDATA:
            if (!connected) {
                len = aciaTxBuffer->Get(txbuf, 30);
                acia.SendToRx(txbuf, len); // local echo if there is no connection
                txbuf[len] = 0;
                CollectCommand(&modemCommand, (char *)txbuf, len);
                if (modemCommand.state == 3) {
                    if (xSemaphoreTake(connectionLock, 0) != pdTRUE) {
                        if (xQueueSend(commandQueue, &modemCommand, 10) == pdFALSE) {
                            acia.SendToRx((uint8_t *)"NAK\r", 4);
                        }
                    } else {
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

    listenerSocket->Start(newPort);
}

Modem modem;
