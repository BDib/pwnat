#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include "socket.h"
#include "common.h"

extern int debug_level;
static int running = 1;

static void signal_handler(int sig) {
    running = 0;
}

int udprelay(const char *stun_addr) {
    socket_t *udp_sock;
    char data[4096];
    struct pollfd fds[1];
    int ret;
    socket_t *from = sock_create(NULL, NULL, SOCK_IPV4, SOCK_TYPE_UDP, 0, 0);

    signal(SIGINT, &signal_handler);

    udp_sock = sock_create(NULL, "2222", SOCK_IPV4, SOCK_TYPE_UDP, 1, 1);
    if (!udp_sock) return -1;

    printf("Relay mode active. Listening on UDP 2222\n");
    if (stun_addr) {
        printf("Using STUN server: %s\n", stun_addr);
    }

    fds[0].fd = udp_sock->fd;
    fds[0].events = POLLIN;

    while (running) {
        ret = poll(fds, 1, 1000);
        if (ret > 0) {
            if (fds[0].revents & POLLIN) {
                int len = sock_recv(udp_sock, from, data, sizeof(data));
                if (len > 0) {
                    /* Simple relay logic: just echo back for now */
                    sock_send(from, data, len);
                }
            }
        }
    }

    sock_free(udp_sock);
    sock_free(from);
    return 0;
}
