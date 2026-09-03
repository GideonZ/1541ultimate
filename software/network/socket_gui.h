/*
 * socket_gui.h
 *
 *  Created on: Jun 19, 2015
 *      Author: Gideon
 */

#ifndef NETWORK_SOCKET_GUI_H_
#define NETWORK_SOCKET_GUI_H_

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"

class SocketGui : public ConfigurableObject
{
// NOTE: this flag must not be std::atomic. The Ultimate II+ and II+L run the
// rvlite core (fpga/cpu_unit/rvlite), whose decoder does not implement FENCE:
// decode_comb.vhd leaves "when 3 => -- FENCE" commented out, so the opcode
// falls through to the illegal-instruction trap. GCC brackets every
// std::atomic access with FENCE on rv32i, so a single atomic access ends in
// C_exception_handler's endless loop. A plain volatile bool is sufficient
// here: the flag has one writer and no read-modify-write.
	volatile bool enabled;
public:
	TaskHandle_t listenTaskHandle;

	SocketGui();
	void effectuate_settings(void);
	int listenTask(void);
};

#endif /* NETWORK_SOCKET_GUI_H_ */
