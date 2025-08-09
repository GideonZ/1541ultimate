/*
 * sid_device.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device.h"
#include "u64_config.h"

SidDevice::SidDevice(int socket)
{
    this->socket = socket;
    currentAddress = 0;
}

SidDevice::~SidDevice()
{
}

volatile uint8_t *SidDevice::pre(void)
{
    currentAddress = u64_configurator->access_socket_pre(socket);
    return currentAddress;
}

void SidDevice::post(void)
{
    u64_configurator->access_socket_post(socket);
}
