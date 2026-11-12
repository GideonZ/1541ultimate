/*
 * network_target.cc
 *
 *  Created on: Jul 22, 2015
 *      Author: Gideon
 */

#include "network_target.h"
#include "network_interface.h"
#include "socket.h"
#include "lwip/opt.h"
#include "netdb.h"
#include "socket_slices.h"
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

// lwIP also uses SO_SNDTIMEO as the time a close may wait for a buffer for the FIN (20 s without
// it); with the send slice it would reset the connection after 100 ms and drop unsent data.
static int close_after_slices(int socketnr)
{
    struct timeval none = { 0, 0 };
    setsockopt(socketnr, SOL_SOCKET, SO_SNDTIMEO, (char *)&none, sizeof(none));
    return lwip_close(socketnr);
}

// A close for a program that is gone (its reset, or an abort of its OPEN): there is no
// data left to protect, so the socket keeps its send slice as the time a close may wait
// for a buffer. The worker is not held for lwIP's 20 s per socket, which the next program
// would otherwise wait out.
static int close_abandoned(int socketnr)
{
    return lwip_close(socketnr);
}

NetworkTarget::NetworkTarget(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[CMD_MAX_REPLY_LEN];
    status_message.message = new uint8_t[CMD_MAX_STATUS_LEN];
    quick_message.message = quick_bytes;
    socket_count = 0;
    discard_read_reply();
    discard_write_chunk();
    write_chunk_stale = false;
    memset(jobs, 0, sizeof(jobs));
    job = NULL;
    queue = NULL;
    worker = NULL;
    resets = 0;
    resets_closed = 0;
    cancel = NULL;
    opened_socket = -1;
    orphan = -1;
}

NetworkTarget::~NetworkTarget()
{
    close_all_sockets();
    delete[] data_message.message;
    delete[] status_message.message;
}


// Socket-free commands, answered on the UCI Server task at its own level: they never
// wait behind the worker (a cancelled name lookup can hold it for 14 s). Their reply is
// built in quick_message, which only this task writes.
bool NetworkTarget :: is_quick(Message *command)
{
    switch(command->message[1]) {
        case NET_CMD_IDENTIFY:
        case NET_CMD_GET_INTERFACE_COUNT:
        case NET_CMD_GET_IPADDR:
        case NET_CMD_GET_NETADDR:
            return true;
    }
    return false;
}

void NetworkTarget :: answer_quick(Message *command, Message **reply, Message **status)
{
    NetworkInterface *interface;

    switch(command->message[1]) {
        case NET_CMD_IDENTIFY:
            *reply  = &c_net_message_identification;
            *status = &c_status_ok;
            break;
        case NET_CMD_GET_INTERFACE_COUNT:
        	quick_message.message[0] = (uint8_t)NetworkInterface :: getNumberOfInterfaces();
        	quick_message.length = 1;
        	quick_message.last_part = true;
        	*reply  = &quick_message;
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
        		*reply = &quick_message;
        		*status = &c_status_ok;
        		interface->getIpAddr(quick_message.message);
        		quick_message.length = 12;
            	quick_message.last_part = true;
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
        		*reply = &quick_message;
        		*status = &c_status_ok;
        		interface->getMacAddr(quick_message.message);
        		quick_message.length = 6;
            	quick_message.last_part = true;
        	} else {
        		*reply = &c_message_empty;
        		*status = &c_status_interface_not_set;
        	}
        	break;
    }
}

bool NetworkTarget :: start_worker(void)
{
    if (!queue) {
        // One more than the jobs: room for the wake-up of a reset.
        queue = xQueueCreate(NET_JOBS + 1, sizeof(NetJob *));
        if (!queue) {
            return false;
        }
        if (xTaskCreate(NetworkTarget :: worker_task, "UCI Network", configMINIMAL_STACK_SIZE, this,
                        PRIO_NETSERVICE, &worker) != pdPASS) {
            vQueueDelete(queue);
            queue = NULL;
            return false;
        }
    }
    return true;
}

// On the UCI Server task: the command is handed to the worker, and the reply follows
// through deferred_reply.
void NetworkTarget :: parse_command(Message *command, Message **reply, Message **status)
{
    *reply = &c_message_empty;
    *status = &c_status_internal_error;
    if (is_quick(command)) {
        answer_quick(command, reply, status);
        return;
    }
    NetJob *j = NULL;
    if (start_worker()) {
        portENTER_CRITICAL();
        for (int i = 0; i < NET_JOBS; i++) {
            if (!jobs[i].in_use) {
                j = &jobs[i];
                j->in_use = true;
                break;
            }
        }
        portEXIT_CRITICAL();
    }
    if (!j) {
        printf("UCI network target busy.\n");
        return;
    }
    j->done = false;
    j->cancel = false;
    j->opened = -1;
    j->command.length = command->length;
    j->command.last_part = command->last_part;
    j->command.message = j->bytes;
    memcpy(j->bytes, command->message, command->length);
    j->ticket = command_defer();
    job = j;
    // A free job means a free place in the queue: each job is queued at most once.
    xQueueSend(queue, &j, 0);
}

void NetworkTarget :: deferred_reply(Message **reply, Message **status)
{
    NetJob *j = job;
    job = NULL;
    if (!j) {
        *reply = &c_message_empty;
        *status = &c_status_internal_error;
        return;
    }
    *reply = j->reply;
    *status = j->status;
    portENTER_CRITICAL();
    j->in_use = false;
    portEXIT_CRITICAL();
}

// On the UCI Server task: abort or C64 reset. A job that is done but not delivered is
// freed here; one that still runs or waits, the worker frees. An OPEN that finished just
// before the abort made a socket the C64 never learnt of: the worker closes it next.
void NetworkTarget :: cancel_job(void)
{
    if (!job) {
        return;
    }
    bool wake = false;
    portENTER_CRITICAL();
    job->cancel = true;
    if (job->done) {
        job->in_use = false;
        if (job->opened >= 0) {
            orphan = job->opened;
            wake = true;
        }
    }
    portEXIT_CRITICAL();
    job = NULL;
    if (wake) {
        wake_worker();
    }
}

// On the UCI Server task. A busy worker sees what it has to do after its command anyway.
void NetworkTarget :: wake_worker(void)
{
    if (queue && !uxQueueMessagesWaiting(queue)) {
        NetJob *wake = NULL;
        xQueueSend(queue, &wake, 0);
    }
}

void NetworkTarget :: worker_task(void *a)
{
    NetworkTarget *t = (NetworkTarget *)a;
    NetJob *j;

    while (1) {
        xQueueReceive(t->queue, &j, portMAX_DELAY);
        t->close_sockets_after_reset();
        t->close_orphan();
        if (!j) {
            continue; // the wake-up of a reset
        }
        if (!j->cancel) { // one that was aborted while queued is not started
            t->cancel = &j->cancel;
            t->opened_socket = -1;
            t->execute(&j->command, &j->reply, &j->status);
            j->opened = t->opened_socket;
            t->cancel = NULL;
        }
        t->close_sockets_after_reset();
        portENTER_CRITICAL();
        bool cancelled = j->cancel;
        uint32_t ticket = j->ticket;
        int opened = j->opened;
        j->done = true;
        if (cancelled) {
            j->in_use = false;
        }
        portEXIT_CRITICAL();
        if (!cancelled) {
            command_deferred_done(ticket); // from here on, the job is not ours
        } else if ((opened >= 0) && t->owns_socket(opened)) {
            // The C64 never learnt the number of this socket.
            t->untrack_socket(opened);
            close_abandoned(opened);
        }
    }
}

// On the worker.
void NetworkTarget :: close_orphan(void)
{
    portENTER_CRITICAL();
    int socketnr = orphan;
    orphan = -1;
    portEXIT_CRITICAL();
    if ((socketnr >= 0) && owns_socket(socketnr)) {
        untrack_socket(socketnr);
        close_abandoned(socketnr);
    }
}

// On the worker: the sockets of the program that was running when the C64 was reset.
void NetworkTarget :: close_sockets_after_reset(void)
{
    uint32_t seen = resets;
    if (seen != resets_closed) {
        close_all_sockets();
        resets_closed = seen;
    }
}

// On the worker.
void NetworkTarget :: execute(Message *command, Message **reply, Message **status)
{
    NetworkInterface *interface;

    // The command interface only accepts a command while it is idle, so a
    // reply that is still being handed out cannot normally be interrupted by
    // one. Dropping it here as well means no path into this target can hand a
    // client bytes left over from an earlier read.
    discard_read_reply();

    // Any other command ends a chunked write that is still accumulating. That
    // is what keeps the accumulation out of the way of `buffer`'s other users,
    // open_socket's name resolution and a read reply being handed out. So does
    // an abort or a C64 reset, which leave the flag for this task to act on.
    bool drop_chunk = write_chunk_stale;
    write_chunk_stale = false;
    if (drop_chunk || (command->message[1] != NET_CMD_WRITE_SOCKET_CHUNK)) {
        discard_write_chunk();
    }

    switch(command->message[1]) {
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
        case NET_CMD_WRITE_SOCKET_CHUNK:
            if (command->length < 7) { // 2 + 5
                *reply = &c_message_empty;
                *status = &c_status_invalid_params;
                discard_write_chunk();
                break;
            }
            write_socket_chunk(command, reply, status);
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

	SliceWait wait(cancel, 0); // as long as lwIP tries, unless the command is aborted
	int ret = connect_sliced(socket, &addr, wait);
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

	// The table has room for every socket lwip can create, so this cannot
	// fail. Refusing rather than writing past the table keeps that a check
	// instead of an assumption.
	if (!track_socket(socket)) {
		*status = &c_status_no_socket;
		lwip_close(socket);
		return;
	}
	opened_socket = socket;
	*reply = &data_message;
	this->data_message.message[0] = (uint8_t)socket;
	this->data_message.length = 1;
	data_message.last_part = true;

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 40000; // 40 ms
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
	// A send waits in slices, so that an abort or a reset reaches a stalled WRITE.
	tv.tv_usec = SOCKET_SLICE_MS * 1000;
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
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
	// Zero bytes means the peer closed only on a stream. On a datagram socket
	// it is an empty datagram, which is data: it is reported as a zero-length
	// read with status OK, and the socket stays open.
	if (ret == 0 && !is_datagram_socket(socketnr)) {
		untrack_socket(socketnr);
		close_after_slices(socketnr);
		*status = &c_status_socket_closed;
		return;
	}
	if (ret >= 0) {
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
// Both write commands report a send from here, so they cannot drift apart in
// what they tell the client. A datagram leaves whole or not at all, so the retries
// in send_sliced repeat only a send that sent nothing, never part of a payload.
void NetworkTarget :: send_to_socket(int socketnr, uint8_t *src, int length, Message **reply, Message **status)
{
    int ret;
    if (owns_socket(socketnr)) {
        SliceWait wait(cancel, 0); // no limit, as before, unless the command is aborted
        int sent = 0, waited_ms = 0;
        while (1) {
            int now;
            ret = send_sliced(socketnr, (const char *)src + sent, length - sent, wait, &now);
            sent += now;
            if (now) {
                waited_ms = 0;
            }
            // With no free buffer on the link (ENOMEM: a WiFi send no longer waits for one),
            // the send is tried again a tick later, as a blocking send used to wait,
            // for at most NET_SEND_NOMEM_MS without progress, and stops at an abort.
            if ((ret == 0) || (errno != ENOMEM) || wait.cancelled() || (waited_ms >= NET_SEND_NOMEM_MS)) {
                break;
            }
            vTaskDelay(1);
            waited_ms += portTICK_PERIOD_MS;
            errno = ENOMEM;
        }
        // A partial send that ends in an error is an error, as send_all reported it.
        if (length == 0) {
            // send_sliced sends nothing for 0 bytes; an empty UDP datagram is still a datagram.
            ret = (lwip_send(socketnr, src, 0, 0) < 0) ? -1 : 0;
        } else if (ret == 0) {
            ret = sent;
        } else {
            ret = -1;
        }
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

void NetworkTarget :: write_socket(Message *command, Message **reply, Message **status)
{
    uint8_t socketnr = command->message[2];
    uint8_t *src = &command->message[3];

    int length = command->length - 3;
    send_to_socket(socketnr, src, length, reply, status);
}

void NetworkTarget :: write_socket_chunk(Message *command, Message **reply, Message **status)
{
    uint8_t socketnr = command->message[2];
    int offset = ((int)command->message[3]) | (((int)command->message[4]) << 8);
    int total = ((int)command->message[5]) | (((int)command->message[6]) << 8);
    int length = command->length - 7;

    *reply = &c_message_empty;

    if (offset == 0) {
        // An opening chunk announces the payload and replaces whatever was
        // accumulating, so a client that restarts a payload gets a clean one.
        discard_write_chunk();
        if (total > NET_MAX_SOCKET_WRITE) {
            *status = &c_status_param_out_of_range;
            return;
        }
        write_handle = socketnr;
        write_total = total;
    } else if ((offset != write_offset) || (socketnr != write_handle) || (total != write_total)) {
        // A chunk carries where it belongs, so one that does not continue the
        // payload in progress, or that arrives when none is, is refused here
        // rather than spliced into a datagram the client never asked for.
        discard_write_chunk();
        *status = &c_status_invalid_params;
        return;
    }

    if ((write_offset + length) > write_total) {
        discard_write_chunk();
        *status = &c_status_invalid_params;
        return;
    }

    memcpy(&buffer[write_offset], &command->message[7], length);
    write_offset += length;

    if (write_offset < write_total) {
        // The payload is short of its total, so nothing goes out yet. The empty
        // reply and this status say the chunk was taken and no more.
        *status = &c_status_ok;
        return;
    }

    // Complete, so it leaves as the single datagram the client asked for. The
    // accumulation ends here, before the send, so a send that fails cannot
    // leave a payload behind for the next chunk to continue.
    int handle = write_handle;
    int payload = write_total;
    discard_write_chunk();
    send_to_socket(handle, buffer, payload, reply, status);
}

void NetworkTarget :: discard_write_chunk(void)
{
    write_handle = 0;
    write_total = 0;
    write_offset = 0;
}

void NetworkTarget :: close_socket(Message *command, Message **reply, Message **status)
{
    uint8_t socketnr = command->message[2];
    int result = -1;
    if (owns_socket(socketnr)) {
        untrack_socket(socketnr);
        result = close_after_slices(socketnr);
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

// A socket handed to the client without a table entry would survive a C64
// reset, which is the defect in #808, so the table must cover every descriptor
// lwip can return. lwip's NUM_SOCKETS is MEMP_NUM_NETCONN, tested here rather
// than including lwip's private socket header.
static_assert(NET_MAX_SOCKETS >= MEMP_NUM_NETCONN,
              "the socket table is smaller than the number of lwip sockets");

bool NetworkTarget :: track_socket(int socketnr)
{
    if (socket_count >= NET_MAX_SOCKETS) {
        return false;
    }
    sockets[socket_count++] = socketnr;
    return true;
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

bool NetworkTarget :: is_datagram_socket(int socketnr)
{
    int type = 0;
    socklen_t len = sizeof(type);
    return (lwip_getsockopt(socketnr, SOL_SOCKET, SO_TYPE, &type, &len) == 0) && (type == SOCK_DGRAM);
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

void NetworkTarget :: close_all_sockets(void)
{
    for (int i = 0; i < socket_count; i++) {
        close_abandoned(sockets[i]);
    }
    socket_count = 0;
}

// On the UCI Server task. The worker closes the sockets, before it runs anything else,
// and a reply that is left over is dropped as the next program's first command starts.
void NetworkTarget :: c64_reset(void)
{
    cancel_job();
    resets++;
    wake_worker();
    // The worker drops a payload half collected when the machine went down: lwip hands out
    // the low descriptors again, so a stale handle could name a live socket of the next program.
    write_chunk_stale = true;
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
    // transaction, which is what a datagram read means anyway. The worker drops it
    // as the next command starts; an aborted command that still runs is cancelled.
    cancel_job();
    // A chunked write goes the same way, so an abandoned payload is never completed by
    // whatever the client sends next; the worker drops it as the next command starts.
    write_chunk_stale = true;
}
