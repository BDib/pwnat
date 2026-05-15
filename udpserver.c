/*
 * Project: udptunnel
 * File: udpserver.c
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
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#ifndef _WIN32
#	include <poll.h>
#	include <netdb.h>
#	include <arpa/inet.h>
#else
#	include "windoze.h"
#	define poll WSAPoll
#endif

#include "common.h"
#include "list.h"
#include "client.h"
#include "message.h"
#include "socket.h"
#include "destination.h"
#include "packet.h"

extern int debug_level;
extern int ipver;
static int running = 1;
static int next_client_id = 1;

/* internal functions */
static int handle_message(uint16_t id, uint8_t msg_type, char* data,
						  int data_len, socket_t* from, list_t* clients,
						  list_t* allowed_destinations, char* port);
static int destination_allowed(list_t* allowed_destinations,
							   const char* host, const char* port);
static void disconnect_and_remove_client(uint16_t id, list_t* clients);
static void signal_handler(int sig);

int udpserver(int argc, char* argv[])
{
	char host_str[ADDRSTRLEN];
	char port_str[ADDRSTRLEN];
	char addrstr[ADDRSTRLEN];
	list_t* clients = NULL;
	list_t* allowed_destinations = NULL;
	socket_t* udp_sock = NULL;
	socket_t* udp_from = NULL;
	char data[MAX_PAYLOAD_LEN];
	client_t* client;
	uint16_t tmp_id;
	uint8_t tmp_type;
	uint16_t tmp_len;
	struct timeval curr_time;
	struct timeval timeout;
	struct timeval check_time;
	struct timeval check_interval;
	int ret;
	int i;
	int allowed_start;
	int icmp_sock;
	int listen_sock;
	int timeexc = 0;
	struct sockaddr_in dest_addr, rsrc;
	uint32_t timeexc_ip;
	struct hostent* host_ent;
    struct pollfd *fds = NULL;
    int fds_size = 0;

	signal(SIGINT, &signal_handler);

	memset(&dest_addr, 0, sizeof(struct sockaddr_in));
	host_ent                    = gethostbyname("3.3.3.3");
	timeexc_ip                  = *(uint32_t*)host_ent->h_addr_list[0];
	dest_addr.sin_family        = AF_INET;
	dest_addr.sin_port          = 0;
	dest_addr.sin_addr.s_addr   = timeexc_ip;

	allowed_start = argc;
	for(i = 0; i < argc; i++)
		if(strchr(argv[i], ':')) {
			allowed_start = i;
			break;
		}
	if(allowed_start == 0) {
		sprintf(port_str, "2222");
		host_str[0] = 0;
	} else if(allowed_start == 1) {
		if(strchr(argv[0], ':') || strchr(argv[0], '.')) {
			strncpy(host_str, argv[0], sizeof(host_str));
			host_str[sizeof(host_str)-1] = 0;
			sprintf(port_str, "2222");
		} else {
			strncpy(port_str, argv[0], sizeof(port_str));
			port_str[sizeof(port_str)-1] = 0;
			host_str[0] = 0;
		}
	} else if(allowed_start >= 2) {
		strncpy(host_str, argv[0], sizeof(host_str));
		strncpy(port_str, argv[1], sizeof(port_str));
		host_str[sizeof(host_str)-1] = 0;
		port_str[sizeof(port_str)-1] = 0;
	}

	if(argc > allowed_start) {
		allowed_destinations = list_create(sizeof(destination_t),
										   p_destination_cmp,
										   p_destination_copy,
										   p_destination_free);
		if(!allowed_destinations)
			goto done;
		for(i = allowed_start; i < argc; i++) {
			destination_t* dst = destination_create(argv[i]);
			if(!dst)
				goto done;
			if(!list_add(allowed_destinations, dst))
				goto done;
			destination_free(dst);
		}
	}
	clients = list_create(sizeof(client_t), p_client_cmp, p_client_copy,
						  p_client_free);
	if(!clients)
		goto done;

	if(!(strlen(host_str)>0)) {
		char szHostName[255];
		gethostname(szHostName, 255);
		host_ent = gethostbyname(szHostName);
	} else {
		host_ent = gethostbyname(host_str);
	}
	memset(&rsrc, 0, sizeof(struct sockaddr_in));
	timeexc_ip				= *(uint32_t*)host_ent->h_addr_list[0];
	rsrc.sin_family        = AF_INET;
	rsrc.sin_port          = 0;
	rsrc.sin_addr.s_addr   = timeexc_ip;

	udp_sock = sock_create((host_str[0] == 0 ? NULL : host_str), port_str,
						   ipver, SOCK_TYPE_UDP, 1, 1);
	if(!udp_sock)
		goto done;
	if(debug_level >= DEBUG_LEVEL1)
		printf("Listening on UDP %s\n",
			   sock_get_str(udp_sock, addrstr, sizeof(addrstr)));

	udp_from = sock_create(NULL, NULL, ipver, SOCK_TYPE_UDP, 0, 0);
	if(!udp_from)
		goto done;

	timerclear(&timeout);
	gettimeofday(&check_time, NULL);
	check_interval.tv_sec = 0;
	check_interval.tv_usec = 500000;
	create_listen_socket(&listen_sock,&dest_addr);
	if(listen_sock == -1) {
		printf("[main] can't open listener socket\n");
		exit(1);
	}
	create_icmp_socket(&icmp_sock);
	if(icmp_sock == -1) {
		printf("[main] can't open raw socket\n");
		exit(1);
	}

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(struct sockaddr_in));
	sa.sin_family = PF_INET;
	sa.sin_port = htons(atoi(port_str));
	sa.sin_addr.s_addr = INADDR_ANY;

	int plen;
	char* ips = malloc(16);
	unsigned char* packet = malloc(IP_MAX_SIZE);

	while(running) {
		if(timeexc++ % 100 == 0) {
			send_icmp(icmp_sock, &rsrc, &dest_addr, NULL, 1);
		}
		while((plen = recv(listen_sock, packet, 100, 0)) > 0) {
			if(plen!=(IPHDR_SIZE+ICMPHDR_SIZE)*2 || packet[IPHDR_SIZE+0] != 11 || packet[IPHDR_SIZE+1] != 0 || packet[IPHDR_SIZE+ICMPHDR_SIZE+9] != 1)
				break;
			sprintf(ips, "%d.%d.%d.%d", (unsigned char)packet[12],(unsigned char) packet[13],(unsigned char) packet[14],(unsigned char) packet[15]);
			memset(packet, 0, plen);
			printf("Got connection request from %s\n", ips);
			host_ent = gethostbyname(ips);
			memcpy(&(sa.sin_addr), host_ent->h_addr, host_ent->h_length);
			inet_pton(PF_INET, ips, &(sa.sin_addr));
			sendto(udp_sock->fd, ips, 0, 0, (struct sockaddr*)&sa, sizeof(struct sockaddr));
		}

        int num_clients = LIST_LEN(clients);
        int needed_fds = 1 + num_clients;

        if (needed_fds > fds_size) {
            fds_size = needed_fds + 10;
            fds = realloc(fds, sizeof(struct pollfd) * fds_size);
            if (!fds) {
                fprintf(stderr, "Error reallocating fds\n");
                goto done;
            }
        }

        fds[0].fd = SOCK_FD(udp_sock);
        fds[0].events = POLLIN;
        for(i = 0; i < num_clients; i++) {
            client = list_get_at(clients, i);
            fds[i+1].fd = SOCK_FD(client->tcp_sock);
            fds[i+1].events = POLLIN;
        }

		ret = poll(fds, needed_fds, 50);
		PERROR_GOTO(ret < 0 && errno != EINTR, "poll", done);

		gettimeofday(&curr_time, NULL);
		if(timercmp(&curr_time, &check_time, >)) {
			for(i = 0; i < LIST_LEN(clients); i++) {
				client = list_get_at(clients, i);
				if(client_timed_out(client, curr_time)) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
					continue;
				}
				ret = client_check_and_resend(client, curr_time);
				if(ret == -2) {
					disconnect_and_remove_client(CLIENT_ID(client), clients);
					i--;
				}
			}
			timeradd(&curr_time, &check_interval, &check_time);
		}
		if(ret <= 0) continue;

		if(fds[0].revents & POLLIN) {
			ret = msg_recv_msg(udp_sock, udp_from, data, sizeof(data),
							   &tmp_id, &tmp_type, &tmp_len);
			if(ret == 0)
				ret = handle_message(tmp_id, tmp_type, data, tmp_len,
									 udp_from, clients,
									 allowed_destinations, port_str);
			if(ret == -2) {
				disconnect_and_remove_client(tmp_id, clients);
			}
		}

		for(i = 0; i < num_clients; i++) {
			client = list_get_at(clients, i);
			if(fds[i+1].revents & (POLLIN | POLLHUP | POLLERR)) {
                uint16_t cid = CLIENT_ID(client);
				ret = client_recv_tcp_data(client);
				if(ret == 0)
					ret = client_send_udp_data(client);
				if(ret < 0) {
					disconnect_and_remove_client(cid, clients);
				}
                break;
			}
		}
	}
done:
    if (fds) free(fds);
	if(debug_level >= DEBUG_LEVEL1)
		printf("Cleaning up...\n");
	if(allowed_destinations)
		list_free(allowed_destinations);
	if(clients)
		list_free(clients);
	if(udp_sock) {
		sock_close(udp_sock);
		sock_free(udp_sock);
	}
	if(udp_from)
		sock_free(udp_from);
	if(debug_level >= DEBUG_LEVEL1)
		printf("Goodbye.\n");
	return 0;
}

void disconnect_and_remove_client(uint16_t id, list_t* clients)
{
	client_t* c;
	if(id == 0)
		return;
	c = list_get(clients, &id);
	if(!c)
		return;
	if(debug_level >= DEBUG_LEVEL1)
		printf("Client %d disconnected.\n", CLIENT_ID(c));
	client_disconnect_tcp(c);
	list_delete(clients, &id);
}

static int handle_message(uint16_t id, uint8_t msg_type, char* data, int data_len,
				   socket_t* from, list_t* clients,
				   list_t* allowed_destinations, char* port_str)
{
	client_t* c = NULL;
	client_t* c2 = NULL;
	socket_t* tcp_sock = NULL;
	int ret = 0;
	if(id != 0) {
		c = list_get(clients, &id);
		if(!c)
			return -1;
	}
	if(id == 0 && msg_type != MSG_TYPE_HELLO)
		return -2;
	switch(msg_type) {
	case MSG_TYPE_GOODBYE:
		ret = -2;
		break;
	case MSG_TYPE_HELLO: {
			int i;
			char port[6];
			char addrstr[ADDRSTRLEN];
			uint16_t req_id;
			if(id != 0)
				break;
			req_id = ntohs(*((uint16_t*)data));
			data += sizeof(uint16_t);
			data_len -= (int)sizeof(uint16_t);
			for(i = 0; i < data_len; i++)
				if(data[i] == ' ')
					break;
			if(i == data_len)
				break;
			data[i++] = 0;
			strncpy(port, data+i, data_len-i);
			port[data_len-i] = 0;
			if(!destination_allowed(allowed_destinations, data, port)) {
				if(debug_level >= DEBUG_LEVEL1)
					printf("Connection to %s:%s denied\n", data, port);
				msg_send_msg(from, next_client_id, MSG_TYPE_GOODBYE, NULL, 0);
				return -2;
			}
			tcp_sock = sock_create(data, port, ipver, SOCK_TYPE_TCP, 0, 0);
			ERROR_GOTO(tcp_sock == NULL, "Error creating tcp socket", error);
			c = client_create((uint16_t)next_client_id++, tcp_sock, from, 0);
			sock_free(tcp_sock);
			ERROR_GOTO(c == NULL, "Error creating client", error);
			c2 = list_add(clients, c);
			ERROR_GOTO(c2 == NULL, "Error adding client to list", error);
			if(debug_level >= DEBUG_LEVEL1) {
				sock_get_str(c2->udp_sock, addrstr, sizeof(addrstr));
				printf("New connection(%d): udp://%s", CLIENT_ID(c2), addrstr);
				sock_get_str(c2->tcp_sock, addrstr, sizeof(addrstr));
				printf(" -> tcp://%s\n", addrstr);
			}
			client_send_helloack(c2, req_id);
			client_reset_keepalive(c2);
			client_free(c);
			break;
		}
	case MSG_TYPE_HELLOACK:
		client_got_helloack(c);
		client_connect_tcp(c, port_str);
		break;
	case MSG_TYPE_KEEPALIVE:
		client_reset_keepalive(c);
		break;
	case MSG_TYPE_DATA0:
	case MSG_TYPE_DATA1:
		ret = client_got_udp_data(c, data, data_len, msg_type);
		if(ret == 0)
			ret = client_send_tcp_data(c);
		break;
	case MSG_TYPE_ACK0:
	case MSG_TYPE_ACK1:
		client_got_ack(c, msg_type);
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
	}
	return ret;
error:
	return -1;
}

static int destination_allowed(list_t* allowed_destinations,
						const char* host, const char* port)
{
	int i;
	if(!allowed_destinations)
		return 1;
	for(i = 0; i < LIST_LEN(allowed_destinations); i++) {
		destination_t* dst = list_get_at(allowed_destinations, i);
		if((!dst->host || !strcmp(dst->host, host))
		   && (!dst->port || !strcmp(dst->port, port)))
			return 1;
	}
	return 0;
}

static void signal_handler(int sig)
{
	switch(sig) {
	case SIGINT:
		running = 0;
	}
}
