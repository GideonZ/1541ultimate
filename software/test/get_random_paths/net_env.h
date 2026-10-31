/*
 * net_env.h
 *
 * What NetworkTarget::get_random() and the status messages, cut out of network_target.cc
 * and command_intf.cc as they are (target/pc/linux/getrandompaths/Makefile), are built
 * against on the build host.
 */

#ifndef NET_ENV_H_
#define NET_ENV_H_

#include <stdio.h>
#include <string.h>
#include "command_intf.h"
#define class struct // get_random() is private
#include "network_target.h"
#undef class
#include "wifi_random.h"

extern Message c_message_empty;
extern Message c_status_ok;

#endif /* NET_ENV_H_ */
