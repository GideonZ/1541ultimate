#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "indexed_list.h"

#define ERR_OK 0

class HTTPDaemon : public ConfigurableObject
{
	static void http_listen_task(void *a);
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

	HTTPDaemon();
	~HTTPDaemon() { }

	void effectuate_settings(void);
	int listen_task(void);
};

#endif
