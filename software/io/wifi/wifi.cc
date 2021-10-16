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
#include "userinterface.h"

extern "C" {
int alt_irq_register(int, int, void (*)(void*));
}

typedef enum
{
    WIFI_OFF = 0, WIFI_QUIT, WIFI_BOOTMODE, WIFI_DOWNLOAD, WIFI_START, WIFI_DOWNLOAD_START, WIFI_DOWNLOAD_MSG, WIFI_ECHO
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
#define FLASH_TRANSFER_SIZE  0x400

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
    xTaskCreate( WiFi :: CommandTaskStart, "WiFi Command Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    doClose = false;
    programError = false;
    runModeTask = NULL;
}


void WiFi :: Disable()
{
    // stop running the run-mode task
    if (runModeTask) {
        vTaskDelete(runModeTask);
        runModeTask = NULL;
    }

    U64_WIFI_CONTROL = 0;
}

void WiFi :: Enable()
{
    U64_WIFI_CONTROL = 0;
    vTaskDelay(50);
    uart->ClearRxBuffer();
    uart->SetBaudRate(115200);
    U64_WIFI_CONTROL = 1; // 5 for watching the UART output directly

    xTaskCreate( WiFi :: RunModeTaskStart, "WiFi RunTask", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &runModeTask );
}

void WiFi :: Boot()
{
    // stop running the run-mode task
    if (runModeTask) {
        vTaskDelete(runModeTask);
        runModeTask = NULL;
    }

    U64_WIFI_CONTROL = 2;
    vTaskDelay(150);
    uart->ClearRxBuffer();
    uart->SetBaudRate(115200);
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
        buffer += 4;
    }
    va_end(ap);
}

bool WiFi::RequestEcho(void)
{
    const char *test_string = "This string should be echoed.";
    int length = strlen(test_string);

    uart->SendSlipOpen();
    uint8_t header[4];
    header[0] = 0x02;
    header[1] = 0;
    header[2] = length & 0xFF;
    header[3] = length >> 8;
    uart->SendSlipData(header, 4);
    uart->SendSlipData(test_string, length);
    uart->SendSlipClose();
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
    uart->SendSlipData(data, length);
    uart->SendSlipClose();

    do {
        int r = uart->GetSlipPacket(receiveBuffer, 512, timeout);

        if (r > 0) {
            if (receiveBuffer[0] == 1) {
                if (receiveBuffer[1] == opcode) {
                    uint8_t size = receiveBuffer[2];
                    uint8_t success = receiveBuffer[8];
                    uint8_t error = receiveBuffer[9];
                    if (((size == 2) || (size == 4)) && (success == 0)) {
                        return true;
                    } else {
                        printf("Command: %b. Error = %b.\n", opcode, error);
                        dump_hex_relative(receiveBuffer, r);
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

    vTaskDelay(100); // wait for the boot message to appear
    bzero(receiveBuffer, 512);
    if (uart->Read(receiveBuffer, 512) > 0) {
        printf("Boot message:\n%s\n", receiveBuffer);
    } else {
    	printf("No boot message.\n");
    	return -7; // no response at all from ESP32
    }
    bool downloadStringFound = false;
    for (int i=0;i<100;i++) {
    	if (strncmp((char *)(receiveBuffer + i), "DOWNLOAD_BOOT", 13) == 0) {
    		downloadStringFound = true;
    		break;
    	}
    }
    if (!downloadStringFound) {
    	printf("Download string not found.\n");
    	return -8;
    }

    // uart->SetBaudRate(115200*2);

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
                    0x200000, // 2 MB
                     0x10000, // 64 KB block size
                      0x1000, // 4 KB sector size
                       0x100, // 256 byte page size
                     0xFFFF); // Status mask
    if (!Command(ESP_SET_FLASH_PARAMS, 24, 0, parambuf, receiveBuffer, 200))
        return -3;

    uint32_t block_size = FLASH_TRANSFER_SIZE;
    uint32_t blocks = (length + block_size - 1) / block_size;
    uint32_t total_length = blocks * block_size;

    // Now start Flashing
    PackParams(parambuf, 4, total_length, blocks, block_size, address);
    if (!Command(ESP_FLASH_BEGIN, 16, 0, parambuf, receiveBuffer, 15 * 200)) {
    	printf("Command Error ESP_FLASH_BEGIN.\n");
    	return -4;
    }

    // Flash Blocks
    const uint8_t *pb = binary;
    uint32_t remain = length;

    printf("Number of blocks to program: %d\n", blocks);

    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t now = (remain > block_size) ? block_size : remain;
        memcpy(flashBlock + 16, pb, now);
        // Fill the remaining part of this block with FF
        if (now < block_size) {
            memset(flashBlock + 16 + now, 0xFF, block_size - now);
        }
        uint8_t chk = 0xEF;
        for (int m = 16; m < 16 + block_size; m++) {
            chk ^= flashBlock[m];
        }
        PackParams(flashBlock, 4, block_size, i, 0, 0);
        if (!Command(ESP_FLASH_DATA, 16 + block_size, chk, flashBlock, receiveBuffer, 5 * 200)) {
        	printf("Command Error ESP_FLASH_DATA.\n");
            return -5;
        }
        remain -= now;
        pb += now;
    }

    PackParams(parambuf, 1, 1); // Stay in the Loader
    if (!Command(ESP_FLASH_END, 4, 0, parambuf, receiveBuffer, 200)) {
    	printf("Command Error ESP_FLASH_END.\n");
        return -6;
    }

    printf("Programming ESP32 was a success!\n");
    return 0;
}

void WiFi::CommandTaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->CommandThread();
}

void WiFi::RunModeTaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->RunModeThread();
}

/*
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
*/

void WiFi::CommandThread()
{
    uart->EnableRxIRQ(true);

    wifiCommand_t command;
    bool run = true;
    int a;

    while (run) {
        xQueueReceive(commandQueue, &command, portMAX_DELAY);

        switch (command.commandCode) {
        case WIFI_QUIT:
            run = false;
            break;
        case WIFI_OFF:
            Disable();
            uart->EnableSlip(false);
            break;
        case WIFI_BOOTMODE:
            uart->EnableSlip(true);
            Boot();
            break;
        case WIFI_START:
            uart->EnableSlip(true);
            Enable();
            break;
        case WIFI_DOWNLOAD:
            uart->EnableSlip(true);
            Boot();
            a = Download((const uint8_t *)command.data, command.address, command.length);
            if (a) {
                programError = true;
            }
            if (command.doFree) {
                delete[] ((uint8_t *)command.data);
            }
            break;
        case WIFI_DOWNLOAD_START:
            programError = false;
            break;
        case WIFI_DOWNLOAD_MSG:
            if (!programError) {
                UserInterface :: postMessage("ESP32 Program Complete.");
            } else {
                UserInterface :: postMessage("ESP32 Program Failed.");
            }
            break;
        case WIFI_ECHO:
            RequestEcho();
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
}

/*
void WiFi::Listen()
{
    int sockfd, portno;
    unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;

     First call to socket() function
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        puts("ERROR wifiThread opening socket");
        return;
    }

    printf("WiFi Thread Sockfd = %8x\n", sockfd);

     Initialize socket structure
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 3333;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

     Now bind the host address using bind() call.
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        puts("wifiThread ERROR on binding");
        return;
    }

     Now start listening for the clients, here process will
     * go in sleep mode and will wait for the incoming connection


    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    uint8_t buffer[512];
    int read, ret;

    while (1) {
         Accept actual connection from the client
        actualSocket = accept(sockfd, (struct sockaddr * )&cli_addr, &clilen);
        if (actualSocket < 0) {
            puts("wifiThread ERROR on accept");
            return;
        }

        printf("wifiThread actualSocket = %8x\n", actualSocket);

        Boot();
        uart->EnableSlip(false);

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
*/

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

BaseType_t WiFi::doDownloadWrap(bool start)
{
    wifiCommand_t command;
    command.commandCode = (start) ? WIFI_DOWNLOAD_START : WIFI_DOWNLOAD_MSG;
    return xQueueSend(commandQueue, &command, 200);
}

BaseType_t WiFi::doRequestEcho()
{
    wifiCommand_t command;
    command.commandCode = WIFI_ECHO;
    return xQueueSend(commandQueue, &command, 200);
}

WiFi wifi;

void WiFi :: RunModeThread()
{
    static uint8_t buffer[2048];
    while(true) {
        int pktSize = uart->GetSlipPacket(buffer, 2048, portMAX_DELAY);
        printf("Slip Packet Received (%d):\n", pktSize);
        dump_hex_relative(buffer, pktSize);
    }
}
