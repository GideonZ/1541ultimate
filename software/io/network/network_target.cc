/*
 * network_target.cc
 *
 *  Created on: Jul 22, 2015
 *      Author: Gideon
 */

#include "network_target.h"

NetworkTarget net(3);

NetworkTarget::NetworkTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
}

NetworkTarget::~NetworkTarget()
{
    delete[] data_message.message;
    delete[] status_message.message;
}

Message c_net_message_identification = { 34, true, (uint8_t *)"ULTIMATE-II NETWORK INTERFACE V1.0" };

void NetworkTarget :: parse_command(Message *command, Message **reply, Message **status)
{
    switch(command->message[1]) {
        case NET_CMD_IDENTIFY:
            *reply  = &c_net_message_identification;
            *status = &c_status_ok;
            break;
        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command;
    }
}

void NetworkTarget :: get_more_data(Message **reply, Message **status)
{

}

void NetworkTarget :: abort(void)
{

}
