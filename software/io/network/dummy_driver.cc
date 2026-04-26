/*
 * dummy_driver.c
 *
 *  Created on: Feb 21, 2019
 *      Author: gideon
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "dummy_driver.h"
#include "dump_hex.h"
#include "flash.h"


DummyDriver dummy_interface; // global object!

// entry point for free buffer callback
void DummyDriver_free_buffer(void *drv, void *b) {
    ((DummyDriver *)drv)->free_buffer((uint8_t *)b);
}

// entry point for output packet callback
uint8_t DummyDriver_output(void *drv, void *b, int len) {
    return ((DummyDriver *)drv)->output_packet((uint8_t *)b, len);
}


DummyDriver :: DummyDriver()
{
    netstack = NULL;
    link_up = false;
}

void DummyDriver :: init(void)
{
    netstack = getNetworkStack(this, DummyDriver_output, DummyDriver_free_buffer);

    uint8_t flash_serial[8];
    Flash *flash = get_flash();
    flash->read_serial(flash_serial);

    local_mac[0] = 0x02;
    local_mac[1] = 0x15;
    local_mac[2] = 0x41;
    local_mac[3] = flash_serial[1] ^ flash_serial[5];
    local_mac[4] = flash_serial[2] ^ flash_serial[6];
    local_mac[5] = flash_serial[3] ^ flash_serial[7];

    if (netstack) {
        netstack->set_mac_address(local_mac);
        netstack->start();
    }
}

void DummyDriver :: forceLinkUp(void)
{
    printf("Bringing link up.\n");
    if (netstack) {
        netstack->link_up();
        link_up = true;
    }
}

void DummyDriver :: forceLinkDown(void)
{
    printf("Bringing link down.\n");
    if (netstack) {
        netstack->link_down();
        link_up = false;
    }
}

DummyDriver :: ~DummyDriver()
{
    if(netstack) {
        netstack->stop();
        releaseNetworkStack(netstack);
    }
}

void DummyDriver :: free_buffer(uint8_t *buffer)
{
    printf("FREE DUMMY BUFFER CALLED %p!\n", buffer);
}

uint8_t DummyDriver :: output_packet(uint8_t *buffer, int pkt_len)
{
    printf("Dummy Driver sending data:\n");
    dump_hex_relative(buffer, (pkt_len > 64)?64:pkt_len);
    return 0;
}
