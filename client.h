/*
 * Project: udptunnel
 * File: client.h
 */

#ifndef CLIENT_H
#define CLIENT_H

#include <inttypes.h>
#include <sys/time.h>

#include "common.h"
#include "socket.h"
#include "message.h"

#ifdef HAVE_NICE
#include "ice_transport.h"
#endif

#define CLIENT_TIMEOUT 1 /* in seconds */
#define CLIENT_MAX_RESEND 10
#define WINDOW_SIZE 16

#define CLIENT_WAIT_HELLO 1
#define CLIENT_WAIT_DATA0 2
#define CLIENT_WAIT_DATA1 3
#define CLIENT_WAIT_ACK0  4
#define CLIENT_WAIT_ACK1  5

typedef struct {
    char data[MSG_MAX_LEN];
    int len;
    struct timeval timeout;
    uint32_t seq;
} window_slot_t;

typedef struct client {
	uint16_t id; /* Must be first in struct */
	socket_t* tcp_sock; /* Socket for connection to TCP server */
	transport_t transport; /* Abstract transport (UDP or ICE) */
	int connected;
	struct timeval keepalive;

	/* For data going from tunnel to TCP connection */
	char udp2tcp[MSG_MAX_LEN];
	int udp2tcp_len;
	int udp2tcp_state;
    uint32_t expected_seq;

	/* For data going from TCP connection to tunnel */
    window_slot_t window[WINDOW_SIZE];
    uint32_t next_seq;
    uint32_t last_ack;

	char tcp2udp[MSG_MAX_LEN];
	int tcp2udp_len;
	int tcp2udp_state;
	struct timeval tcp2udp_timeout;
	int resend_count;

#ifdef HAVE_NICE
    ice_transport_t *ice;
#endif
} client_t;

#define CLIENT_ID(c) ((c)->id)

client_t* client_create(uint16_t id, socket_t* tcp_sock, transport_t *trans,
						int connected);
client_t* client_copy(client_t* dst, client_t* src, size_t len);
int client_cmp(client_t* c1, client_t* c2, size_t len);
int client_connect_tcp(client_t* c, char* port);
void client_disconnect_tcp(client_t* c);
void client_disconnect_udp(client_t* c);
void client_free(client_t* c);
int client_recv_udp_msg(client_t* client, char* data, int data_len,
						uint16_t* id, uint8_t* msg_type, uint16_t* len);
int client_got_udp_data(client_t* client, char* data, int data_len,
						uint8_t msg_type);
int client_send_tcp_data(client_t* client);
int client_recv_tcp_data(client_t* client);
int client_send_udp_data(client_t* client);
int client_got_ack(client_t* client, uint8_t ack_type);
void client_handle_ack_seq(client_t* client, uint32_t ack_seq);
int client_send_hello(client_t* client, char* host, char* port,
					  uint16_t req_id);
int client_send_helloack(client_t* client, uint16_t req_id);
int client_got_helloack(client_t* client);
int client_send_goodbye(client_t* client);
int client_check_and_resend(client_t* client, struct timeval curr_tv);
int client_check_and_send_keepalive(client_t* client, struct timeval curr_tv);
void client_reset_keepalive(client_t* client);
int client_timed_out(client_t* client, struct timeval curr_tv);

/* Function pointers to use when making a list_t of clients */
#define p_client_copy ((void* (*)(void *, const void *, size_t))&client_copy)
#define p_client_cmp ((int (*)(const void *, const void *, size_t))&client_cmp)
#define p_client_free ((void (*)(void *))&client_free)

#endif /* CLIENT_H */
