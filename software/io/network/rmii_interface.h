#ifndef RMII_INTERFACE_H
#define RMII_INTERFACE_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "iomap.h"
#include <stdint.h>

// Rx
// Tx
// Free

#define RMII_RX_MAC(x)   *((volatile uint8_t *)(RMII_BASE + 0x00 + x))
#define RMII_RX_PROMISC  *((volatile uint8_t *)(RMII_BASE + 0x07))
#define RMII_RX_ENABLE   *((volatile uint8_t *)(RMII_BASE + 0x08))
#define RMII_TX_ADDRESS  *((volatile uint32_t *)(RMII_BASE + 0x10))
#define RMII_TX_LENGTH   *((volatile uint16_t *)(RMII_BASE + 0x14))
#define RMII_TX_START    *((volatile uint8_t *)(RMII_BASE + 0x18))
#define RMII_TX_IRQACK   *((volatile uint8_t *)(RMII_BASE + 0x19))
#define RMII_TX_BUSY  	 *((volatile uint8_t *)(RMII_BASE + 0x1A))
#define RMII_FREE_BASE   *((volatile uint32_t *)(RMII_BASE + 0x20))
#define RMII_FREE_PUT    *((volatile uint8_t *)(RMII_BASE + 0x24))
#define RMII_ALLOC_POP   *((volatile uint8_t *)(RMII_BASE + 0x2F)) // write
#define RMII_ALLOC_VALID *((volatile uint8_t *)(RMII_BASE + 0x2F)) // read
#define RMII_ALLOC_ID	 *((volatile uint8_t *)(RMII_BASE + 0x28)) // read
#define RMII_ALLOC_SIZE  *((volatile uint16_t *)(RMII_BASE + 0x2A))
#define RMII_FREE_RESET  *((volatile uint8_t *)(RMII_BASE + 0x2E)) // write

struct EthPacket
{
	uint16_t size;
	uint8_t  id;
};

class RmiiInterface
{
	NetworkInterface *netstack;
	uint8_t *ram_buffer;
	uint8_t *ram_base;
	bool link_up;
	uint8_t addr;
	uint8_t local_mac[6];
	QueueHandle_t queue;
	static void startRmiiTask(void *);
    void rmiiTask(void);
    void initRx(void);
    void deinitRx(void);
public:
	RmiiInterface();
	~RmiiInterface();

	void    input_packet(struct EthPacket *pkt);
	err_t output_packet(uint8_t *buffer, int pkt_len);
    void free_buffer(uint8_t *b);
    void rx_interrupt_handler(void);
};

#endif
