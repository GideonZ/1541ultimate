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
Message c_status_interface_not_set   = { 26, true, (uint8_t *)"83,INTERFACE NOT AVAILABLE" };
Message c_status_host_not_resolvable = { 18, true, (uint8_t *)"84,UNRESOLVED HOST" };
Message c_status_no_socket           = { 23, true, (uint8_t *)"85,ERROR OPENING SOCKET" };
Message c_status_socket_closed       = { 28, true, (uint8_t *)"01,CONNECTION CLOSED BY HOST" };
Message c_status_net_no_data         = { 26, true, (uint8_t *)"03,MORE DATA NOT SUPPORTED" };
Message c_status_internal_error      = { 17, true, (uint8_t *)"86,INTERNAL ERROR" };

NetworkTarget::NetworkTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[CMD_MAX_REPLY_LEN];
    status_message.message = new uint8_t[CMD_MAX_STATUS_LEN];
    socket_count = 0;
    discard_read_reply();
}

NetworkTarget::~NetworkTarget()
{
    close_all_sockets();
    delete[] data_message.message;
    delete[] status_message.message;
}


void NetworkTarget :: parse_command(Message *command, Message **reply, Message **status)
{
    NetworkInterface *interface;

    // The command interface only accepts a command while it is idle, so a
    // reply that is still being handed out cannot normally be interrupted by
    // one. Dropping it here as well means no path into this target can hand a
    // client bytes left over from an earlier read.
    discard_read_reply();

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
/*
        case NET_CMD_SET_INTERFACE:
        	*reply = &c_message_empty;
        	if (command->length != 3) {
        		*status = &c_status_invalid_params;
	        } else if (command->message[2] >= (uint8_t)NetworkInterface :: getNumberOfInterfaces()) {
        		*status = &c_status_param_out_of_range;
	        } else {
                *status = &c_status_ok;
        		interface_number = command->message[2];
        	}
        	break;
*/
        case NET_CMD_GET_IPADDR:
            *reply = &c_message_empty;
            if (command->length != 3) {
                *status = &c_status_invalid_params;
                break;
            }
            if (command->message[2] >= (uint8_t)NetworkInterface :: getNumberOfInterfaces()) {
                *status = &c_status_param_out_of_range;
                break;
            }
            interface = NetworkInterface :: getInterface(command->message[2]);
            if (interface) {
        		*reply = &data_message;
        		*status = &c_status_ok;
        		interface->getIpAddr(data_message.message);
        		data_message.length = 12;
            	data_message.last_part = true;
        	} else {
        		*status = &c_status_interface_not_set;
        	}
        	break;
        case NET_CMD_GET_NETADDR:
            *reply = &c_message_empty;
            if (command->length != 3) {
                *status = &c_status_invalid_params;
                break;
            }
            if (command->message[2] >= (uint8_t)NetworkInterface :: getNumberOfInterfaces()) {
                *status = &c_status_param_out_of_range;
                break;
            }
            interface = NetworkInterface :: getInterface(command->message[2]);
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
            *reply = &c_message_empty;
            if (command->length != 15) { // 12 + 3
                *status = &c_status_invalid_params;
                break;
            }
            if (command->message[2] >= (uint8_t)NetworkInterface :: getNumberOfInterfaces()) {
                *status = &c_status_param_out_of_range;
                break;
            }
            interface = NetworkInterface :: getInterface(command->message[2]);
        	if (interface) {
				interface->setIpAddr(&command->message[3]);
                *reply = &c_message_empty;
                *status = &c_status_ok;
        	} else {
        		*reply = &c_message_empty;
        		*status = &c_status_interface_not_set;
        	}
        	break;
        case NET_CMD_OPEN_TCP:
            if (command->length < 5) { // Impossible
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
                break;
            }
            open_socket(command, reply, status, SOCK_STREAM);
			break;
        case NET_CMD_OPEN_UDP:
            if (command->length < 5) { // Impossible
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
                break;
            }
        	open_socket(command, reply, status, SOCK_DGRAM);
        	break;
        case NET_CMD_CLOSE_SOCKET:
            if (command->length != 3) { // 2 + 1
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
                break;
            }
            close_socket(command, reply, status);
            break;
        case NET_CMD_READ_SOCKET:
            if (command->length != 5) { // 2 + 3
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
                break;
            }
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

	// A socket is given up only for one that is going to be handed out, so an
	// open that fails costs the client nothing. This matters for TCP, where a
	// refused or unanswered connect() is the ordinary error path rather than
	// an edge case: a client retrying a connection to a host that is down
	// would otherwise lose every socket it holds, one per attempt.
	int socket = socket(AF_INET, type, 0);
	if (socket < 0 && socket_count >= NET_MAX_SOCKETS) {
		// The pool is out while this client holds its full share of it, so
		// the client gives up its oldest and tries once more. Once only: a
		// second refusal is the rest of the device using the pool rather than
		// this client, and retrying would close everything the client has for
		// nothing. Below the cap the answer stays 85, because a pool the
		// firmware itself filled is not the client's to pay for.
		close_oldest_socket();
		socket = socket(AF_INET, type, 0);
	}
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
		return;
	}

	if (socket > 255) {
        *status = &c_status_internal_error;
        lwip_close(socket);
        return;
	}

	// The cap, applied now that the socket exists and is about to be handed
	// out. Between socket() above and here the client holds one more than the
	// cap, which only happens when the pool had room to spare: had it not,
	// socket() would have failed and taken the evict-and-retry branch.
	//
	// A loop rather than a single test so that track_socket() is provably
	// within the table. sockets[NET_MAX_SOCKETS] would be socket_count
	// itself, so a count that ever exceeded the table would overwrite the
	// count rather than fail where it could be seen.
	while (socket_count >= NET_MAX_SOCKETS) {
		close_oldest_socket();
	}
	track_socket(socket);
	*reply = &data_message;
	this->data_message.message[0] = (uint8_t)socket;
	this->data_message.length = 1;
	data_message.last_part = true;

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 40000; // 40 ms
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
}

void NetworkTarget :: read_socket(Message *command, Message **reply, Message **status)
{
	uint8_t socketnr = command->message[2];
	uint32_t length = ((uint32_t)command->message[3]) | (((uint32_t)command->message[4]) << 8);

    // A complete unfragmented IPv4 UDP datagram is the most this command can
    // ever return, so that is what it accepts. Anything larger cannot arrive.
    if (length > NET_MAX_SOCKET_READ) {
        *status = &c_status_param_out_of_range;
        *reply = &c_message_empty;
        return;
    }

    *reply = &data_message;
    // The whole payload is received here, into a buffer that outlives this
    // call, so that a reply spanning several blocks never has to go back to
    // the socket. A second receive on a datagram socket would take the next
    // datagram rather than the rest of this one.
    //
    // lwip_recvmsg rather than lwip_recv: on a datagram socket it returns the
    // length of the datagram itself and sets MSG_TRUNC when it did not fit,
    // where lwip_recv reports only what was copied and drops the rest without
    // a trace. On a stream socket the two behave the same way.
    struct iovec iov;
    struct msghdr msg;
    iov.iov_base = buffer;
    iov.iov_len = length;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    int ret;
    if (owns_socket(socketnr)) {
        ret = lwip_recvmsg(socketnr, &msg, 0);
    } else {
        errno = EBADF;
        ret = -1;
    }
    // The header is the number of bytes this reply carries, which is what the
    // network target document specifies and what existing clients read. On a
    // reply that spans blocks it is still the total, not the part in the first
    // block, so a client that concatenates the blocks gets exactly the reply a
    // large enough response queue would have delivered in one.
    int copied = (ret > (int)length) ? (int)length : ret;
    data_message.length = 2;
    data_message.last_part = true;
    data_message.message[0] = (copied & 0xFF);
    data_message.message[1] = (copied & 0xFF00) >> 8;

    // printf("Reading %d bytes from socket %d resulted in %d\n", length, socketnr, ret);
	if (ret == 0) {
		untrack_socket(socketnr);
		lwip_close(socketnr);
		*status = &c_status_socket_closed;
		return;
	}
	if (ret > 0) {
		Message *result = &c_status_ok;
		if (msg.msg_flags & MSG_TRUNC) {
			// The datagram was longer than the caller asked for and the rest is
			// gone. Reporting that on the status channel leaves the reply itself
			// unchanged, so a client that does not look is no worse off than
			// before, and one that does can tell a short datagram from a
			// truncated one.
			sprintf((char *)this->status_message.message, "04,DATAGRAM TRUNCATED: %d", ret);
			this->status_message.length = strlen((char *)this->status_message.message);
			result = &status_message;
		}
		start_read_reply(copied, result, reply, status);
		return;
	}
	*status = &status_message;
	sprintf((char *)this->status_message.message, "02,NO DATA: %d", errno);
	this->status_message.length = strlen((char *)this->status_message.message);
}

void NetworkTarget :: start_read_reply(int payload_length, Message *result, Message **reply, Message **status)
{
    read_total = payload_length;
    read_offset = 0;
    read_status = result;

    int chunk = (payload_length > NET_FIRST_BLOCK_PAYLOAD) ? NET_FIRST_BLOCK_PAYLOAD : payload_length;
    memcpy(&data_message.message[2], buffer, chunk);
    read_offset = chunk;
    data_message.length = chunk + 2;
    data_message.last_part = (read_offset >= read_total);

    *reply = &data_message;
    // The command has one result and reports it once, on the block that ends
    // the reply. An intermediate block leaves the status queue empty rather
    // than repeating the text, so a client that appends the status of every
    // block ends up with exactly the text a single block reply would give it.
    *status = data_message.last_part ? read_status : &c_message_empty;
    if (data_message.last_part) {
        discard_read_reply();
    }
}

void NetworkTarget :: discard_read_reply(void)
{
    read_total = 0;
    read_offset = 0;
    read_status = &c_status_ok;
}

#include "dump_hex.h"
void NetworkTarget :: write_socket(Message *command, Message **reply, Message **status)
{
    uint8_t socketnr = command->message[2];
    uint8_t *src = &command->message[3];

    int length = command->length - 3;
    int ret;
    if (owns_socket(socketnr)) {
        ret = lwip_send(socketnr, src, length, 0);
    } else {
        errno = EBADF;
        ret = -1;
    }
    // printf("Writing %d bytes to socket %d resulted in %d\n", length, socketnr, ret);
    // dump_hex_relative(src, length);

    *reply = &data_message;
    data_message.length = 2;
    data_message.last_part = true;
    data_message.message[0] = (ret & 0xFF);
    data_message.message[1] = (ret & 0xFF00) >> 8;

    if (ret < 0) {
		*status = &status_message;
		sprintf((char *)this->status_message.message, "12,SEND ERROR: %d", errno);
		this->status_message.length = strlen((char *)this->status_message.message);
	} else {
	    *status = &c_status_ok;
	}
}

void NetworkTarget :: close_socket(Message *command, Message **reply, Message **status)
{
    uint8_t socketnr = command->message[2];
    int result = -1;
    if (owns_socket(socketnr)) {
        untrack_socket(socketnr);
        result = lwip_close(socketnr);
    } else {
        errno = EBADF;
    }
    *reply = &c_message_empty;
    if (result < 0) {
        *status = &status_message;
        sprintf((char *)this->status_message.message, "12,ERROR ON CLOSE: %d", errno);
        this->status_message.length = strlen((char *)this->status_message.message);
    } else {
        *status = &c_status_ok;
    }
}

void NetworkTarget :: track_socket(int socketnr)
{
    sockets[socket_count++] = socketnr;
}

void NetworkTarget :: untrack_socket(int socketnr)
{
    for (int i = 0; i < socket_count; i++) {
        if (sockets[i] == socketnr) {
            socket_count--;
            for (; i < socket_count; i++) {
                sockets[i] = sockets[i + 1];
            }
            return;
        }
    }
}

bool NetworkTarget :: owns_socket(int socketnr)
{
    for (int i = 0; i < socket_count; i++) {
        if (sockets[i] == socketnr) {
            return true;
        }
    }
    return false;
}

void NetworkTarget :: close_oldest_socket(void)
{
    // untrack_socket() removes exactly the entry named, so a caller that
    // loops on socket_count terminates.
    if (socket_count == 0) {
        return;
    }
    int oldest = sockets[0];
    untrack_socket(oldest);
    lwip_close(oldest);
}

void NetworkTarget :: close_all_sockets(void)
{
    for (int i = 0; i < socket_count; i++) {
        lwip_close(sockets[i]);
    }
    socket_count = 0;
}

void NetworkTarget :: c64_reset(void)
{
    // The reply is dropped here as well as in abort(), because this hook runs
    // for a reset whose CMD_ABORT_DATA post did not fit the queue, and that is
    // exactly the case where abort() is never called. Without it the next
    // program asking for a further block over Data More would be handed what
    // is left of the previous program's read.
    discard_read_reply();
    close_all_sockets();
}

void NetworkTarget :: get_more_data(Message **reply, Message **status)
{
    if (read_offset >= read_total) {
        // Nothing was left pending, so this is a client asking for a block
        // that was never announced.
        *reply = &c_message_empty;
        *status = &c_status_net_no_data;
        return;
    }

    int chunk = read_total - read_offset;
    if (chunk > NET_MAX_REPLY_BLOCK) {
        chunk = NET_MAX_REPLY_BLOCK;
    }
    // A continuation block is payload only. The header went out with the first
    // block and already counted these bytes.
    memcpy(data_message.message, &buffer[read_offset], chunk);
    read_offset += chunk;
    data_message.length = chunk;
    data_message.last_part = (read_offset >= read_total);

    *reply = &data_message;
    *status = data_message.last_part ? read_status : &c_message_empty;
    if (data_message.last_part) {
        discard_read_reply();
    }
}

void NetworkTarget :: abort(int a)
{
    // `a` counts the bytes the client took from the block it abandoned. They
    // are its business; what is left of the payload goes away with the
    // transaction, which is what a datagram read means anyway.
    discard_read_reply();
}
