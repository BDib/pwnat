/*
 * pwnat, by Samy Kamkar
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef _WIN32
#	include "windoze.h"
#endif

#include "common.h"
#include "socket.h"

int opt_debug = 0;
struct sockaddr_in remote;

int debug_level = 1;
int ipver = SOCK_IPV4;
char* opt_key = NULL;
char* opt_stun = NULL;
char* opt_turn = NULL;
char* opt_turn_user = NULL;
char* opt_turn_pass = NULL;
int opt_relay = 0;

int udpclient(int argc, char* argv[]);
int udpserver(int argc, char* argv[]);
int udprelay(const char *stun_addr);
void usage(char* progname);

int main(int argc, char* argv[])
{
	int ret;
	int isserv = 0;
#ifdef _WIN32
	WSADATA wsa_data;
	ret = WSAStartup(MAKEWORD(2,0), &wsa_data);
	ERROR_GOTO(ret != 0, "WSAStartup() failed", error);
#endif
	while((ret = getopt(argc, argv, "hscv6k:t:u:p:r:")) != EOF) {
		switch(ret) {
		case 'k':
			opt_key = optarg;
			break;
		case 't':
			opt_turn = optarg;
			break;
		case 'u':
			opt_turn_user = optarg;
			break;
		case 'p':
			opt_turn_pass = optarg;
			break;
		case 'r':
			opt_relay = 1;
			opt_stun = optarg;
			break;
		case '6':
			ipver = SOCK_IPV6;
			break;
		case 's':
			isserv = 1;
			break;
		case 'c':
			isserv = 0;
			break;
		case 'v':
			if(debug_level < 3)
				debug_level++;
			break;
		case 'h':
		default:
			goto error;
		}
	}
	ret = 0;
	if (opt_relay) {
		ret = udprelay(opt_stun);
	} else if(isserv) {
		if(argc - optind < 0)
			goto error;
		ret = udpserver(argc - optind, argv + optind);
	} else {
		if(argc - optind != 5 && argc - optind != 6 && argc - optind != 4)
			goto error;
		ret = udpclient(argc - optind, argv + optind);
	}
#ifdef _WIN32
	WSACleanup();
#endif
	return ret;
error:
	usage(argv[0]);
	exit(1);
}

void usage(char* progname)
{
	printf("usage: %s <-s | -c | -r stun_server> [-k key] [-t turn_server -u user -p pass] <args>\n", progname);
	printf("  -c    client mode (default)\n"
		   "        <args>: [local ip] <local port> <proxy host> [proxy port (def:2222)] <remote host> <remote port>\n"
		   "  -s    server mode\n"
		   "        <args>: [local ip] [proxy port (def:2222)] [[allowed host]:[allowed port] ...]\n"
		   "  -r    relay mode (act as a STUN/TURN-like relay)\n"
		   "        <args>: <stun_server_ip>\n"
		   "  -k    encryption key (optional)\n"
		   "  -t    TURN server (optional fallback)\n"
		   "  -u    TURN username\n"
		   "  -p    TURN password\n"
		   "  -6    use IPv6\n"
		   "  -v    show debug output (up to 3)\n"
		   "  -h    show this help and exit\n");
}
