/*
 * esp32.cc
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 * 
 *  This class implements the basic interaction with the ESP32 module over a UART connection.
 *  It supports flashing, turning it on and off, etc.
 *  The communication with the ESP32 is based on the SLIP protocol. The ESP32 uses this protocol
 *  for programming the module, and it has been re-used to communicate with the application running
 *  on the ESP32 after it is programmed. The first and currently only application that runs on the
 *  ESP32 is a Esp32 network modem. This allows the network stack to run on the Ultimate, such that
 *  both LAN and Esp32 connections can coexist. See the 'wifi.cc' file to see how the Ultimate
 *  communicates with the ESP32 application.
 *  
 */

#include "esp32.h"
#include "u64.h"
#include "dump_hex.h"
#include <stdarg.h>
#include "userinterface.h"
#include "subsys.h"

#define DEBUG_INPUT 0

extern "C" {
int alt_irq_register(int, int, void (*)(void*));
}

typedef enum
{
    WIFI_OFF = 0, WIFI_QUIT, WIFI_BOOTMODE, WIFI_DOWNLOAD, WIFI_START, WIFI_DOWNLOAD_START, WIFI_DOWNLOAD_MSG, UART_ECHO,
} espCommand_e;

typedef struct
{
    espCommand_e commandCode;
    const void *data;
    uint32_t address;
    uint32_t length;
    bool doFree;
} espCommand_t;

#define ESP_FLASH_BEGIN		 0x02
#define ESP_FLASH_DATA		 0x03
#define ESP_FLASH_END		 0x04
#define ESP_SET_FLASH_PARAMS 0x0B
#define ESP_CHANGE_BAUDRATE  0x0F
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

Esp32 :: Esp32()
{
    rxSemaphore = xSemaphoreCreateBinary();
    packets = new command_buf_context_t;
    cmd_buffer_init(packets); // C functions, so no constructor
    running = false;

    uart = new DmaUART((void *)WIFI_UART_BASE, ITU_IRQHIGH_WIFI, rxSemaphore, packets);
}

void Esp32 :: AttachApplication(Esp32Application *app)
{
    registered_app = app;
}

void Esp32 :: Disable()
{
    running = false;
    StopApp();

    // stop running the run-mode task
    uart->ModuleCtrl(ESP_MODE_OFF);
    uart->ClearRxBuffer(); // disables interrupts as well
}

void Esp32 :: StartApp()
{
    StopApp();

    // Note that the application will start with the module in the either on or off state.
    // The application becomes responsible for turning the module on. When the application
    // wants to detect the module, it should start it in boot mode. This enables the application to
    // detect the ESP32 module and display UI messages accordingly. It doesn't need to do so,
    // it can also assume that the application on the ESP is running and simply connect to it.

    // Start Application
    // TODO: Make it configurable what application starts.. Now, it's just a single one
    if (registered_app) {
        application = registered_app;
        application->Init(uart, packets);
        application->Start();
    }
}

void Esp32 :: StopApp()
{
    if (application) {
        application->Terminate();
        application = NULL;
    }
    uart->ResetReceiveCallback();
}

int Esp32 :: DetectModule(void *buffer, int buffer_len)
{
    running = false;
    uart->ModuleCtrl(ESP_MODE_OFF);
    uart->ClearRxBuffer(); // disables interrupts as well
    vTaskDelay(50);
    uart->SetBaudRate(115200);
    uart->EnableLoopback(false);
    uart->FlowControl(false);
    uart->ClearRxBuffer();
    uart->EnableSlip(false);
    uart->EnableIRQ(true);
    uart->ModuleCtrl(ESP_MODE_BOOT);
    vTaskDelay(100); // 0.5 seconds
    uart->EnableSlip(true);
    return uart->Read((uint8_t*)buffer, buffer_len);
}

void Esp32 :: EnableRunMode()
{
    uart->ModuleCtrl(ESP_MODE_OFF);
    uart->ClearRxBuffer(); // disables interrupts as well
    vTaskDelay(50);
    uart->SetBaudRate(115200);
    uart->EnableLoopback(false);
    uart->FlowControl(false);
    uart->ClearRxBuffer();
    uart->EnableSlip(false);
    uart->EnableIRQ(true);
    uart->ModuleCtrl(ESP_MODE_RUN);
    vTaskDelay(300); // 1.5 seconds
    ReadRxMessage();
    uart->EnableSlip(true);
    running = true;
}

void Esp32 :: Boot()
{
    Disable(); // clears running
    vTaskDelay(150);
    uart->SetBaudRate(115200);
    uart->EnableLoopback(false);
    uart->FlowControl(false);
    uart->ClearRxBuffer();
    uart->EnableSlip(false);
    uart->EnableIRQ(true);
    uart->ModuleCtrl(ESP_MODE_BOOT);
    //ReadRxMessage(); Do not print message, because Download routine expects a message and verifies it
}

void Esp32::PackParams(uint8_t *buffer, int numparams, ...)
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

bool Esp32::Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data,
        uint8_t *receiveBuffer, int timeout)
{
    command_buf_t *buf;
    if (uart->GetBuffer(&buf, 100) != pdTRUE) {
        return false;
    }
    memset(buf->data, 0, 8);
    buf->data[1] = opcode;
    buf->data[2] = length & 0xFF;
    buf->data[3] = length >> 8;
    buf->data[4] = chk;
    memcpy(buf->data + 8, data, length);
    buf->size = 8 + length;
    uart->TransmitPacket(buf); // will be freed by transmit

    do {
        if (uart->ReceivePacket(&buf, timeout) == pdTRUE) {
            memcpy(receiveBuffer, buf->data, buf->size); // ## TODO: what if size > sizeof receiveBuffer?
            int bsize = buf->size;
            uart->FreeBuffer(buf);
            if (receiveBuffer[0] == 1) {
                if (receiveBuffer[1] == opcode) {
                    uint8_t size = receiveBuffer[2];
                    uint8_t success = receiveBuffer[8];
                    uint8_t error = receiveBuffer[9];
                    if (((size == 2) || (size == 4)) && (success == 0)) {
                        return true;
                    } else {
                        printf("Command: %b. Error = %b.\n", opcode, error);
                        dump_hex_relative(receiveBuffer, bsize);
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

static uint8_t receiveBuffer[512];
int Esp32 :: Download(void)
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

    bool downloadStringFound = false;
    bool synced = false;

    for(int retries = 0; retries < 3; retries++) {
        uart->EnableSlip(false); // first receive boot message
        uart->EnableLoopback(false);
        uart->FlowControl(false);
        uart->txDebug = false; // disable debug
        Boot(); // also does disable, so this should stop the application

        vTaskDelay(100); // wait for the boot message to appear
        bzero(receiveBuffer, 512);
        // Force current data to be output as a packet
        uart->EnableSlip(true);
        uart->EnableSlip(false); 
        if (uart->Read(receiveBuffer, 512) > 0) {
            printf("Boot message:\n%s\n", receiveBuffer);
        } else {
            printf("No boot message.\n");
        }
        for (int i=0;i<100;i++) {
            if (strncmp((char *)(receiveBuffer + i), "DOWNLOAD", 8) == 0) {
                downloadStringFound = true;
                break;
            }
        }
        if (!downloadStringFound) {
            printf("Download string not found.\n");
        } else {
            break;
        }
    }
    if (!downloadStringFound) {
        return -7;
    }

    uart->EnableSlip(true); // now communicate with packets

    command_buf_t *buf;
    for (int i = 0; i < 10; i++) {
        uart->SendSlipPacket(syncFrame, 44);
        if (uart->ReceivePacket(&buf, 200) == pdTRUE) {
            dump_hex_relative(buf->data, buf->size);
        } else {
            printf("No Reply. "); uart->PrintStatus();
            continue;
        }
        memset(buf->data + 4, 0xEE, 4);
        if ((buf->size == 12) && (memcmp(buf->data, syncResp, 12) == 0)) {
            synced = true;
        }
        uart->FreeBuffer(buf);
        if (synced) {
            break;
        }
    }
    if (!synced) {
        printf("ESP did not sync\n");
        return -1;
    }
    BaseType_t received;
    do {
        received = uart->ReceivePacket(&buf, 20);
        if (received == pdTRUE) {
            printf("%d.", buf->size);
        }
    } while (received == pdTRUE);

    printf("Setting up Flashing ESP32.\n");
    uint8_t parambuf[32]; // up to 8 ints

    // Attach SPI Flash
    PackParams(parambuf, 2, 0, 0); // Default Flash
    if (!Command(ESP_ATTACH_SPI, 8, 0, parambuf, receiveBuffer, 500)) {
        printf("Command Error ESP_ATTACH_SPI\n");
        return -2;
    }

    // If a command succeeded, jerk up the baudrate
#if CLOCK_FREQ == 66666667
    const uint32_t br = 666666;
#else
    const uint32_t br = 2000000;
#endif

    PackParams(parambuf, 2, br, 0); 
    if (!Command(ESP_CHANGE_BAUDRATE, 8, 0, parambuf, receiveBuffer, 200)) {
        printf("Command Error ESP_CHANGE_BAUDRATE\n");
        return -9;
    }
    uart->SetBaudRate(br);

    // Set SPI Flash Parameters
    PackParams(parambuf, 6, 0, // FlashID
                    0x200000, // 2 MB
                     0x10000, // 64 KB block size
                      0x1000, // 4 KB sector size
                       0x100, // 256 byte page size
                     0xFFFF); // Status mask
    if (!Command(ESP_SET_FLASH_PARAMS, 24, 0, parambuf, receiveBuffer, 200)) {
        printf("Command Error ESP_SET_FLASH_PARAMS\n");
        return -3;
    }
    return 0;
}

int Esp32 :: Flash(const uint8_t *binary, uint32_t address, uint32_t length, EspDownloadCallback_t callback, void *context)
{
    uint32_t block_size = FLASH_TRANSFER_SIZE;
    uint32_t blocks = (length + block_size - 1) / block_size;
    uint32_t total_length = blocks * block_size;
    uint8_t parambuf[32]; // up to 8 ints
    static uint8_t flashBlock[16 + FLASH_TRANSFER_SIZE];

    printf("Number of blocks to program: %d (from %p) to %u\n", blocks, binary, address);

    // Now start Flashing
#if U64 == 2 || U2P == 2 // ESP32-C3 or S3, no need to try with extra zero
    PackParams(parambuf, 5, total_length, blocks, block_size, address, 0);
    if (!Command(ESP_FLASH_BEGIN, 20, 0, parambuf, receiveBuffer, 15 * 200)) {
        printf("Command Error ESP_FLASH_BEGIN (with zero)\n");
        return -4;
    }
#else
    PackParams(parambuf, 4, total_length, blocks, block_size, address);
    if (!Command(ESP_FLASH_BEGIN, 16, 0, parambuf, receiveBuffer, 15 * 200)) {
    	printf("Command Error ESP_FLASH_BEGIN. Let's try adding an extra 0 for ESP32-C3\n");
        PackParams(parambuf, 5, total_length, blocks, block_size, address, 0);
        if (!Command(ESP_FLASH_BEGIN, 20, 0, parambuf, receiveBuffer, 15 * 200)) {
        	printf("Command Error ESP_FLASH_BEGIN with extra zero\n");
    	    return -4;
        }
    }
#endif
    // Flash Blocks
    const uint8_t *pb = binary;
    uint32_t remain = length;

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
        if(callback) {
            callback(context);
        }
        remain -= now;
        pb += now;
    }

/*  PackParams(parambuf, 1, 1); // Stay in the Loader
    if (!Command(ESP_FLASH_END, 4, 0, parambuf, receiveBuffer, 200)) {
    	printf("Command Error ESP_FLASH_END.\n");
        return -6;
    }
*/
    printf("Programming ESP32 was a success!\n");
    return 0;
}

#define PRINT_BOOT_MSG 0
void Esp32 :: ReadRxMessage(void)
{
    static uint8_t buffertje[1540];

    int rx, to = 5;
    do {
        rx = uart->Read(buffertje, 1536);
        buffertje[rx] = 0;
        if (rx) {
            to = 3;
#if PRINT_BOOT_MSG
            printf((char *)buffertje);
#endif
        } else {
            vTaskDelay(25);
            to --;
            if (to == 0) {
                uart->EnableSlip(true);
                vTaskDelay(1);
                rx = uart->Read(buffertje, 1536);
                buffertje[rx] = 0;
#if PRINT_BOOT_MSG
                if (rx) {
                    printf("Last (%d): \n", rx);
                    printf((char *)buffertje);
                }
#endif
                break;
            }
        }
    } while(true);

#if PRINT_BOOT_MSG
    printf("\e[0m -> End of boot message <- \n");
#endif
}

void Esp32::Quit()
{
    uart->EnableIRQ(false);
    Disable();
}

Esp32 esp32;
