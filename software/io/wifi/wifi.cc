/*
 * wifi.cc
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#include "wifi.h"
#include "socket.h"
#include "u64.h"
#include "dump_hex.h"
#include <stdarg.h>

extern "C" {
int alt_irq_register(int, int, void (*)(void*));
}

typedef enum
{
    WIFI_OFF = 0, WIFI_QUIT, WIFI_BOOTMODE, WIFI_DOWNLOAD, WIFI_START,
} wifiCommand_e;

typedef struct
{
    wifiCommand_e commandCode;
    const void *data;
    uint32_t address;
    uint32_t length;
    bool doFree;
} wifiCommand_t;

#define ESP_FLASH_BEGIN		 0x02
#define ESP_FLASH_DATA		 0x03
#define ESP_FLASH_END		 0x04
#define ESP_SET_FLASH_PARAMS 0x0B
#define ESP_ATTACH_SPI       0x0D
#define FLASH_TRANSFER_SIZE  0x1000

// Single APP partition table
const uint8_t partition_table[] = {
    0xaa, 0x50, 0x01, 0x02, 0x00, 0x90, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x6e, 0x76, 0x73, 0x00, //  |.P.......`..nvs.|
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  |................|
    0xaa, 0x50, 0x01, 0x01, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x70, 0x68, 0x79, 0x5f, //  |.P..........phy_|
    0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  |init............|
    0xaa, 0x50, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x66, 0x61, 0x63, 0x74, //  |.P..........fact|
    0x6f, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //  |ory.............|
    0xeb, 0xeb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //  |................|
    0xf4, 0xad, 0x4f, 0x45, 0x38, 0x56, 0x4b, 0x5d, 0x74, 0x35, 0xb6, 0x2c, 0x75, 0xb6, 0x95, 0x24, //  |..OE8VK]t5.,u..$|
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //  |................|
};

WiFi :: WiFi()
{
    rxSemaphore = xSemaphoreCreateBinary();

    uart = new FastUART((void *)U64_WIFI_UART, rxSemaphore);

    if (-EINVAL == alt_irq_register(1, (int)uart, FastUART :: FastUartRxInterrupt)) {
        puts("Failed to install WifiUART IRQ handler.");
    }

    commandQueue = xQueueCreate(8, sizeof(wifiCommand_t));
    xTaskCreate( WiFi :: TaskStart, "WiFi Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    doClose = false;
}


void WiFi :: Disable()
{
    U64_WIFI_CONTROL = 0;
}

void WiFi :: Enable()
{
    U64_WIFI_CONTROL = 0;
    vTaskDelay(50);
    U64_WIFI_CONTROL = 1;
}

void WiFi :: Boot()
{
    U64_WIFI_CONTROL = 2;
    vTaskDelay(150);
    U64_WIFI_CONTROL = 3;
}

void WiFi::PackParams(uint8_t *buffer, int numparams, ...)
{
    va_list ap;
    va_start(ap, numparams);
    for (int i = 0; i < numparams; i++) {
        uint32_t param = va_arg(ap, uint32_t);
        buffer[0] = param & 0xFF;
        param >>= 8;
        buffer[1] = param & 0xFF;
        param >>= 8;
        buffer[2] = param & 0xFF;
        param >>= 8;
        buffer[3] = param & 0xFF;
        param >>= 8;
        buffer += 4;
    }
    va_end(ap);
}

bool WiFi::Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data,
        uint8_t *receiveBuffer, int timeout)
{
    uart->SendSlipOpen();
    uint8_t header[8];
    memset(header, 0, 8);
    header[1] = opcode;
    header[2] = length & 0xFF;
    header[3] = length >> 8;
    header[4] = chk;
    uart->SendSlipData(header, 8);
    //printf("-> "); dump_hex_relative(header, 8);
    uart->SendSlipData(data, length);
    //printf("=> "); dump_hex_relative(data, length);
    uart->SendSlipClose();

    do {
        int r = uart->GetSlipPacket(receiveBuffer, 512, timeout);

        if (r > 0) {
            dump_hex_relative(receiveBuffer, r);
            if (receiveBuffer[0] == 1) {
                if (receiveBuffer[1] == opcode) {
                    uint8_t size = receiveBuffer[2];
                    uint8_t success = receiveBuffer[8];
                    uint8_t error = receiveBuffer[9];
                    if (((size == 2) || (size == 4)) && (success == 0)) {
                        return true;
                    } else {
                        printf("Command: %b. Error = %b.\n", opcode, error);
                        break;
                    }
                }
            }
        } else {
            printf("No response to command with opcode %b\n", opcode);
            break;
        }
    } while (1);
    return false;
}

int WiFi :: Download(const uint8_t *binary, uint32_t address, uint32_t length)
{
    const uint8_t syncFrame[] = { 0x00, 0x08, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x07, 0x07, 0x12, 0x20,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    };
    const uint8_t syncResp[] = { 0x01, 0x08, 0x04, 0x00, 0xEE, 0xEE, 0xEE, 0xEE,
                                 0x00, 0x00, 0x00, 0x00 };

    static uint8_t receiveBuffer[512];
    static uint8_t flashBlock[16 + FLASH_TRANSFER_SIZE];

    bool synced = false;

    if (uart->Read(receiveBuffer, 512) > 0) {
        printf("Boot message:\n%s\n", receiveBuffer);
    }

    for (int i = 0; i < 10; i++) {
        uart->SendSlipPacket(syncFrame, 44);
        int r = uart->GetSlipPacket(receiveBuffer, 512, 200);
        dump_hex_relative(receiveBuffer, r);
        memset(receiveBuffer + 4, 0xEE, 4);
        if ((r == 12) && (memcmp(receiveBuffer, syncResp, 12) == 0)) {
            synced = true;
            break;
        }
    }
    if (!synced) {
        printf("ESP did not sync\n");
        return -1;
    }
    int r;
    do {
        r = uart->GetSlipPacket(receiveBuffer, 512, 20);
        printf("%d.", r);
    } while (r > 0);

    printf("Setting up Flashing ESP32.\n");
    uint8_t parambuf[32]; // up to 8 ints

    // Attach SPI Flash
    PackParams(parambuf, 2, 0, 0); // Default Flash
    if (!Command(ESP_ATTACH_SPI, 8, 0, parambuf, receiveBuffer, 500))
        return -2;

    // Set SPI Flash Parameters
    PackParams(parambuf, 6, 0, // FlashID
                    0x400000, // 4 MB
                     0x10000, // 64 KB block size
                      0x1000, // 4 KB sector size
                       0x100, // 256 byte page size
                     0xFFFF); // Status mask
    if (!Command(ESP_SET_FLASH_PARAMS, 24, 0, parambuf, receiveBuffer, 200))
        return -3;

    uint32_t block_size = FLASH_TRANSFER_SIZE;
    uint32_t blocks = (length + block_size - 1) / block_size;

    // Now start Flashing
    PackParams(parambuf, 4, length, blocks, block_size, address);
    if (!Command(ESP_FLASH_BEGIN, 16, 0, parambuf, receiveBuffer, 15 * 200))
        return -4;

    // Flash Blocks
    const uint8_t *pb = binary;
    uint32_t remain = length;

    printf("Number of blocks to program: %d\n", blocks);

    for (uint32_t i = 0; i < blocks; i++, pb += block_size) {
        uint32_t now = (remain > block_size) ? block_size : remain;
        uint8_t chk = 0xEF;
        for (int m = 0; m < now; m++) {
            chk ^= pb[m];
        }
        memcpy(flashBlock + 16, pb, now);
        PackParams(flashBlock, 4, now, i, 0, 0);
        if (!Command(ESP_FLASH_DATA, 16 + now, chk, flashBlock, receiveBuffer, 5 * 200))
            return -5;
        remain -= now;
        pb += now;
    }

    PackParams(parambuf, 1, 1); // Stay in the Loader
    if (!Command(ESP_FLASH_END, 4, 0, parambuf, receiveBuffer, 200))
        return -6;

    printf("Programming ESP32 was a success!\n");
    return 0;
}

void WiFi::TaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->Thread();
}

void WiFi::EthernetRelay(void *context)
{
    WiFi *w = (WiFi *) context;
    uint8_t buffer[256];
    int ret;
    while (1) {
        ret = recv(w->actualSocket, buffer, 256, 0);
        if (ret > 0) {
            printf(">%d|", ret);
            w->uart->Write(buffer, ret);
        } else {
            break;
        }
    }
    w->doClose = true;
    xSemaphoreGive(w->rxSemaphore);
    vTaskDelete(w->relayTask);
}

void WiFi::Thread()
{
    uart->EnableRxIRQ(true);

    wifiCommand_t command;
    bool run = true;

    while (run) {
        xQueueReceive(commandQueue, &command, portMAX_DELAY);

        switch (command.commandCode) {
        case WIFI_QUIT:
            run = false;
            break;
        case WIFI_OFF:
            Disable();
            break;
        case WIFI_BOOTMODE:
            Boot();
            break;
        case WIFI_START:
            Enable();
            break;
        case WIFI_DOWNLOAD:
            Boot();
            Download((const uint8_t *)command.data, command.address, command.length);
            if (command.doFree) {
                free((void *)command.data);
            }
            break;
        default:
            break;
        }
    }

    Disable();
    uart->EnableRxIRQ(false);
    vTaskDelete(NULL);
}

void WiFi::Quit()
{
    uart->EnableRxIRQ(false);
    Disable();
    // Should also kill the thread?
}

void WiFi::Listen()
{
    int sockfd, portno;
    unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        puts("ERROR wifiThread opening socket");
        return;
    }

    printf("WiFi Thread Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 3333;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        puts("wifiThread ERROR on binding");
        return;
    }

    /* Now start listening for the clients, here process will
     * go in sleep mode and will wait for the incoming connection
     */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    uint8_t buffer[512];
    int read, ret;

    while (1) {
        /* Accept actual connection from the client */
        actualSocket = accept(sockfd, (struct sockaddr * )&cli_addr, &clilen);
        if (actualSocket < 0) {
            puts("wifiThread ERROR on accept");
            return;
        }

        printf("wifiThread actualSocket = %8x\n", actualSocket);

        Boot();

        vTaskDelay(100);
        read = uart->Read(buffer, 512);
        printf("%s\n", buffer);

        doClose = false;
        // xTaskCreate( WiFi :: EthernetRelay, "WiFi Socket Rx", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );

        struct timeval tv;
        tv.tv_sec = 1; // bug in lwip; this is just used directly as tick value
        tv.tv_usec = 1;
        setsockopt(actualSocket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(struct timeval));

        while (!doClose) {
            // UART RX => Ethernet
            if (xSemaphoreTake(rxSemaphore, 1) == pdTRUE) {
                do {
                    read = uart->Read(buffer, 512);
                    if (read) {
                        printf("<%d|", read);
                        //printf("From ESP:\n");
                        //dump_hex_relative(buffer, read);
                        int ret = send(actualSocket, buffer, read, 0);
                        if (ret < 0) {
                            break;
                        }
                    }
                } while (read);
            }

            // Ethernet => UART TX
            ret = recv(actualSocket, buffer, 512, 0);
            if (ret > 0) {
                //printf("To ESP:\n");
                //dump_hex_relative(buffer, ret);
                printf(">%d|", ret);
                uart->Write(buffer, ret);
            } else if (ret == 0) {
                printf("Receive returned 0. Exiting\n");
                break;
            }
        }
        lwip_close(actualSocket);
        printf("WiFi Socket closed.\n");
    }
}

BaseType_t WiFi::doDownload(uint8_t *data, uint32_t address, uint32_t length, bool doFree)
{
    wifiCommand_t command;
    command.commandCode = WIFI_DOWNLOAD;
    if (!data) {
        command.data = partition_table;
        command.address = 0x8000;
        command.length = 144;
        command.doFree = false;
    } else {
        command.data = data;
        command.address = address;
        command.length = length;
        command.doFree = doFree;
    }
    return xQueueSend(commandQueue, &command, 200);
}

BaseType_t WiFi::doBootMode()
{
    wifiCommand_t command;
    command.commandCode = WIFI_BOOTMODE;
    return xQueueSend(commandQueue, &command, 200);
}

BaseType_t WiFi::doStart()
{
    wifiCommand_t command;
    command.commandCode = WIFI_START;
    return xQueueSend(commandQueue, &command, 200);
}

WiFi wifi;

