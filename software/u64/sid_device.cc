/*
 * sid_device.cc
 *
 *  Created on: Sep 14, 2019
 *      Author: gideon
 */

#include "sid_device.h"
#include "u64_config.h"

SidDevice::SidDevice(int socket, volatile uint8_t *base)
{
    this->socket = socket;
    currentAddress = base;
}

SidDevice::~SidDevice()
{
}

void SidDevice::set_address(volatile uint8_t *base)
{
    currentAddress = base;
}

void SidDevice::pre(void)
{
    u64_configurator.access_socket_pre(socket);
}

void SidDevice::post(void)
{
    u64_configurator.access_socket_post(socket);
}
