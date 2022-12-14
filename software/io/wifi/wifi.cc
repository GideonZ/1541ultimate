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

extern "C" {
int alt_irq_register(int, int, void (*)(void*));
}

WiFi :: WiFi()
{
    rxSemaphore = xSemaphoreCreateBinary();

    uart = new FastUART((void *)U64_WIFI_UART, rxSemaphore);

    if (-EINVAL == alt_irq_register(1, (int)uart, FastUART :: FastUartRxInterrupt)) {
        puts("Failed to install WifiUART IRQ handler.");
    }

    xTaskCreate( WiFi :: TaskStart, "WiFi Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    doClose = false;
}

void WiFi :: Disable()
{
    uart->EnableRxIRQ(false);
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


void WiFi :: TaskStart(void *context)
{
    WiFi *w = (WiFi *)context;
    w->Thread();
}

void WiFi :: EthernetRelay(void *context)
{
    WiFi *w = (WiFi *)context;
    uint8_t buffer[256];
    int ret;
    while(1) {
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

void WiFi :: Thread()
{
    uart->EnableRxIRQ(true);

    int sockfd, portno;
    unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        puts("ERROR wifiThread opening socket");
        return;
    }

    printf("WiFi Thread Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 3333;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
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

    while(1) {
        /* Accept actual connection from the client */
        actualSocket = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (actualSocket < 0)
        {
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
        setsockopt(actualSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

        while(!doClose) {
            // UART RX => Ethernet
            if(xSemaphoreTake(rxSemaphore, 1) == pdTRUE) {
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
                } while(read);
            }

            // Ethernet => UART TX
            ret = recv(actualSocket, buffer, 512, 0);
            if (ret > 0) {
                //printf("To ESP:\n");
                //dump_hex_relative(buffer, ret);
                printf(">%d|", ret);
                uart->Write(buffer, ret);
            } else if(ret == 0) {
                printf("Receive returned 0. Exiting\n");
                break;
            }
        }
        lwip_close(actualSocket);
        printf("WiFi Socket closed.\n");
    }
}


WiFi wifi;
