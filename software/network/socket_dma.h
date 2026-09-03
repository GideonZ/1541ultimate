/*
 * socket_test.h
 *
 *  Created on: Apr 12, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_DMA_H_
#define NETWORK_SOCKET_DMA_H_

#include "menu.h"
#include "filemanager.h"
#include "subsys.h"
#include "config.h"

#define SOCKET_BUFFER_SIZE 200000

class SocketDMA : public ConfigurableObject {
	static void dmaThread(void *a);
	static void identThread(void *a);
	static bool performCommand(int socket, void *load_buffer, int length, uint16_t cmd, uint32_t len, struct in_addr *client_ip, bool &authenticated);
	static int  readSocket(int socket, void *buffer, int max_remain);
	static int  writeSocket(int socket, void *buffer, int length);

	uint8_t *load_buffer;
// NOTE: this flag must not be std::atomic. The Ultimate II+ and II+L run the
// rvlite core (fpga/cpu_unit/rvlite), whose decoder does not implement FENCE:
// decode_comb.vhd leaves "when 3 => -- FENCE" commented out, so the opcode
// falls through to the illegal-instruction trap. GCC brackets every
// std::atomic access with FENCE on rv32i, so a single atomic access ends in
// C_exception_handler's endless loop. A plain volatile bool is sufficient
// here: the flag has one writer and no read-modify-write.
	volatile bool dmaEnabled;
	volatile bool identEnabled;
public:
	SocketDMA();
	virtual ~SocketDMA();
	void effectuate_settings(void);

};

#endif /* NETWORK_SOCKET_DMA_H_ */
