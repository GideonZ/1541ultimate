/*
 * socket_dma.cc
 *
 *  Created on: June 3, 2017
 *      Author: Gideon
 */

#include <string.h>

#include "itu.h"
#include "filemanager.h"
#include "socket_dma.h"
#include "socket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dump_hex.h"
#include "c64.h"
#include "c64_subsys.h"
#include "u64.h"
#include "c1541.h"
#include "data_streamer.h"
#include "filetype_crt.h"
#include "network_config.h"
#include "product.h"

// "Ok ok, use them then..."
#define SOCKET_CMD_DMA         0xFF01
#define SOCKET_CMD_DMARUN      0xFF02
#define SOCKET_CMD_KEYB        0xFF03
#define SOCKET_CMD_RESET       0xFF04
#define SOCKET_CMD_WAIT	       0xFF05
#define SOCKET_CMD_DMAWRITE    0xFF06
#define SOCKET_CMD_REUWRITE    0xFF07
#define SOCKET_CMD_KERNALWRITE 0xFF08
#define SOCKET_CMD_DMAJUMP     0xFF09
#define SOCKET_CMD_MOUNT_IMG   0xFF0A
#define SOCKET_CMD_RUN_IMG     0xFF0B
#define SOCKET_CMD_POWEROFF    0xFF0C
#define SOCKET_CMD_RUN_CRT     0xFF0D
#define SOCKET_CMD_IDENTIFY    0xFF0E

#define SOCKET_CMD_AUTHENTICATE 0xFF1F

// Only available on U64
#define SOCKET_CMD_VICSTREAM_ON    0xFF20
#define SOCKET_CMD_AUDIOSTREAM_ON  0xFF21
#define SOCKET_CMD_DEBUGSTREAM_ON  0xFF22
#define SOCKET_CMD_VICSTREAM_OFF   0xFF30
#define SOCKET_CMD_AUDIOSTREAM_OFF 0xFF31
#define SOCKET_CMD_DEBUGSTREAM_OFF 0xFF32

// Undocumented, shall only be used by developers.
#define SOCKET_CMD_LOADSIDCRT   0xFF71
#define SOCKET_CMD_LOADBOOTCRT  0xFF72
#define SOCKET_CMD_READFLASH    0xFF75
#define SOCKET_CMD_DEBUG_REG    0xFF76

SocketDMA *socket_dma = NULL;

extern cart_def sid_cart;
extern cart_def boot_cart;

SocketDMA::SocketDMA() {
	load_buffer = new uint8_t[SOCKET_BUFFER_SIZE];
	if (load_buffer) {
	    xTaskCreate( dmaThread, "DMA Load Task", configMINIMAL_STACK_SIZE, (void *)load_buffer, PRIO_NETSERVICE, NULL );
	}
    xTaskCreate(identThread, "UDP Ident Task", configMINIMAL_STACK_SIZE, NULL, PRIO_NETSERVICE, NULL );
}

SocketDMA::~SocketDMA() {
    delete[] load_buffer;
}

bool SocketDMA :: performCommand(int socket, void *load_buffer, int length, uint16_t cmd, uint32_t len, struct in_addr *client_ip, bool &authenticated)
{
	uint8_t *buf = (uint8_t *)load_buffer;
	SubsysCommand *sys_command;

    uint16_t offs;
    uint32_t offs32;
    uint16_t i;
    uint16_t size;
    const char *name = "";
    char title[52];
    bool ok = true;
    // TODO: check len > remaining

    const char *password = NULL;
    if (!authenticated && cmd != SOCKET_CMD_AUTHENTICATE) {
        return false;  // If not authenticated we disconnect
    }
    switch(cmd) {
    case SOCKET_CMD_AUTHENTICATE:
        password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
        if (!(*password) || (strlen(password) == length && memcmp(password, load_buffer, length) == 0)) {
            authenticated = true;
            buf[0] = 1;
        }
        else {
            ok = false;
            buf[0] = 0;
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Throttle failed password attempts
        }
        writeSocket(socket, buf, 1);
        break;
    case SOCKET_CMD_IDENTIFY:
        getProductTitleString(title+1, sizeof(title)-1, true);
        title[0] = (char)strlen(title+1);
        writeSocket(socket, title, 1+title[0]);
        break;
    case SOCKET_CMD_DMA:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD, buf, len);
        sys_command->execute();
        break;
    case SOCKET_CMD_DMARUN:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD_RUN, buf, len);
        sys_command->execute();
        break;
    case SOCKET_CMD_DMAJUMP:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_BUFFER, RUNCODE_DMALOAD_JUMP, buf, len);
        sys_command->execute();
        break;
    case SOCKET_CMD_DMAWRITE:
        offs = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, offs, buf + 2, len - 2);
        sys_command->execute();
        break;
    case SOCKET_CMD_KEYB:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, 0x0277, buf, len);
        sys_command->execute();
        buf[0] = len;
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_DMA_RAW_WRITE, 0x00C6, buf, 1);
        sys_command->execute();
        break;
    case SOCKET_CMD_RESET:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0, buf, len);
        sys_command->execute();
        break;
    case SOCKET_CMD_POWEROFF:
        sys_command = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_POWEROFF, 0, buf, len);
        sys_command->execute();
        break;
    case SOCKET_CMD_WAIT:
        vTaskDelay(len);
        len = 0;
        break;
    case SOCKET_CMD_REUWRITE:
        offs32 = (uint32_t)buf[0] | (((uint32_t)buf[1]) << 8) | (((uint32_t)buf[2]) << 16);
        for (i=3; i<len; i++)
           *(uint8_t *)(REU_MEMORY_BASE+ ((offs32+i-3)&0xffffff)) = buf[i];
        break;
    case SOCKET_CMD_KERNALWRITE:
        C64 :: getMachine()->enable_kernal(buf + 2);
        break;
    case SOCKET_CMD_LOADSIDCRT:
        size = (len > 0x2000) ? 0x2000 : len;
        if (sid_cart.custom_addr) {
            memcpy(sid_cart.custom_addr, buf, size);
            //sid_cart.length = size;
        }
        break;
    case SOCKET_CMD_LOADBOOTCRT:
        size = (len > 0x2000) ? 0x2000 : len;
        if (boot_cart.custom_addr) {
            memcpy(boot_cart.custom_addr, buf, size);
            boot_cart.length = size;
        }
        break;
    case SOCKET_CMD_MOUNT_IMG:
    case SOCKET_CMD_RUN_IMG:
    {
        FileManager *fm = FileManager :: getFileManager();
        FRESULT fres = fm->save_file(true, "/temp", "tcpimage.d64", buf, len, NULL);
        if (fres == FR_OK) {
            sys_command = new SubsysCommand(NULL, SUBSYSID_DRIVE_A, MENU_1541_MOUNT_D64, 1541, "/temp", "tcpimage.d64");
            sys_command->execute();
        }
        if (cmd == SOCKET_CMD_RUN_IMG) {
            char *drvId = "H";
            drvId[0] = 0x40 + c1541_A->get_current_iec_address();
            SubsysCommand *c64cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_MOUNT_LOAD_RUN, drvId, "*");
            c64cmd->execute();
        }
    }
    break;
    case SOCKET_CMD_RUN_CRT:
    {
        FileManager *fm = FileManager :: getFileManager();
        FRESULT fres = fm->save_file(true, "/temp", "tcpimage.crt", buf, len, NULL);
        if (fres == FR_OK) {
            sys_command = new SubsysCommand(NULL, SUBSYSID_C64, 0, 0, "/temp", "tcpimage.crt");
            FileTypeCRT::execute_st(sys_command);
            delete sys_command;
        }
    }
    break;

#ifdef U64
    case SOCKET_CMD_VICSTREAM_ON:
        // First DEBUG stream off
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 2, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();

        buf[len] = 0;
        if (len > 2) {
            name = (const char *)&buf[2];
        }
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 0, name, "");
        sys_command->direct_call = DataStreamer :: S_startStream;

        if ((len >= 2) && (buf[0] || buf[1])) {
            sys_command->bufferSize = (((uint32_t)buf[1]) << 8) | buf[0];
        }
        sys_command->execute();
        break;

    case SOCKET_CMD_AUDIOSTREAM_ON:
        buf[len] = 0;
        if (len > 2) {
            name = (const char *)&buf[2];
        }
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 1, name, "");
        sys_command->direct_call = DataStreamer :: S_startStream;

        if ((len >= 2) && (buf[0] || buf[1])) {
            sys_command->bufferSize = (((uint32_t)buf[1]) << 8) | buf[0];
        }
        sys_command->execute();
        break;

    case SOCKET_CMD_DEBUGSTREAM_ON:
        // First VIC stream off
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 0, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();

        buf[len] = 0;
        if (len > 2) {
            name = (const char *)&buf[2];
        }
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 2, name, "");
        sys_command->direct_call = DataStreamer :: S_startStream;

        if ((len >= 2) && (buf[0] || buf[1])) {
            sys_command->bufferSize = (((uint32_t)buf[1]) << 8) | buf[0];
        }
        sys_command->execute();
        break;

    case SOCKET_CMD_VICSTREAM_OFF:
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 0, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();
        break;

    case SOCKET_CMD_AUDIOSTREAM_OFF:
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 1, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();
        break;

    case SOCKET_CMD_DEBUGSTREAM_OFF:
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 2, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();
        break;

    case SOCKET_CMD_DEBUG_REG:
        uint8_t reg;
        reg = U64_DEBUG_REGISTER;
        writeSocket(socket, &reg, 1);
        if (len > 0) {
            U64_DEBUG_REGISTER = buf[0];
        }
        break;
#endif
    case SOCKET_CMD_READFLASH:
    	switch (buf[0])
    	{
           case 0:
           {
              unsigned int len;
              unsigned char tmp[4];
              sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_READ_FLASH, FLASH_CMD_PAGESIZE, (uint8_t*) &len, 0);
              sys_command->execute();
              tmp[0] = len;
              tmp[1] = len >> 8;
              tmp[2] = len >> 16;
              tmp[3] = len >> 24;
              writeSocket(socket, tmp, 4);
              break;
           }
           case 1:
           {
              unsigned int len;
              unsigned char tmp[4];
              sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_READ_FLASH, FLASH_CMD_NOPAGES, (uint8_t*) &len, 0);
              sys_command->execute();
              tmp[0] = len;
              tmp[1] = len >> 8;
              tmp[2] = len >> 16;
              tmp[3] = len >> 24;
              writeSocket(socket, tmp, 4);
              break;
           }
           case 2:
           {
              int len;
              int page = (((uint32_t)buf[1]) ) | (((uint32_t)buf[2]) << 8) | (((uint32_t)buf[3]) << 16);
              sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_READ_FLASH, FLASH_CMD_PAGESIZE, (uint8_t*) &len, 0);
              sys_command->execute();
              char* buffer = new char[len];
              sys_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_READ_FLASH, FLASH_CMD_GETPAGE + page, buffer, len);
              sys_command->execute();
              writeSocket(socket, buffer, len);
              delete[] buffer;              
              break;
           }
    	}
    }
    return ok;
}

int SocketDMA::readSocket(int socket, void *buffer, int max_remain)
{
    int received = 0;
    int n;
    uint8_t *buf = (uint8_t *)buffer;
    do {
        int current = (max_remain > 8192) ? 8192 : max_remain;
        n = recv(socket, buf, current, 0);
        buf += n;
        max_remain -= n;
        received += n;
    } while((n > 0) && (max_remain > 0));
    if (n < 0) {
        return n;
    }
    return received;
}

int SocketDMA::writeSocket(int socket, void *buffer, int length)
{
    uint8_t *buf = (uint8_t *)buffer;
    int sent = 0;
    int n;
    do {
        int current = (length > 2048) ? 2048 : length;
        n = send(socket, buf, current, 0);
        buf += n;
        length -= n;
        sent += n;
    } while((n > 0) && (length > 0));
    if (n < 0) {
        return n;
    }
    return sent;
}

void SocketDMA::dmaThread(void *load_buffer)
{
    while (networkConfig.cfg->get_value(CFG_NETWORK_ULTIMATE_DMA_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    puts("Socket DMA server starting");

	int sockfd, newsockfd, portno;
	unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int  n;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
	{
    	puts("ERROR dmaThread opening socket");
    	return;
	}

    printf("DMA Thread Sockfd = %8x\n", sockfd);

    /* Initialize socket structure */
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = 64;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
 	    puts("dmaThread ERROR on binding");
	    return;
    }

    /* Now start listening for the clients, here process will
    * go in sleep mode and will wait for the incoming connection
    */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    uint8_t your_ip[4];
    uint16_t your_port;

    while(1) {
		/* Accept actual connection from the client */
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (newsockfd < 0)
		{
			puts("dmaThread ERROR on accept");
			return;
		}

		uint32_t addr = cli_addr.sin_addr.s_addr;
		your_ip[3] = addr >> 24;
	    your_ip[2] = (addr >> 16) & 0xFF;
	    your_ip[1] = (addr >> 8) & 0xFF;
	    your_ip[0] = addr & 0xFF;

		printf("dmaThread newsockfd = %8x. Client = %d.%d.%d.%d : %d\n", newsockfd, your_ip[0], your_ip[1], your_ip[2], your_ip[3], cli_addr.sin_port);

		/* If connection is established then start communicating */
		uint8_t *mempntr = (uint8_t *)load_buffer;
		uint8_t buf[16];

		bool authenticated = false;
		const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
		if (!*password)
	            authenticated = true;
		while(1) {
	        n = recv(newsockfd, buf, 2, 0);
	        if (n <= 0) {
	            break;
	        }
	        // assume n = 2
	        uint16_t cmd = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);

	        if ((cmd & 0xFF00) != 0xFF00) {
	            printf("Illegal command: %04x\n", cmd);
	            continue;
	        }

	        n = recv(newsockfd, buf, 2, 0);
            if (n <= 0) {
                break;
            }
	        uint16_t len = (uint16_t)buf[0] | (((uint16_t)buf[1]) << 8);
	        uint32_t len32 = len;

	        if ((cmd == SOCKET_CMD_MOUNT_IMG) || (cmd == SOCKET_CMD_RUN_IMG) || (cmd == SOCKET_CMD_RUN_CRT)) {
	            n = recv(newsockfd, buf+2, 1, 0);
                len32 |= (((uint32_t)buf[2]) << 16);
	        }
	        if (len32 > SOCKET_BUFFER_SIZE) {
	            len32 = SOCKET_BUFFER_SIZE;
	        }
	        if (len32) {
	            n = readSocket(newsockfd, mempntr, len32);
	        }
	        if (n <= 0) {
	            break;
	        }
            if (!performCommand(newsockfd, load_buffer, n, cmd, len32, &cli_addr.sin_addr, authenticated)) {
                break;
            }
		}
        puts("ERROR reading from socket");
        closesocket(newsockfd);
    }
    // this will never happen
    closesocket(sockfd);
}

void SocketDMA::identThread(void *_a)
{
    while (networkConfig.cfg->get_value(CFG_NETWORK_ULTIMATE_IDENT_SERVICE) == 0) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    puts("Socket ident server starting");

	int sockfd, newsockfd, portno;
	unsigned long int clilen;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t client_struct_length = sizeof(cli_addr);
    int  n;
    char client_message[256];
    char menu_header[64];

    getProductTitleString(menu_header, sizeof(menu_header), true);

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
	{
    	puts("ERROR identThread opening socket");
    	vTaskDelete(NULL);
	}

    while(1) {
        /* Initialize socket structure */
        memset((char *) &serv_addr, 0, sizeof(serv_addr));
        portno = 64;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(portno);

        /* Now bind the host address using bind() call.*/
        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        {
            puts("identThread ERROR on binding");
            vTaskDelay(500); // some seconds
            continue; // try again
        }
        while(1) {
            // Receive client's message:
            int n = recvfrom(sockfd, client_message, sizeof(client_message), 0,
                (struct sockaddr*)&cli_addr, &client_struct_length);
            if (n < 0) {
                printf("Couldn't receive: %d\n", n);
                vTaskDelay(500); // some seconds
                continue; // try again
            }
            printf("Received Ident Request from IP: %s and port: %i\n",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            if (n > 0) {
                client_message[n] = 0;
            }

            const char *hostname = networkConfig.cfg->get_string(CFG_NETWORK_HOSTNAME);

            char product[64];
            getProductVersionString(product, sizeof(product), true);

            char fpga_version[8];
            sprintf(fpga_version, "1%02x", getFpgaVersion());
#ifdef U64
            char core_version[8];
            sprintf(core_version, "1.%02x", C64_CORE_VERSION);
#endif
            const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);

            if (strncmp(client_message, "json", 4) == 0) {
                client_message[36] = 0;
                JSON_Bool password_protected(*password ? 1 : 0);
                JSON *obj = JSON::Obj()
                    ->add("product", product)
                    ->add("firmware_version", APPL_VERSION_ASCII)
                    ->add("fpga_version", fpga_version)
#ifdef U64
                    ->add("core_version", core_version)
#endif
                    ->add("hostname", hostname)
                    ->add("menu_header", menu_header)
                    ->add("your_string", client_message+4);

                // Current Assembly64 ignores responses with booleans so we only send this if passwords
                // are active, in which case Assembly won't work anyway.
                if (*password) {
                    ((JSON_Object *)obj)->add("password_protected", &password_protected);
                }

                // Add unique id if configured
                const char *unique_id = networkConfig.cfg->get_string(CFG_NETWORK_UNIQUE_ID);
                if (unique_id && *unique_id) {
                    if (strcmp(unique_id, "Default") == 0) {
                        unique_id = getProductUniqueId();
                    }
                    ((JSON_Object *)obj)->add("unique_id", unique_id);
                }

                // Render and send
                const char *msg = obj->render();
                n = sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)&cli_addr,
                        client_struct_length);

            } else {
                // Respond to client:
                if (n > 32) {
                    n = 32;
                }
                sprintf(client_message + n, ",%s,%s", hostname, menu_header);

                n = sendto(sockfd, client_message, strlen(client_message), 0, (struct sockaddr *)&cli_addr,
                        client_struct_length);
            }
            if (n < 0) {
                printf("Can't send, reason: %d\n", n);
            }
        }
    }
    // this will never happen
    lwip_close(sockfd);
}

#include "init_function.h"
InitFunction init_socket64("Raw Socket 64", [](void *_obj, void *_param) { socket_dma = new SocketDMA(); }, NULL, NULL, 102); // global that causes us to exist
