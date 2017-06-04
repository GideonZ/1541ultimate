/*
 * network_target.cc
 *
 *  Created on: Jul 22, 2015
 *      Author: Gideon
 */

#include "network_target.h"
#include "network_interface.h"
#include "socket.h"
#include "netdb.h"
#include <errno.h>
#include <stdio.h>

NetworkTarget net(3);

Message c_net_message_identification = { 34, true, (uint8_t *)"ULTIMATE-II NETWORK INTERFACE V1.0" };
Message c_status_invalid_params      = { 17, true, (uint8_t *)"81,INVALID PARAMS" };
Message c_status_param_out_of_range  = { 28, true, (uint8_t *)"82,PARAMETER(S) OUT OF RANGE" };
Message c_status_interface_not_set   = { 20, true, (uint8_t *)"83,INTERFACE NOT SET" };
Message c_status_host_not_resolvable = { 18, true, (uint8_t *)"84,UNRESOLVED HOST" };
Message c_status_no_socket           = { 23, true, (uint8_t *)"85,ERROR OPENING SOCKET" };
Message c_status_socket_closed       = { 28, true, (uint8_t *)"01,CONNECTION CLOSED BY HOST" };
Message c_status_net_no_data         = { 26, true, (uint8_t *)"03,MORE DATA NOT SUPPORTED" };

NetworkTarget::NetworkTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
    interface_number = 0;
}

NetworkTarget::~NetworkTarget()
{
    delete[] data_message.message;
    delete[] status_message.message;
}


void NetworkTarget :: parse_command(Message *command, Message **reply, Message **status)
{
	NetworkInterface *interface = NetworkInterface :: getInterface(interface_number);
	switch(command->message[1]) {
        case NET_CMD_IDENTIFY:
            *reply  = &c_net_message_identification;
            *status = &c_status_ok;
            break;
        case NET_CMD_GET_INTERFACE_COUNT:
        	data_message.message[0] = (uint8_t)NetworkInterface :: getNumberOfInterfaces();
        	data_message.length = 1;
        	data_message.last_part = true;
        	*reply  = &data_message;
            *status = &c_status_ok;
        	break;
        case NET_CMD_SET_INTERFACE:
        	*reply = &c_message_empty;
        	if (command->length != 3)
        		*status = &c_status_invalid_params;
        	else if (command->message[2] >= (uint8_t)NetworkInterface :: getNumberOfInterfaces())
        		*status = &c_status_param_out_of_range;
        	else
        		interface_number = command->message[2];
        	break;
        case NET_CMD_GET_IPADDR:
        	if (interface) {
        		*reply = &data_message;
        		*status = &c_status_ok;
        		interface->getIpAddr(data_message.message);
        		data_message.length = 12;
            	data_message.last_part = true;
        	} else {
        		*reply = &c_message_empty;
        		*status = &c_status_interface_not_set;
        	}
        	break;
        case NET_CMD_GET_NETADDR:
        	if (interface) {
        		*reply = &data_message;
        		*status = &c_status_ok;
        		interface->getMacAddr(data_message.message);
        		data_message.length = 6;
            	data_message.last_part = true;
        	} else {
        		*reply = &c_message_empty;
        		*status = &c_status_interface_not_set;
        	}
        	break;
        case NET_CMD_SET_IPADDR:
        	if (interface) {
        		if (command->length != 14) { // 2 + 12
					*reply = &c_message_empty;
            		*status = &c_status_invalid_params;
        		} else {
					*reply = &c_message_empty;
					*status = &c_status_ok;
        		}
				interface->setIpAddr(&command->message[2]);
        	} else {
        		*reply = &c_message_empty;
        		*status = &c_status_interface_not_set;
        	}
        	break;
        case NET_CMD_OPEN_TCP:
        	open_socket(command, reply, status, SOCK_STREAM);
			break;
        case NET_CMD_OPEN_UDP:
        	open_socket(command, reply, status, SOCK_DGRAM);
        	break;
        case NET_CMD_READ_SOCKET:
        	read_socket(command, reply, status);
        	break;
        case NET_CMD_WRITE_SOCKET:
        	write_socket(command, reply, status);
        	break;
        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command;
    }
}

void NetworkTarget :: open_socket(Message *command, Message **reply, Message **status, int type)
{
	*reply = &c_message_empty;
	*status = &c_status_ok;

	uint16_t port_number = uint16_t(command->message[2]) | (uint16_t(command->message[3]) << 8);
	struct sockaddr_in addr;
	  /* set up address to connect to */
	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = PP_HTONS(port_number);
	command->message[command->length] = 0;

	struct hostent hent;
	struct hostent *hent_result;
	int h_errno;

	gethostbyname_r((const char *)&command->message[4], &hent, (char *)buffer, NET_CMD_BUFSIZE, &hent_result, &h_errno);

	if (hent_result) {
		struct in_addr *s;
		s = (struct in_addr *)hent_result->h_addr_list[0];
		addr.sin_addr.s_addr = s->s_addr;
	} else {
		*status = &c_status_host_not_resolvable;
		return;
	}

	int socket = socket(AF_INET, type, 0);
	if (socket < 0) {
		*status = &c_status_no_socket;
		return;
	}

	int ret = connect(socket, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		*status = &status_message;
		sprintf((char *)this->status_message.message, "11,ERROR ON CONNECT: %d", errno);
		this->status_message.length = strlen((char *)this->status_message.message);
		lwip_close(socket);
	}
	*reply = &data_message;
	memcpy(this->data_message.message, &socket, sizeof(int));
	this->data_message.length = sizeof(int);
	data_message.last_part = true;

	struct timeval tv;
	tv.tv_sec = 4;
	tv.tv_usec = 4;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
}

void NetworkTarget :: read_socket(Message *command, Message **reply, Message **status)
{
	int socket;
	int length;
	uint8_t *dest = (uint8_t *)&socket;
	uint8_t *src = &command->message[2];
	for(int i=0;i<sizeof(int);i++) {
		*(dest++) = *(src++);
	}
	dest = (uint8_t *)&length;
	for(int i=0;i<sizeof(int);i++) {
		*(dest++) = *(src++);
	}

	*reply = &c_message_empty;
	int ret = lwip_recv(socket, data_message.message, length, 0);
	if (ret == 0) {
		lwip_close(socket);
		*status = &c_status_socket_closed;
		return;
	}
	if (ret > 0) {
		*reply = &data_message;
		data_message.length = ret;
    	data_message.last_part = true;
		*status = &c_status_ok;
		return;
	}
	*status = &status_message;
	sprintf((char *)this->status_message.message, "02,NO DATA: %d", errno);
	this->status_message.length = strlen((char *)this->status_message.message);
}

void NetworkTarget :: write_socket(Message *command, Message **reply, Message **status)
{
	int socket;
	uint8_t *dest = (uint8_t *)&socket;
	uint8_t *src = &command->message[2];
	for(int i=0;i<sizeof(int);i++) {
		*(dest++) = *(src++);
	}
	int length = command->length - 4;
	int ret = lwip_send(socket, src, length, 0);
	if (ret < 0) {
		*status = &status_message;
		sprintf((char *)this->status_message.message, "12,SEND ERROR: %d", errno);
		this->status_message.length = strlen((char *)this->status_message.message);
		return;
	}
	*reply = &c_message_empty;
	*status = &c_status_ok;
}


void NetworkTarget :: get_more_data(Message **reply, Message **status)
{
	*reply = &c_message_empty;
	*status = &c_status_net_no_data;
}

void NetworkTarget :: abort(void)
{

}
