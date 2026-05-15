#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <errno.h>

#ifndef _WIN32
#	include <sys/socket.h>
#	include <sys/select.h>
#	include <netinet/in.h>
#	include <netinet/ip.h>
#else
#	include "windoze.h"
#endif

#include "common.h"
#include "list.h"
#include "client.h"
#include "message.h"
#include "socket.h"
#include "destination.h"
#include "packet.h"

uint16_t calculate_checksum(uint16_t *buf, int len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

void create_listen_socket(int* listen_sock, struct sockaddr_in* dest_addr)
{
	*listen_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if(*listen_sock < 0) {
		fprintf(stderr, "Couldn't create privileged icmp socket: %s\n", strerror(errno));
		#ifdef _WIN32
		fprintf(stderr, "WSAGetLastError: %i\n",WSAGetLastError());
		#endif
		return;
	}
	if(fcntl(*listen_sock, F_SETFL, O_NONBLOCK) == -1) {
		perror("F_SETFL");
		return;
	}
	static char packet[ICMPHDR_SIZE];
	memset(packet, 0, ICMPHDR_SIZE);
	sendto(*listen_sock, packet, ICMPHDR_SIZE, 0, (struct sockaddr*)dest_addr, sizeof(struct sockaddr));
}

void create_icmp_socket(int* icmp_sock)
{
	*icmp_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if(*icmp_sock < 0) {
		fprintf(stderr, "Couldn't create privileged raw socket: %s\n", strerror(errno));
		return;
	}
	socket_broadcast(*icmp_sock);
	socket_iphdrincl(*icmp_sock);
}

int send_icmp(int icmp_sock, struct sockaddr_in* rsrc,  struct sockaddr_in* dest_addr, struct sockaddr_in* src_addr, int server)
{
	int pkt_len = IPHDR_SIZE + ICMPHDR_SIZE;
	if(!server) pkt_len += IPHDR_SIZE + ICMPHDR_SIZE;

    char packet[IP_MAX_SIZE];
	memset(packet, 0, IP_MAX_SIZE);

    struct ip_packet_t* ip_pkt = (struct ip_packet_t*)packet;
	ip_pkt->vers_ihl = 0x45;
	ip_pkt->tos = 0;
	ip_pkt->pkt_len = htons(pkt_len);
	ip_pkt->id = 0; /* Kernel fills this if 0 */
	ip_pkt->flags_frag_offset = 0;
	ip_pkt->ttl = IPDEFTTL;
	ip_pkt->proto = 1; /* ICMP */
	ip_pkt->src_ip = rsrc->sin_addr.s_addr;
	ip_pkt->dst_ip = dest_addr->sin_addr.s_addr;
    /* IP checksum handled by kernel or calculated here */
	ip_pkt->checksum = calculate_checksum((uint16_t*)ip_pkt, IPHDR_SIZE);

	struct icmp_packet_t* pkt = (struct icmp_packet_t*)(packet + IPHDR_SIZE);
	pkt->type = server ? 8 : 11; /* Echo Request or Time Exceeded */
	pkt->code = 0;
	pkt->identifier = server ? htons(1) : htons(IPHDR_SIZE + ICMPHDR_SIZE);
	pkt->seq = server ? htons(1337) : 0;

	if(!server) {
        /* Append the 'original' packet that 'failed' */
		struct ip_packet_t* ip_pkt2 = (struct ip_packet_t*)(packet + IPHDR_SIZE + ICMPHDR_SIZE);
		ip_pkt2->vers_ihl = 0x45;
		ip_pkt2->tos = 0;
		ip_pkt2->pkt_len = htons(IPHDR_SIZE + ICMPHDR_SIZE);
		ip_pkt2->id = 0;
		ip_pkt2->flags_frag_offset = 0;
		ip_pkt2->ttl = 1;
		ip_pkt2->proto = 1;
		ip_pkt2->src_ip = dest_addr->sin_addr.s_addr;
		ip_pkt2->dst_ip = src_addr->sin_addr.s_addr;
		ip_pkt2->checksum = calculate_checksum((uint16_t*)ip_pkt2, IPHDR_SIZE);

		struct icmp_packet_t* pkt2 = (struct icmp_packet_t*)(packet + IPHDR_SIZE + ICMPHDR_SIZE + IPHDR_SIZE);
		pkt2->type = 8; /* Echo Request */
		pkt2->code = 0;
		pkt2->identifier = htons(1);
		pkt2->seq = htons(1337);
		pkt2->checksum = calculate_checksum((uint16_t*)pkt2, ICMPHDR_SIZE);

		pkt->checksum = calculate_checksum((uint16_t*)pkt, ICMPHDR_SIZE + IPHDR_SIZE + ICMPHDR_SIZE);
	} else {
		pkt->checksum = calculate_checksum((uint16_t*)pkt, ICMPHDR_SIZE);
	}

	int err = (int)sendto(icmp_sock, packet, pkt_len, 0, (struct sockaddr*)dest_addr, sizeof(struct sockaddr));
	if(err < 0) {
		fprintf(stderr, "Failed to send ICMP packet: %s\n", strerror(errno));
		return -1;
	} else if(err != pkt_len) {
		fprintf(stderr, "Warning: only sent %d of %d bytes\n", err, pkt_len);
    }
	return 0;
}

uint16_t calc_icmp_checksum(uint16_t* data, int bytes)
{
    return calculate_checksum(data, bytes);
}

void socket_broadcast(int sd)
{
	const int one = 1;
	if(setsockopt(sd, SOL_SOCKET, SO_BROADCAST, (char*)&one, sizeof(one)) == -1) {
		perror("setsockopt(SO_BROADCAST)");
	}
}

void socket_iphdrincl(int sd)
{
	const int one = 1;
	if(setsockopt(sd, IPPROTO_IP, IP_HDRINCL, (char*)&one, sizeof(one)) == -1) {
		perror("setsockopt(IP_HDRINCL)");
	}
}
