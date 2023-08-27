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
#include "rpc_calls.h"
#include "network_esp32.h"
#include "filemanager.h"

#define DEBUG_INPUT 0

extern "C" {
int alt_irq_register(int, int, void (*)(void*));
}

typedef enum
{
    WIFI_OFF = 0, WIFI_QUIT, WIFI_BOOTMODE, WIFI_DOWNLOAD, WIFI_START, WIFI_DOWNLOAD_START, WIFI_DOWNLOAD_MSG, WIFI_ECHO, UART_ECHO,
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
    packets = new command_buf_context_t;
    cmd_buffer_init(packets); // C functions, so no constructor

    uart = new FastUART((void *)U64_WIFI_UART, rxSemaphore, packets);

    if (-EINVAL == alt_irq_register(1, (int)uart, FastUART :: FastUartInterrupt)) {
        puts("Failed to install WifiUART IRQ handler.");
    }

    commandQueue = xQueueCreate(8, sizeof(wifiCommand_t));
    xTaskCreate( WiFi :: CommandTaskStart, "WiFi Command Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    doClose = false;
    programError = false;
    runModeTask = NULL;
    state = eWifi_Off;

    bzero(my_mac, 6);
    my_ip = 0;
    my_gateway = 0;
    my_netmask = 0;
    netstack = new NetworkLWIP_WiFi(this, wifi_tx_packet, wifi_free);
    netstack->attach_config();
}

void WiFi :: RefreshRoot()
{
    // Quick hack to refresh the screen
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");    
}

void WiFi :: Disable()
{
    // stop running the run-mode task
    state = eWifi_Off;

    if (runModeTask) {
        vTaskDelete(runModeTask);
        runModeTask = NULL;
    }
    U64_WIFI_CONTROL = 0;
    RefreshRoot();
}

void WiFi :: Enable(bool startTask)
{
    Disable();
    vTaskDelay(50);
    uart->SetBaudRate(115200);
    uart->EnableLoopback(false);
    uart->FlowControl(false);
    uart->ClearRxBuffer();
    uart->EnableSlip(true);
    uart->EnableIRQ(true);

    U64_WIFI_CONTROL = 1; // 5 for watching the UART output directly, 1 = own uart

    uart->PrintRxMessage();
    if (startTask) {
        xTaskCreate( WiFi :: RunModeTaskStart, "WiFi RunTask", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &runModeTask );
    }
}

void WiFi :: Boot()
{
    Disable();

    U64_WIFI_CONTROL = 2;
    vTaskDelay(150);
    uart->SetBaudRate(115200);
    uart->EnableLoopback(false);
    uart->FlowControl(false);
    uart->ClearRxBuffer();
    uart->EnableSlip(true);
    uart->EnableIRQ(true);
    U64_WIFI_CONTROL = 3;
    //uart->PrintRxMessage(); Do not print message, because Download routine expects a message and verifies it
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

bool WiFi::UartEcho(void)
{
    uart->EnableLoopback(true);
    uart->FlowControl(true);
    uart->ClearRxBuffer();
    uart->EnableSlip(true);
    uart->EnableIRQ(true);
    int br = 115200;
    //uart->SetBaudRate(32*115200);
    command_buf_t *buf;

    for(int rate = 115200; rate < 8000000; rate *= 2) {
        uart->SetBaudRate(rate);
        for(int i=800; i<=1000;i+=50) {
            uint16_t start, stop;
            if (uart->GetBuffer(&buf, 100) != pdTRUE) {
                return false;
            }
            memset(buf->data, 0x00, CMD_BUF_SIZE);
            buf->data[0] = 1;
            buf->data[i-1] = 2;
            int txbuf = buf->bufnr;
            buf->size = i;
            uart->TransmitPacket(buf, &start);

            if (uart->ReceivePacket(&buf, 100) == pdTRUE) {
                stop = getMsTimer();
                int t = 0;
                for (int j=0;j<buf->size;j++) {
                    t += (int)buf->data[j];
                }
                bool ok = (t == 3) && (buf->data[0] == 1) && (buf->data[i-1] == 2) && (buf->size == i);
                printf("%d Bytes received from buffer %d in buffer %d after %d ms Verdict: %s\n", buf->size, txbuf, buf->bufnr, stop - start, ok ? "OK!" : "FAIL!");
                uart->FreeBuffer(buf);
                //dump_hex_relative(buf->data, buf->size);
            } else {
                printf("No packet.\n");
            }
        }
    }
    uart->EnableLoopback(false);
}

bool WiFi::RequestEcho(void)
{
    command_buf_t *buf;
    uart->txDebug = false;

    uart->SetBaudRate(115200);

    while (uart->ReceivePacket(&buf, 100) == pdTRUE) {
        printf("%d Bytes received in buffer %d\n", buf->size, buf->bufnr);
        uart->FreeBuffer(buf);
    }

    char string[40];
    uint16_t major, minor;
    if (wifi_detect(&major, &minor, string, 40) == pdTRUE) {
        printf("ESP32 module V%d.%d detected: %s\n", major, minor, string);
    }

    const int rates[] = { 115200, 230400, 460800, 921600, 1000000, 2000000, 4000000, 6666666 };

    for(int rate = 0; rate < 8; rate ++) {
        wifi_setbaud(rates[rate], 1); // uart->SetBaudRate(rate); + remote switch as well     uart->FlowControl(false);

        for(int i=800; i<=1000;i+=50) {
            uint16_t start, stop;
            if (uart->GetBuffer(&buf, 100) != pdTRUE) {
                return false;
            }
            memset(buf->data, 0x00, CMD_BUF_SIZE);
            rpc_identify_req *req = (rpc_identify_req *)buf->data;
            req->hdr.command = CMD_ECHO;
            buf->data[i-1] = 2;
            int txbuf = buf->bufnr;
            buf->size = i;
            uart->TransmitPacket(buf, &start);

            if (uart->ReceivePacket(&buf, 200) == pdTRUE) {
                stop = getMsTimer();
                int t = 0;
                for (int j=0;j<buf->size;j++) {
                    t += (int)buf->data[j];
                }
                bool ok = (t == 3) && (buf->data[0] == 1) && (buf->data[i-1] == 2) && (buf->size == i);
                printf("%d Bytes received from buffer %d in buffer %d after %d ms Verdict: %s\n", buf->size, txbuf, buf->bufnr, stop - start, ok ? "OK!" : "FAIL!");
                if (!ok) {
                    dump_hex_relative(buf->data, buf->size);
                }
                uart->FreeBuffer(buf);
                //dump_hex_relative(buf->data, buf->size);
            }
            //printf("End of packets. Aux data:\n");
            //uart->PrintRxMessage();
        }
    }
    uart->txDebug = false;
}

bool WiFi::Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data,
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

    uart->EnableSlip(true); // now communicate with packets
    // uart->SetBaudRate(115200*2);

    command_buf_t *buf;
    for (int i = 0; i < 10; i++) {
        uart->SendSlipPacket(syncFrame, 44);
        if (uart->ReceivePacket(&buf, 200) == pdTRUE) {
            dump_hex_relative(buf->data, buf->size);
        } else {
            printf("No Reply.\n");
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


void WiFi::CommandThread()
{
    uart->EnableIRQ(true);

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
            Enable(true);
            break;
        case WIFI_DOWNLOAD:
            uart->EnableSlip(false); // first receive boot message
            uart->EnableLoopback(false);
            uart->FlowControl(false);
            uart->txDebug = false; // disable debug
            Boot();
            state = eWifi_Download;
            RefreshRoot();
            a = Download((const uint8_t *)command.data, command.address, command.length);
            if (a) {
                programError = true;
            }
            if (command.doFree) {
                delete[] ((uint8_t *)command.data);
            }
            state = eWifi_Off;
            RefreshRoot();
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
            Enable(false);
            RequestEcho();
            break;
        case UART_ECHO:
            Disable();
            UartEcho();
            break;
        default:
            break;
        }
    }

    Disable();
    uart->EnableIRQ(false);
    vTaskDelete(NULL);
}

void WiFi::Quit()
{
    uart->EnableIRQ(false);
    Disable();
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

BaseType_t WiFi::doDisable()
{
    wifiCommand_t command;
    command.commandCode = WIFI_OFF;
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

BaseType_t WiFi::doUartEcho()
{
    wifiCommand_t command;
    command.commandCode = UART_ECHO;
    return xQueueSend(commandQueue, &command, 200);
}

WiFi wifi;
ultimate_ap_records_t wifi_aps;

void WiFi :: RunModeThread()
{
    uint16_t major, minor;
    BaseType_t suc = pdFALSE;
    command_buf_t *buf;
    rpc_header_t *hdr;
    event_pkt_got_ip *ev;
    int result;

    state = eWifi_NotDetected;
    RefreshRoot();

    while(1) {
        printf("WiFiState: %d\n", state);
        switch(state) {
        case eWifi_NotDetected:
            uart->txDebug = true;
            if (wifi_detect(&major, &minor, moduleName, 32)) {
                state = eWifi_Detected;
                RefreshRoot();
            }
            break;

        case eWifi_Detected:
            printf("Going to do setbaud..\n");
            uart->txDebug = true;
            result = wifi_setbaud(6666666, 1);
            printf("Result of setbaud: %d\n", result);

            wifi_getmac(my_mac);
            netstack->set_mac_address(my_mac);
            wifi_scan(&wifi_aps);

            state = eWifi_NotConnected;
            RefreshRoot();
            break;

        case eWifi_NotConnected:
        case eWifi_Failed:
            outbyte('[');
            cmd_buffer_received(packets, &buf, portMAX_DELAY);
            outbyte(']');
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
            switch(hdr->command) {
            case EVENT_RESCAN:
                cmd_buffer_free(packets, buf);
                state = eWifi_Detected;
                RefreshRoot();
                break;

            case EVENT_CONNECTED:
                cmd_buffer_free(packets, buf);
                printf("Connected Event processed in RunThread\n");
                if (state == eWifi_Connected) { // already connected!
                    netstack->link_down();
                }
                state = eWifi_Connected;
                uart->txDebug = false;

                if(netstack) {
                    netstack->start();
                    netstack->link_up();
                }
                RefreshRoot();
                break;

            case EVENT_DISCONNECTED:
                cmd_buffer_free(packets, buf);
                state = eWifi_Failed;
                if(netstack) {
                    netstack->stop();
                    netstack->link_down();
                }
                RefreshRoot();
                break;

            default:
                printf("Unexpected Event type in state NotConnected or Failed: Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
                cmd_buffer_free(packets, buf);
                break;
            }
            break;

        case eWifi_Connected:
            outbyte('{');
            cmd_buffer_received(packets, &buf, portMAX_DELAY);
            outbyte('}');
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
            switch(hdr->command) {
            case EVENT_CONNECTED:
                cmd_buffer_free(packets, buf);
                netstack->link_down();
                if(netstack) {
                    netstack->start();
                    netstack->link_up();
                }
                RefreshRoot();
                break;

            case EVENT_DISCONNECTED:
                cmd_buffer_free(packets, buf);
                state = eWifi_NotConnected;
                if(netstack) {
                    netstack->stop();
                    netstack->link_down();
                }
                RefreshRoot();
                break;

            case EVENT_RECV_PACKET:
                if (netstack) {
                    rpc_rx_pkt *pkt = (rpc_rx_pkt *)buf->data;
#if DEBUG_INPUT
                    printf("In %d\n", pkt->len);
                    dump_hex_relative(&pkt->data, 64);
#endif
                    netstack->input(buf, (uint8_t*)&pkt->data, pkt->len);
                } else {
                    puts("Packet received, but no net stack!");
                    cmd_buffer_free(packets, buf);
                }
                // no free, as the packet needs to live on in the network stack
                break;

            default:
                printf("Unexpected Event type in Connected: Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
                cmd_buffer_free(packets, buf);
                break;
            }
        }
    }

    // Wait forever
    vTaskDelay(portMAX_DELAY);
}

void WiFi :: sendEvent(uint8_t code)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    rpc_header_t *hdr = (rpc_header_t *)(buf->data);
    hdr->command = code;
    hdr->sequence = 0;
    hdr->thread = 0;
    cmd_buffer_loopback(packets, buf); // internal event
}

void WiFi :: freeBuffer(command_buf_t *buf)
{
    cmd_buffer_free(packets, buf);
}

void WiFi ::getAccessPointItems(Browsable *parent, IndexedList<Browsable *> &list)
{
    ultimate_ap_records_t *aps = &wifi_aps;
    ENTER_SAFE_SECTION;
    for (int i = 0; i < aps->num_records; i++) {
        list.append(new BrowsableWifiAP(parent, (char *)aps->aps[i].ssid, aps->aps[i].rssi, aps->aps[i].authmode));
    }
    LEAVE_SAFE_SECTION;
}

/// C like functions to 'talk' with the WiFi Module
static uint16_t sequence_nr = 0;
extern WiFi wifi;
TaskHandle_t tasksWaitingForReply[NUM_BUFFERS];

#define BUFARGS(x, cmd)     command_buf_t *buf; \
                            wifi.uart->GetBuffer(&buf, portMAX_DELAY); \
                            rpc_ ## x ## _req *args = (rpc_ ## x ## _req *)buf->data; \
                            args->hdr.command = cmd; \
                            args->hdr.sequence = sequence_nr++; \
                            args->hdr.thread = (uint8_t)buf->bufnr; \
                            buf->size = sizeof(rpc_ ## x ## _req); \
                            tasksWaitingForReply[buf->bufnr] = xTaskGetCurrentTaskHandle();


#define TRANSMIT(x)         wifi.uart->TransmitPacket(buf); \
                            xTaskNotifyWait(0, 0, (uint32_t *)&buf, portMAX_DELAY); \
                            printf("Received buffer %d, size %d:\n", buf->bufnr, buf->size); \
                            dump_hex_relative(buf->data, buf->size > 32 ? 32 : buf->size); \
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data; \
                            tasksWaitingForReply[result->hdr.thread] = NULL;

#define RETURN_STD          errno = result->xerrno; \
                            int retval = result->retval; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;

#define RETURN_ESP          int retval = result->esp_err; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;


void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
}

BaseType_t u64_buffer_received_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    BaseType_t res;

/*
    hex(hdr->thread);
    ioWrite8(UART_DATA, '-');
    uint32_t th = (uint32_t)tasksWaitingForReply[hdr->thread];
    hex((uint8_t)(th >> 16));
    hex((uint8_t)(th >> 8));
    hex((uint8_t)(th >> 0));
    ioWrite8(UART_DATA, ';');

*/
    if ((hdr->thread < NUM_BUFFERS) && (tasksWaitingForReply[hdr->thread])) {
        res = xTaskNotifyFromISR(tasksWaitingForReply[hdr->thread], (uint32_t)buf, eSetValueWithOverwrite, w);
    } else {
        res = cmd_buffer_received_isr(context, buf, w);
    }
    return res;
}


int wifi_setbaud(int baudrate, uint8_t flowctrl)
{
    BUFARGS(setbaud, CMD_SET_BAUD);

    args->baudrate = baudrate;
    args->flowctrl = flowctrl;
    args->inversions = 0;

    wifi.uart->TransmitPacket(buf);
    vTaskDelay(100); // wait until transmission must have completed (no handshake!) (~ half second, remote side will send reply at new rate after one second)
    wifi.uart->SetBaudRate(baudrate);
    wifi.uart->FlowControl(flowctrl != 0);

    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 1000); // 5 seconds
    if (success == pdTRUE) {
        // copy results
        rpc_espcmd_resp *result = (rpc_espcmd_resp *)buf->data;
        wifi.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
        return result->esp_err;
    }
    return -1;
}

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)
{
    BUFARGS(identify, CMD_IDENTIFY);

    wifi.uart->TransmitPacket(buf);
    // now block thread for reply
    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 100); // .5 seconds
    if (success == pdTRUE) {
        // copy results
        rpc_identify_resp *result = (rpc_identify_resp *)buf->data;
        *major = result->major;
        *minor = result->minor;
        strncpy(str, &result->string, maxlen);
        str[maxlen-1] = 0;
        wifi.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
    }
    return success;
}

int wifi_getmac(uint8_t *mac)
{
    BUFARGS(identify, CMD_WIFI_GETMAC);
    TRANSMIT(getmac);
    if (result->esp_err == 0) {
        memcpy(mac, result->mac, 6);
    } else {
        printf("Get MAC returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

int wifi_scan(void *a)
{
    ultimate_ap_records_t *aps = (ultimate_ap_records_t *)a;

    BUFARGS(identify, CMD_WIFI_SCAN);
    TRANSMIT(scan);
    ENTER_SAFE_SECTION;
    memcpy(aps, &(result->rec), sizeof(ultimate_ap_records_t));
    LEAVE_SAFE_SECTION;
    RETURN_ESP;
}

int wifi_wifi_connect(char *ssid, char *password, uint8_t auth)
{
    BUFARGS(wifi_connect, CMD_WIFI_CONNECT);

    bzero(args->ssid, 32);
    bzero(args->password, 64);
    strncpy(args->ssid, ssid, 32);
    strncpy(args->password, password, 64);
    args->auth_mode = auth;

    TRANSMIT(scan);
    RETURN_ESP;
}

int wifi_wifi_disconnect()
{
    BUFARGS(wifi_connect, CMD_WIFI_DISCONNECT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

uint8_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    BUFARGS(send_eth, CMD_SEND_PACKET);

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    wifi.uart->TransmitPacket(buf);
    //TRANSMIT(espcmd);
    //RETURN_ESP;
}

void wifi_free(void *driver, void *buffer)
{
    // driver points to the WiFi object
    // buffer points to the cmd_buffer_t object
    WiFi *w = (WiFi *)driver;
    w->freeBuffer((command_buf_t *)buffer);
}


#include "subsys.h"
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *>&items)
{
    items.append(new Action("Connect", BrowsableWifiAP :: connect_ap, (int)this, 0));
}

int BrowsableWifiAP :: connect_ap(SubsysCommand *cmd)
{
    char password[64] = { 0 };
    BrowsableWifiAP *bap = (BrowsableWifiAP *)cmd->functionID;
    if (bap->auth) {
        if(cmd->user_interface) {
            cmd->user_interface->string_box("Enter password", password, 64);
        }
    }
    int result = wifi_wifi_connect(bap->ssid, password, bap->auth);
    printf("Connect result: %d\n", result);
    if (result == 0) {
        wifi.sendEvent(EVENT_CONNECTED);
    }
    return 0;
}
