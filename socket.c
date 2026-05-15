/*
 * Project: udptunnel
 * File: socket.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unistd.h>
#include <inttypes.h>
#ifndef _WIN32
#	include <sys/socket.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#else
#	include "windoze.h"
#endif /*WIN32*/

#include <errno.h>
#include "socket.h"
#include "common.h"
#ifdef HAVE_NICE
#include "ice_transport.h"
#endif

extern int debug_level;

void print_hexdump(char* data, int len);

socket_t* sock_create(char* host, char* port, int ipver, int sock_type,
					  int is_serv, int conn)
{
	socket_t* sock = NULL;
	struct addrinfo hints;
	struct addrinfo* info = NULL;
	struct sockaddr* paddr;
	int ret;
	sock = calloc(1, sizeof(*sock));
	if(!sock)
		return NULL;
	paddr = (struct sockaddr*)&sock->addr;
	sock->fd = -1;
	switch(sock_type) {
	case SOCK_TYPE_TCP:
		sock->type = SOCK_STREAM;
		break;
	case SOCK_TYPE_UDP:
		sock->type = SOCK_DGRAM;
		break;
	default:
		goto error;
	}
	if(host == NULL && port == NULL)
		goto done;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = (ipver == SOCK_IPV6) ? AF_INET6 : AF_INET;
	hints.ai_socktype = sock->type;
	hints.ai_flags = is_serv ? AI_PASSIVE : 0;
	ret = getaddrinfo(host, port, &hints, &info);
	if (ret != 0) goto error;
	memcpy(paddr, info->ai_addr, info->ai_addrlen);
	sock->addr_len = info->ai_addrlen;
	if(conn) {
		if(sock_connect(sock, is_serv, port) != 0)
			goto error;
	}
done:
	if(info)
		freeaddrinfo(info);
	return sock;
error:
	if(sock)
		free(sock);
	if(info)
		freeaddrinfo(info);
	return NULL;
}

socket_t* sock_copy(socket_t* sock)
{
	socket_t* new;
	if (!sock) return NULL;
	new = malloc(sizeof(*sock));
	if(!new)
		return NULL;
	memcpy(new, sock, sizeof(*sock));
	return new;
}

int sock_connect(socket_t* sock, int is_serv, char* port)
{
	struct sockaddr* paddr;
	int ret;
	int opt = 1;

	ERROR_GOTO(sock->fd != -1, "Socket already connected.", error);
	paddr = SOCK_ADDR(sock);
	sock->fd = socket(paddr->sa_family, sock->type, sock->type == SOCK_DGRAM ? IPPROTO_UDP : 0);
	PERROR_GOTO(sock->fd < 0, "socket", error);

	if (is_serv) {
		setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
		ret = bind(sock->fd, paddr, sock->addr_len);
		PERROR_GOTO(ret != 0, "bind", error);

		if(sock->type == SOCK_STREAM) {
			ret = listen(sock->fd, BACKLOG);
			PERROR_GOTO(ret != 0, "listen", error);
		}
	} else {
		if(sock->type == SOCK_STREAM) {
			ret = connect(sock->fd, paddr, sock->addr_len);
			PERROR_GOTO(ret != 0, "connect", error);
		}
	}
	return 0;
error:
	return -1;
}

socket_t* sock_accept(socket_t* serv)
{
	socket_t* client;
	client = calloc(1, sizeof(*client));
	if(!client)
		goto error;
	client->type = serv->type;
	client->addr_len = sizeof(struct sockaddr_storage);
	client->fd = accept(serv->fd, SOCK_ADDR(client), &client->addr_len);
	PERROR_GOTO(SOCK_FD(client) < 0, "accept", error);
	return client;
error:
	if(client)
		free(client);
	return NULL;
}

int sock_addr_equal(socket_t* s1, socket_t* s2)
{
	if(s1->addr_len != s2->addr_len)
		return 0;
	return (memcmp(&s1->addr, &s2->addr, s1->addr_len) == 0);
}

void sock_close(socket_t* s)
{
	if(s->fd != -1) {
#ifdef _WIN32
		closesocket(s->fd);
#else
		close(s->fd);
#endif
		s->fd = -1;
	}
}

void sock_free(socket_t* s)
{
	free(s);
}

#ifdef _WIN32
char* sock_get_str(socket_t* s, char* buf, int len)
{
	return sock_get_addrstr(s, buf, len);
}
#else
char* sock_get_str(socket_t* s, char* buf, int len)
{
	void* src_addr;
	char addr_str[INET6_ADDRSTRLEN];
	unsigned short port;
	switch(s->addr.ss_family) {
	case AF_INET:
		src_addr = (void*)&((struct sockaddr_in*)&s->addr)->sin_addr;
		port = ntohs(((struct sockaddr_in*)&s->addr)->sin_port);
		break;
	case AF_INET6:
		src_addr = (void*)&((struct sockaddr_in6*)&s->addr)->sin6_addr;
		port = ntohs(((struct sockaddr_in6*)&s->addr)->sin6_port);
		break;
	default:
		return NULL;
	}
	if(inet_ntop(s->addr.ss_family, src_addr,
				 addr_str, sizeof(addr_str)) == NULL)
		return NULL;
	snprintf(buf, len, "%s:%hu", addr_str, port);
	return buf;
}
#endif /*WIN32*/

#ifdef _WIN32
char* sock_get_addrstr(socket_t* s, char* buf, int len)
{
	DWORD plen = len;
	if(WSAAddressToString((struct sockaddr*)&s->addr, s->addr_len,
						  NULL, buf, &plen) != 0) {
		return NULL;
	}
	return buf;
}
#else
char* sock_get_addrstr(socket_t* s, char* buf, int len)
{
	void* src_addr;
	switch(s->addr.ss_family) {
	case AF_INET:
		src_addr = (void*)&((struct sockaddr_in*)&s->addr)->sin_addr;
		break;
	case AF_INET6:
		src_addr = (void*)&((struct sockaddr_in6*)&s->addr)->sin6_addr;
		break;
	default:
		return NULL;
	}
	if(inet_ntop(s->addr.ss_family, src_addr, buf, len) == NULL)
		return NULL;
	return buf;
}
#endif /*WIN32*/

uint16_t sock_get_port(socket_t* s)
{
	switch(s->addr.ss_family) {
	case AF_INET:
		return (uint16_t)ntohs(((struct sockaddr_in*)&s->addr)->sin_port);
	case AF_INET6:
		return (uint16_t)
			   ntohs(((struct sockaddr_in6*)&s->addr)->sin6_port);
	}
	return 0;
}

int sock_recv(socket_t* sock, socket_t* from, char* data, int len)
{
	int bytes_recv = 0;
	socket_t tmp;
	switch(sock->type) {
	case SOCK_STREAM:
		bytes_recv = recv(sock->fd, data, len, 0);
		break;
	case SOCK_DGRAM:
		if(!from)
			from = &tmp;
		from->fd = sock->fd;
		from->addr_len = sock->addr_len;
		bytes_recv = recvfrom(from->fd, data, len, 0,
							  SOCK_ADDR(from), &from->addr_len);
		break;
	}
	#ifndef _WIN32
	if (bytes_recv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	PERROR_GOTO(bytes_recv<0, "recv", error);
	#else
	if(bytes_recv<0 && WSAGetLastError()!=WSAECONNRESET){
		printf("WSAGetLastError: %i\n",WSAGetLastError());goto error;
	}
	#endif /* _WIN32 */
	ERROR_GOTO(bytes_recv==0 && sock->type == SOCK_STREAM, "disconnect", disconnect);
	if(debug_level >= DEBUG_LEVEL3) {
		printf("sock_recv: type=%d, fd=%d, bytes=%d\n",
			   sock->type, sock->fd, bytes_recv);
		print_hexdump(data, bytes_recv);
	}
	return bytes_recv;
disconnect:
	return 0;
error:
	return -1;
}

int sock_send(socket_t* to, const char* data, int len)
{
	int bytes_sent = 0;
	int ret;
	switch(to->type) {
	case SOCK_STREAM:
		while(bytes_sent < len) {
			ret = send(to->fd, data + bytes_sent, len - bytes_sent, 0);
			PERROR_GOTO(ret < 0, "send", error);
			ERROR_GOTO(ret == 0, "disconnected", disconnect);
			bytes_sent += ret;
		}
		break;
	case SOCK_DGRAM:
		bytes_sent = sendto(to->fd, data, len, 0,
							SOCK_ADDR(to), to->addr_len);
		PERROR_GOTO(bytes_sent < 0, "sendto", error);
		break;
	default:
		return 0;
	}
	if(debug_level >= DEBUG_LEVEL3) {
		printf("sock_send: type=%d, fd=%d, bytes=%d\n",
			   to->type, to->fd, bytes_sent);
		print_hexdump((char*)data, bytes_sent);
	}
	return bytes_sent;
disconnect:
	return 0;
error:
	return -1;
}

int transport_send(transport_t *t, const char *data, int len) {
    if (t->type == TRANS_UDP) {
        return sock_send(t->sock, data, len);
    }
#ifdef HAVE_NICE
    else if (t->type == TRANS_ICE) {
        ice_transport_t *ice = (ice_transport_t *)t->ice_ptr;
        if (ice && ice->negotiated) {
            return nice_agent_send(ice->agent, ice->stream_id, ice->component_id, len, data);
        }
    }
#endif
    return -1;
}

int transport_recv(transport_t *t, socket_t *from, char *data, int len) {
    if (t->type == TRANS_UDP) {
        return sock_recv(t->sock, from, data, len);
    }
#ifdef HAVE_NICE
    else if (t->type == TRANS_ICE) {
        ice_transport_t *ice = (ice_transport_t *)t->ice_ptr;
        if (ice && ice->negotiated) {
             g_main_context_iteration(g_main_loop_get_context(ice->loop), FALSE);
             if (ice->recv_buf_len > 0) {
                 int to_copy = MIN(len, ice->recv_buf_len);
                 memcpy(data, ice->recv_buf, to_copy);
                 if (to_copy < ice->recv_buf_len) {
                     memmove(ice->recv_buf, ice->recv_buf + to_copy, ice->recv_buf_len - to_copy);
                 }
                 ice->recv_buf_len -= to_copy;
                 return to_copy;
             }
             return 0;
        }
    }
#endif
    return -1;
}

void print_hexdump(char* data, int len)
{
	int line;
	int max_lines = (len / 16) + (len % 16 == 0 ? 0 : 1);
	int i;
	for(line = 0; line < max_lines; line++) {
		printf("%08x  ", line * 16);
		for(i = line * 16; i < (8 + (line * 16)); i++) {
			if(i < len)
				printf("%02x ", (uint8_t)data[i]);
			else
				printf("   ");
		}
		printf(" ");
		for(i = (line * 16) + 8; i < (16 + (line * 16)); i++) {
			if(i < len)
				printf("%02x ", (uint8_t)data[i]);
			else
				printf("   ");
		}
		printf(" ");
		for(i = line * 16; i < (8 + (line * 16)); i++) {
			if(i < len) {
				if(32 <= data[i] && data[i] <= 126)
					printf("%c", data[i]);
				else
					printf(".");
			} else
				printf(" ");
		}
		printf(" ");
		for(i = (line * 16) + 8; i < (16 + (line * 16)); i++) {
			if(i < len) {
				if(32 <= data[i] && data[i] <= 126)
					printf("%c", data[i]);
				else
					printf(".");
			} else
				printf(" ");
		}
		printf("\n");
	}
}
