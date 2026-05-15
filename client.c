/*
 * Project: udptunnel
 * File: client.c
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "common.h"
#include "client.h"
#include "socket.h"
#ifdef HAVE_NICE
#include "ice_transport.h"
extern char *opt_stun;
extern char *opt_turn;
extern char *opt_turn_user;
extern char *opt_turn_pass;
#endif

extern int debug_level;

client_t* client_create(uint16_t id, socket_t* tcp_sock, transport_t *trans,
						int connected)
{
	client_t* c = NULL;
	c = calloc(1, sizeof(client_t));
	if(!c)
		goto error;
	c->id = id;
	c->tcp_sock = sock_copy(tcp_sock);
	c->transport = *trans;
    if (trans->sock) c->transport.sock = sock_copy(trans->sock);

	c->udp2tcp_state = CLIENT_WAIT_HELLO;
	c->connected = connected;
    c->expected_seq = 0;
    c->next_seq = 0;
    c->last_ack = 0;
	timerclear(&c->keepalive);
	c->resend_count = 0;
#ifdef HAVE_NICE
    c->ice = NULL;
#endif
	return c;
error:
	if(c) {
		if(c->tcp_sock)
			sock_free(c->tcp_sock);
		free(c);
	}
	return NULL;
}

client_t* client_copy(client_t* dst, client_t* src, size_t len)
{
	if(!dst || !src)
		return NULL;
	memcpy(dst, src, sizeof(*src));
	dst->tcp_sock = sock_copy(src->tcp_sock);
	if(!dst->tcp_sock)
		return NULL;
    if (src->transport.sock) {
        dst->transport.sock = sock_copy(src->transport.sock);
    }
	return dst;
}

int client_cmp(client_t* c1, client_t* c2, size_t len)
{
	return c1->id - c2->id;
}

int client_connect_tcp(client_t* c, char* port)
{
	if(!c->connected) {
		if(sock_connect(c->tcp_sock, 0, port) == 0) {
			c->connected = 1;
			return 0;
		}
	}
	return -1;
}

void client_disconnect_tcp(client_t* c)
{
	if(c->connected) {
		sock_close(c->tcp_sock);
		c->connected = 0;
	}
}

void client_disconnect_udp(client_t* c)
{
	if (c->transport.sock) sock_close(c->transport.sock);
}

void client_free(client_t* c)
{
	if(c) {
		sock_free(c->tcp_sock);
		if (c->transport.sock) sock_free(c->transport.sock);
#ifdef HAVE_NICE
        if (c->ice) ice_transport_free(c->ice);
#endif
		free(c);
	}
}

int client_recv_udp_msg(client_t* client, char* data, int data_len,
						uint16_t* id, uint8_t* msg_type, uint16_t* len)
{
	int ret;
	socket_t from;
	ret = msg_recv_msg(&client->transport, &from, data, data_len,
					   id, msg_type, len);
	if(ret < 0)
		return ret;
    if (client->transport.type == TRANS_UDP && client->transport.sock) {
        if(!sock_addr_equal(client->transport.sock, &from))
            return -1;
    }
	return 0;
}

int client_got_udp_data(client_t* client, char* data, int data_len,
						uint8_t msg_type)
{
    if (msg_type == MSG_TYPE_DATA_SEQ) {
        uint32_t seq;
        if (data_len < (int)sizeof(uint32_t)) return -1;
        memcpy(&seq, data, sizeof(uint32_t));
        seq = ntohl(seq);

        if (seq == client->expected_seq) {
            memcpy(client->udp2tcp, data + sizeof(uint32_t), data_len - (int)sizeof(uint32_t));
            client->udp2tcp_len = data_len - (int)sizeof(uint32_t);
            client->expected_seq++;
            uint32_t ack_seq = htonl(seq);
            msg_send_msg(&client->transport, client->id, MSG_TYPE_ACK_SEQ, (char*)&ack_seq, sizeof(uint32_t));
            return 0;
        } else {
            uint32_t ack_seq = htonl(client->expected_seq - 1);
            msg_send_msg(&client->transport, client->id, MSG_TYPE_ACK_SEQ, (char*)&ack_seq, sizeof(uint32_t));
            return 1;
        }
    }

	int is_resend = 0;
	if(data_len > MSG_MAX_LEN)
		return -1;
	if((msg_type == MSG_TYPE_DATA0 && client->udp2tcp_state == CLIENT_WAIT_DATA0)
	   || (msg_type == MSG_TYPE_DATA1 && client->udp2tcp_state == CLIENT_WAIT_DATA1)) {
		memcpy(client->udp2tcp, data, data_len);
		client->udp2tcp_len = data_len;
	} else
		is_resend = 1;

    uint8_t ack_type = (msg_type == MSG_TYPE_DATA0) ? MSG_TYPE_ACK0 : MSG_TYPE_ACK1;
	msg_send_msg(&client->transport, client->id, ack_type, NULL, 0);

	if(is_resend)
		return 1;
	client->udp2tcp_state = client->udp2tcp_state == CLIENT_WAIT_DATA0 ?
							CLIENT_WAIT_DATA1 : CLIENT_WAIT_DATA0;
	return 0;
}

int client_send_tcp_data(client_t* client)
{
	int ret;
	ret = sock_send(client->tcp_sock, client->udp2tcp, client->udp2tcp_len);
	if(ret < 0)
		return -1;
	else if(ret == 0)
		return -2;
	else
		return 0;
}

int client_recv_tcp_data(client_t* client)
{
	int ret;
    if ((uint32_t)(client->next_seq - client->last_ack) >= WINDOW_SIZE)
        return 1;

	if(client->udp2tcp_state == CLIENT_WAIT_HELLO)
		return 1;

	ret = sock_recv(client->tcp_sock, NULL, client->tcp2udp,
					sizeof(client->tcp2udp));
	if(ret < 0)
		return -1;
	if(ret == 0)
		return -2;
	client->tcp2udp_len = ret;
	return 0;
}

int client_send_udp_data(client_t* client)
{
    uint32_t seq = client->next_seq++;
    int slot = seq % WINDOW_SIZE;
    memcpy(client->window[slot].data, client->tcp2udp, client->tcp2udp_len);
    client->window[slot].len = client->tcp2udp_len;
    client->window[slot].seq = seq;
    gettimeofday(&client->window[slot].timeout, NULL);
    client->window[slot].timeout.tv_sec += CLIENT_TIMEOUT;

    char buf[MSG_MAX_LEN + sizeof(uint32_t)];
    uint32_t nseq = htonl(seq);
    memcpy(buf, &nseq, sizeof(uint32_t));
    memcpy(buf + sizeof(uint32_t), client->tcp2udp, client->tcp2udp_len);

	return msg_send_msg(&client->transport, client->id, MSG_TYPE_DATA_SEQ,
					   buf, client->tcp2udp_len + (int)sizeof(uint32_t));
}

int client_got_ack(client_t* client, uint8_t ack_type)
{
	if(ack_type == MSG_TYPE_ACK0 && client->tcp2udp_state == CLIENT_WAIT_ACK0) {
		client->tcp2udp_state = CLIENT_WAIT_DATA1;
		client->resend_count = 0;
		return 0;
	}
	if(ack_type == MSG_TYPE_ACK1 && client->tcp2udp_state == CLIENT_WAIT_ACK1) {
		client->tcp2udp_state = CLIENT_WAIT_DATA0;
		client->resend_count = 0;
		return 0;
	}
	return -1;
}

void client_handle_ack_seq(client_t* client, uint32_t ack_seq) {
    if ((int32_t)(ack_seq - client->last_ack) >= 0) {
        client->last_ack = ack_seq + 1;
    }
}

int client_send_hello(client_t* client, char* host, char* port,
					  uint16_t req_id)
{
	return msg_send_hello(&client->transport, host, port, req_id);
}

int client_send_helloack(client_t* client, uint16_t req_id)
{
	req_id = htons(req_id);
	return msg_send_msg(&client->transport, client->id, MSG_TYPE_HELLOACK,
						(char*)&req_id, sizeof(req_id));
}

int client_got_helloack(client_t* client)
{
	if(client->udp2tcp_state == CLIENT_WAIT_HELLO)
		client->udp2tcp_state = CLIENT_WAIT_DATA0;
	return 0;
}

int client_send_goodbye(client_t* client)
{
	return msg_send_msg(&client->transport, client->id, MSG_TYPE_GOODBYE,
						NULL, 0);
}

int client_check_and_resend(client_t* client, struct timeval curr_tv)
{
    for (uint32_t i = client->last_ack; i != client->next_seq; i++) {
        int slot = i % WINDOW_SIZE;
        if (timercmp(&curr_tv, &client->window[slot].timeout, >)) {
            char buf[MSG_MAX_LEN + sizeof(uint32_t)];
            uint32_t nseq = htonl(client->window[slot].seq);
            memcpy(buf, &nseq, sizeof(uint32_t));
            memcpy(buf + sizeof(uint32_t), client->window[slot].data, client->window[slot].len);
            msg_send_msg(&client->transport, client->id, MSG_TYPE_DATA_SEQ,
                           buf, client->window[slot].len + (int)sizeof(uint32_t));
            client->window[slot].timeout = curr_tv;
            client->window[slot].timeout.tv_sec += CLIENT_TIMEOUT;
        }
    }
	return 0;
}

int client_check_and_send_keepalive(client_t* client, struct timeval curr_tv)
{
	if(client_timed_out(client, curr_tv)) {
		curr_tv.tv_sec += KEEP_ALIVE_SECS;
		memcpy(&client->keepalive, &curr_tv, sizeof(struct timeval));
		return msg_send_msg(&client->transport, client->id, MSG_TYPE_KEEPALIVE,
							NULL, 0);
	}
	return 0;
}

void client_reset_keepalive(client_t* client)
{
	struct timeval curr;
	gettimeofday(&curr, NULL);
	curr.tv_sec += KEEP_ALIVE_TIMEOUT_SECS;
	memcpy(&client->keepalive, &curr, sizeof(struct timeval));
}

int client_timed_out(client_t* client, struct timeval curr_tv)
{
	if(timercmp(&curr_tv, &client->keepalive, >))
		return 1;
	else
		return 0;
}
