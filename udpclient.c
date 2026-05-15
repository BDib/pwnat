/*
 * Project: udptunnel
 * File: udpclient.c
 *
 * Copyright (C) 2009 Daniel Meekins
 * Contact: dmeekins - gmail
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

#ifndef _WIN32
#	include <poll.h>
#	include <sys/socket.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#else
#	include "windoze.h"
#   define poll WSAPoll
#endif

#include "common.h"
#include "message.h"
#include "socket.h"
#include "packet.h"
#include "client.h"
#include "list.h"

extern int debug_level;
extern int ipver;
static int running = 1;
static uint16_t next_req_id;


/* internal functions */
static int handle_message(client_t* c, uint16_t id, uint8_t msg_type,
						  char* data, int data_len);
static void disconnect_and_remove_client(uint16_t id, list_t* clients);
static void signal_handler(int sig);

int udpclient(int argc, char* argv[])
{
	char* lhost, *lport, *phost, *pport, *rhost, *rport;
	list_t* clients;
	list_t* conn_clients;
	client_t* client;
	client_t* client2;
	socket_t* tcp_serv = NULL;
	socket_t* tcp_sock = NULL;
	socket_t* udp_sock = NULL;
	char data[MAX_PAYLOAD_LEN];
	char addrstr[ADDRSTRLEN];
	char pport_s[6];
	struct timeval curr_time;
	struct timeval check_time;
	struct timeval check_interval;
	uint16_t tmp_id;
	uint8_t tmp_type;
	uint16_t tmp_len;
	uint16_t tmp_req_id;
	int ret;
	int i;
	int icmp_sock ;
	int timeexc = -1;
	struct sockaddr_in src, dest, rsrc;
	struct hostent* hp;
	uint32_t timeexc_ip;
	signal(SIGINT, &signal_handler);
	i = 0;
    if(argc > i && (strchr(argv[i], ':') || strchr(argv[i], '.')))
		lhost = argv[i++];
	else
		lhost = NULL;
	lport = argv[i++];
	phost = argv[i++];
    if(argc > i && (strchr(argv[i], ':') || strchr(argv[i], '.'))) {
		snprintf(pport_s, 5, "2222");
		pport = pport_s;
	} else if (argc > i)
		pport = argv[i++];
    else {
        snprintf(pport_s, 5, "2222");
        pport = pport_s;
    }
	rhost = argv[i++];
	rport = argv[i++];
	/* Get info about localhost IP */
	if(!lhost){
		char szHostName[255];
		gethostname(szHostName, 255);
		hp = gethostbyname(szHostName);
	}else{
		hp = gethostbyname(lhost);
	}
	memset(&rsrc, 0, sizeof(struct sockaddr_in));
	timeexc_ip				= *(uint32_t*)hp->h_addr_list[0];
	rsrc.sin_family			= AF_INET;
	rsrc.sin_port			= 0;
	rsrc.sin_addr.s_addr	= timeexc_ip;
	/* IP of destination */
	memset(&src, 0, sizeof(struct sockaddr_in));
	hp					  = gethostbyname(phost);
	timeexc_ip            = *(uint32_t*)hp->h_addr_list[0];
	src.sin_family        = AF_INET;
	src.sin_port          = 0;
	src.sin_addr.s_addr   = timeexc_ip;
	/* IP of where the fake packet (echo request) was going */
	hp = gethostbyname("3.3.3.3");
	memcpy(&dest.sin_addr, hp->h_addr, hp->h_length);
	inet_pton(AF_INET, "3.3.3.3", &(dest.sin_addr));
	srand(time(NULL));
	next_req_id = rand() % 0xffff;
	/* Create an empty list for the clients */
	clients = list_create(sizeof(client_t), p_client_cmp, p_client_copy,
						  p_client_free);
	ERROR_GOTO(clients == NULL, "Error creating clients list.", done);
	/* Create and empty list for the connecting clients */
	conn_clients = list_create(sizeof(client_t), p_client_cmp, p_client_copy,
							   p_client_free);
	ERROR_GOTO(conn_clients == NULL, "Error creating clients list.", done);
	/* Create a TCP server socket to listen for incoming connections */
	tcp_serv = sock_create(lhost, lport, ipver, SOCK_TYPE_TCP, 1, 1);
	ERROR_GOTO(tcp_serv == NULL, "Error creating TCP socket.", done);
	if(debug_level >= DEBUG_LEVEL1) {
		printf("Listening on TCP %s\n",
			   sock_get_str(tcp_serv, addrstr, sizeof(addrstr)));
	}
	/* Initialize all the timers */
	check_interval.tv_sec = 0;
	check_interval.tv_usec = 500000;
	gettimeofday(&check_time, NULL);
	/* open raw socket */
	create_icmp_socket(&icmp_sock);
	if(icmp_sock == -1) {
		printf("[main] can't open raw socket\n");
		exit(1);
	}
	while(running) {
		if(++timeexc==100) {
			timeexc=0;
			/* Send ICMP TTL exceeded to penetrate remote NAT */
			send_icmp(icmp_sock, &rsrc, &src, &dest, 0);
		}

        int num_clients = LIST_LEN(clients);
        int num_conn_clients = LIST_LEN(conn_clients);
        int total_fds = 1 + num_conn_clients + num_clients * 2;
        struct pollfd *fds = malloc(sizeof(struct pollfd) * total_fds);

        fds[0].fd = SOCK_FD(tcp_serv);
        fds[0].events = POLLIN;
        int current_fdi = 1;

        for(i = 0; i < num_conn_clients; i++) {
            client = list_get_at(conn_clients, i);
            fds[current_fdi].fd = SOCK_FD(client->udp_sock);
            fds[current_fdi].events = POLLIN;
            current_fdi++;
        }
        for(i = 0; i < num_clients; i++) {
            client = list_get_at(clients, i);
            fds[current_fdi].fd = SOCK_FD(client->udp_sock);
            fds[current_fdi].events = POLLIN;
            current_fdi++;
            fds[current_fdi].fd = SOCK_FD(client->tcp_sock);
            fds[current_fdi].events = POLLIN;
            current_fdi++;
        }

		ret = poll(fds, current_fdi, 50);
		PERROR_GOTO(ret < 0 && errno != EINTR, "poll", done);

		gettimeofday(&curr_time, NULL);
		/* Go through all the clients and check if didn't get an ACK for sent
		   data during the timeout period */
		if(timercmp(&curr_time, &check_time, >)) {
			for(i = 0; i < LIST_LEN(clients); i++) {
				client = list_get_at(clients, i);
				ret = client_check_and_resend(client, curr_time);
				if(ret == -2) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
					continue;
				}
				ret = client_check_and_send_keepalive(client, curr_time);
				if(ret == -2) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
				}
			}
			timeradd(&curr_time, &check_interval, &check_time);
		}
		if(ret <= 0) {
            free(fds);
            continue;
        }

		timeexc=0;
		/* Check if pending TCP connection to accept and create a new client
		   and UDP connection if one is ready */
		if(fds[0].revents & POLLIN) {
			tcp_sock = sock_accept(tcp_serv);
			udp_sock = sock_create(phost, pport, ipver,
								   SOCK_TYPE_UDP, 0, 1);
			client = client_create(next_req_id++, tcp_sock, udp_sock, 1);
			if(!client || !tcp_sock || !udp_sock) {
				if(tcp_sock)
					sock_close(tcp_sock);
				if(udp_sock)
					sock_close(udp_sock);
			} else {
				client2 = list_add(conn_clients, client);
				client_free(client);
				client = NULL;
				client_send_hello(client2, rhost, rport, CLIENT_ID(client2));
			}
			sock_free(tcp_sock);
			sock_free(udp_sock);
			tcp_sock = NULL;
			udp_sock = NULL;
		}

		/* Check for pending handshakes from UDP connection */
        current_fdi = 1;
		for(i = 0; i < LIST_LEN(conn_clients); i++) {
			client = list_get_at(conn_clients, i);
			if(fds[current_fdi].revents & POLLIN) {
				tmp_req_id = CLIENT_ID(client);
				ret = client_recv_udp_msg(client, data, sizeof(data),
										  &tmp_id, &tmp_type, &tmp_len);
				if(ret == 0)
					ret = handle_message(client, tmp_id, tmp_type,
										 data, tmp_len);
				if(ret < 0) {
					disconnect_and_remove_client(tmp_req_id, conn_clients);
					i--;
				} else {
					client = list_add(clients, client);
					list_delete_at(conn_clients, i);
					i--;
				}
			}
            current_fdi++;
		}
		/* Check if data is ready from any of the clients */
		for(i = 0; i < LIST_LEN(clients); i++) {
			client = list_get_at(clients, i);
			/* Check for UDP data */
			if(fds[current_fdi].revents & POLLIN) {
				ret = client_recv_udp_msg(client, data, sizeof(data),
										  &tmp_id, &tmp_type, &tmp_len);
				if(ret == 0)
					ret = handle_message(client, tmp_id, tmp_type,
										 data, tmp_len);
				if(ret < 0) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
                    current_fdi += 2;
					continue; /* Don't go to check the TCP connection */
				}
			}
            current_fdi++;
			/* Check for TCP data */
			if(fds[current_fdi].revents & POLLIN) {
				ret = client_recv_tcp_data(client);
				if(ret == 0)
					ret = client_send_udp_data(client);
				if(ret < 0) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
				}
			}
            current_fdi++;
		}
        free(fds);
	}
done:
	if(debug_level >= DEBUG_LEVEL1)
		printf("Cleaning up...\n");
	if(tcp_serv) {
		sock_close(tcp_serv);
		sock_free(tcp_serv);
	}
	if(udp_sock) {
		sock_close(udp_sock);
		sock_free(udp_sock);
	}
	if(clients)
		list_free(clients);
	if(debug_level >= DEBUG_LEVEL1)
		printf("Goodbye.\n");
	return 0;
}

/*
 * Closes the TCP and UDP connections for the client and remove its stuff from
 * the lists.
 */
void disconnect_and_remove_client(uint16_t id, list_t* clients)
{
	client_t* c;
	c = list_get(clients, &id);
	if(!c)
		return;
	client_send_goodbye(c);
	if(debug_level >= DEBUG_LEVEL1)
		printf("Client %d disconnected.\n", CLIENT_ID(c));
	client_disconnect_tcp(c);
	client_disconnect_udp(c);
	list_delete(clients, &id);
}

/*
 * Handles a message received from the UDP tunnel. Returns 0 if successful, -1
 * on some error it handled, or -2 if the client is to disconnect.
 */
int handle_message(client_t* c, uint16_t id, uint8_t msg_type,
				   char* data, int data_len)
{
	int ret = 0;
	char addrstr[ADDRSTRLEN];
	switch(msg_type) {
	case MSG_TYPE_GOODBYE:
		ret = -2;
		break;
	case MSG_TYPE_HELLOACK:
		client_got_helloack(c);
		CLIENT_ID(c) = id;
		ret = client_send_helloack(c, ntohs(*((uint16_t*)data)));
		if(debug_level >= DEBUG_LEVEL1) {
			sock_get_str(c->tcp_sock, addrstr, sizeof(addrstr));
			printf("New connection(%d): tcp://%s", CLIENT_ID(c), addrstr);
			sock_get_str(c->udp_sock, addrstr, sizeof(addrstr));
			printf(" -> udp://%s\n", addrstr);
		}
		break;
	case MSG_TYPE_DATA0:
	case MSG_TYPE_DATA1:
		ret = client_got_udp_data(c, data, data_len, msg_type);
		if(ret == 0)
			ret = client_send_tcp_data(c);
		break;
	case MSG_TYPE_ACK0:
	case MSG_TYPE_ACK1:
		ret = client_got_ack(c, msg_type);
		break;
	case MSG_TYPE_ACK_SEQ: {
        uint32_t ack_seq;
        if (data_len >= (int)sizeof(uint32_t)) {
            memcpy(&ack_seq, data, sizeof(uint32_t));
            client_handle_ack_seq(c, ntohl(ack_seq));
        }
        break;
    }
    case MSG_TYPE_DATA_SEQ:
        ret = client_got_udp_data(c, data, data_len, msg_type);
		if(ret == 0)
			ret = client_send_tcp_data(c);
        break;
	default:
		ret = -1;
		break;
	}
	return ret;
}

static void signal_handler(int sig)
{
	switch(sig) {
	case SIGINT:
		running = 0;
	}
}
